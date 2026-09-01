const defaultSleep = (milliseconds) =>
  new Promise((resolve) => setTimeout(resolve, milliseconds));

export function normalizeBaseUrl(value, label = "target") {
  let parsed;
  try {
    parsed = new URL(value);
  } catch {
    throw new Error(`${label} must be an absolute http(s) URL`);
  }
  if (!['http:', 'https:'].includes(parsed.protocol)) {
    throw new Error(`${label} must use http or https`);
  }
  if (parsed.username || parsed.password || parsed.search || parsed.hash) {
    throw new Error(`${label} must not contain credentials, query parameters, or a fragment`);
  }
  parsed.pathname = parsed.pathname.replace(/\/+$/, '');
  return parsed.toString().replace(/\/$/, '');
}

export class RequestGate {
  constructor({
    readIntervalMs = 250,
    writeIntervalMs = 1500,
    now = () => Date.now(),
    sleep = defaultSleep,
  } = {}) {
    if (!Number.isFinite(readIntervalMs) || readIntervalMs < 0) {
      throw new Error('readIntervalMs must be a non-negative number');
    }
    if (!Number.isFinite(writeIntervalMs) || writeIntervalMs < 1000) {
      throw new Error('writeIntervalMs must be at least 1000 ms');
    }
    this.readIntervalMs = readIntervalMs;
    this.writeIntervalMs = writeIntervalMs;
    this.now = now;
    this.sleep = sleep;
    this.nextRequestAt = 0;
    this.counts = { read: 0, write: 0 };
    this.queue = Promise.resolve();
  }

  async enter({ write = false } = {}) {
    const waitMs = Math.max(0, this.nextRequestAt - this.now());
    if (waitMs > 0) await this.sleep(waitMs);
    this.counts[write ? 'write' : 'read'] += 1;
    this.nextRequestAt = this.now() + (write ? this.writeIntervalMs : this.readIntervalMs);
  }

  async run(options, operation) {
    const previous = this.queue;
    let release;
    this.queue = new Promise((resolve) => {
      release = resolve;
    });
    await previous;
    try {
      await this.enter(options);
      return await operation();
    } finally {
      release();
    }
  }
}

export function entityPath(domain, name, action = '') {
  return `/${domain}/${encodeURIComponent(name)}${action ? `/${action}` : ''}`;
}

export function asFiniteNumber(value) {
  if (value === null || value === undefined || value === '' || value === 'nan' || value === 'NaN') {
    return null;
  }
  const number = Number(value);
  return Number.isFinite(number) ? number : null;
}

export function asBoolean(value) {
  if (value === true || value === 1 || value === '1' || value === 'ON' || value === 'on' || value === 'true') {
    return true;
  }
  if (value === false || value === 0 || value === '0' || value === 'OFF' || value === 'off' || value === 'false') {
    return false;
  }
  throw new Error(`cannot convert ${JSON.stringify(value)} to a boolean`);
}

export class HilRestClient {
  constructor({
    baseUrl,
    allowWrites = false,
    gate = new RequestGate(),
    fetchImpl = globalThis.fetch,
    timeoutMs = 12000,
    onRequest = () => {},
    bulkReads = false,
  }) {
    if (typeof fetchImpl !== 'function') throw new Error('fetch is unavailable');
    this.baseUrl = normalizeBaseUrl(baseUrl);
    this.allowWrites = allowWrites;
    this.gate = gate;
    this.fetchImpl = fetchImpl;
    this.timeoutMs = timeoutMs;
    this.onRequest = onRequest;
    this.bulkReads = bulkReads;
  }

  async request(endpoint, {
    method = 'GET',
    optional = false,
    readOnly = false,
    body,
    headers = {},
  } = {}) {
    const normalizedMethod = method.toUpperCase();
    const write = normalizedMethod !== 'GET' && !readOnly;
    if (write && !this.allowWrites) {
      throw new Error('REST writes require the explicit --apply flag');
    }
    return this.gate.run({ write }, async () => {
      this.onRequest({ baseUrl: this.baseUrl, endpoint, method: normalizedMethod, readOnly });
      const response = await this.fetchImpl(`${this.baseUrl}${endpoint}`, {
        method: normalizedMethod,
        body: body ?? (normalizedMethod === 'POST' ? '' : undefined),
        signal: AbortSignal.timeout(this.timeoutMs),
        headers: { 'Cache-Control': 'no-store', ...headers },
      });
      if (optional && response.status === 404) return null;
      if (!response.ok) {
        throw new Error(`${normalizedMethod} ${endpoint}: ${response.status} ${response.statusText}`);
      }
      const text = await response.text();
      if (!text) return null;
      try {
        return JSON.parse(text);
      } catch {
        throw new Error(`${normalizedMethod} ${endpoint}: response is not valid JSON`);
      }
    });
  }

  async value(domain, name, { optional = false } = {}) {
    if (this.bulkReads) {
      const values = await this.values([{ key: 'value', domain, name, optional }]);
      return values.value;
    }
    const response = await this.request(entityPath(domain, name), { optional });
    return response?.value ?? null;
  }

  async values(entities) {
    if (!this.bulkReads) {
      const result = {};
      for (const entity of entities) {
        result[entity.key] = await this.value(entity.domain, entity.name, {
          optional: entity.optional === true,
        });
      }
      return result;
    }
    const lines = entities.map((entity) => `${entity.key}\t${entity.domain}\t${entity.name}`);
    const form = new URLSearchParams({ detail: 'state', entities: lines.join('\n') });
    const payload = await this.request('/openquatt/entities', {
      method: 'POST',
      readOnly: true,
      body: form.toString(),
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
    });
    const missing = new Set(Array.isArray(payload?.missing) ? payload.missing : []);
    const result = {};
    for (const entity of entities) {
      const item = payload?.entities?.[entity.key];
      if (!item || missing.has(entity.key)) {
        if (entity.optional) {
          result[entity.key] = null;
          continue;
        }
        throw new Error(`bulk entity response is missing ${entity.domain}/${entity.name}`);
      }
      result[entity.key] = item.value ?? item.state ?? null;
    }
    return result;
  }

  async post(domain, name, action, query = {}) {
    const search = new URLSearchParams(query).toString();
    const suffix = search ? `?${search}` : '';
    return this.request(`${entityPath(domain, name, action)}${suffix}`, { method: 'POST' });
  }

  setNumber(name, value) {
    return this.post('number', name, 'set', { value: String(value) });
  }

  setSelect(name, option) {
    return this.post('select', name, 'set', { option });
  }

  setSwitch(name, enabled) {
    return this.post('switch', name, enabled ? 'turn_on' : 'turn_off');
  }

  press(name) {
    return this.post('button', name, 'press');
  }
}
