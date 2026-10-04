import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { networkInterfaces } from 'node:os';

export function validateLocalFixtureHost(host, { requireOwned = false } = {}) {
  if (typeof host !== 'string' || !/^192\.168\.2\.(0|[1-9]\d{0,2})$/.test(host)) throw new Error('Local fixture requires an exact IPv4 desktop address in the lab subnet');
  const last = Number(host.split('.')[3]);
  if (last > 254 || [0, 63, 86].includes(last)) throw new Error('Local fixture cannot bind a network, simulator or controller address');
  if (requireOwned && !Object.values(networkInterfaces()).flat().some((entry) => entry?.family === 'IPv4' && entry.address === host)) throw new Error('Local fixture address is not owned by this desktop');
  return host;
}

export async function startLocalInputFixture(host) {
  validateLocalFixtureHost(host, { requireOwned: true });
  const child = spawn(process.env.OQ_HIL_PYTHON || 'python3', [fileURLToPath(new URL('./local-input-fixture.py', import.meta.url)), '--host', host], { stdio: ['pipe', 'pipe', 'pipe'] });
  const pending = new Map();
  let counter = 0;
  let exited = false;
  let closed = false;
  let readySeen = false;
  let stopping = false;
  let failureLatch;
  let buffer = '';
  let readyResolve;
  let readyReject;
  const ready = new Promise((resolve, reject) => { readyResolve = resolve; readyReject = reject; });
  const timeout = setTimeout(() => readyReject(new Error('Local input fixture did not verify controller identity within 45s')), 45000);
  const finished = new Promise((resolve) => child.once('close', () => { closed = true; resolve(); }));
  const shutdown = async () => {
    stopping = true;
    if (closed) return;
    if (!exited && child.stdin.writable) child.stdin.end(JSON.stringify({ stop: true }) + '\n');
    for (const [delay, signal] of [[1500, 'SIGTERM'], [3000, 'SIGKILL'], [3000, null]]) {
      let timer;
      await Promise.race([finished, new Promise((resolve) => { timer = setTimeout(resolve, delay); })]);
      clearTimeout(timer);
      if (closed) return;
      if (signal) child.kill(signal);
    }
    throw new Error('Local input fixture shutdown did not complete within 7.5s');
  };
  const failed = (error) => {
    failureLatch ??= error;
    readyReject(error);
    for (const callback of pending.values()) callback.reject(error);
    pending.clear();
  };
  child.once('error', failed);
  child.stdin.on('error', failed);
  child.once('exit', (code) => { exited = true; if (code !== 0 || !readySeen || !stopping) failed(new Error(`Local input fixture exited ${code}`)); });
  child.stderr.on('data', () => {}); // Connection failures are reported by type on stdout; no sensitive client diagnostics.
  child.stdout.on('data', (chunk) => {
    buffer += chunk.toString();
    for (;;) {
      const newline = buffer.indexOf('\n');
      if (newline < 0) break;
      const line = buffer.slice(0, newline);
      buffer = buffer.slice(newline + 1);
      let message;
      try { message = JSON.parse(line); } catch { failed(new Error('Invalid local input fixture protocol')); continue; }
      if (message.type === 'ready') {
        readySeen = true;
        console.log(`LOCAL_INPUT_FIXTURE ${JSON.stringify(message)}`);
        readyResolve(message);
      } else if (message.type === 'ack') pending.get(message.id)?.resolve(message.values);
      else if (message.type === 'retry') console.log(`NATIVE_HA_RETRY ${message.error}`);
    }
  });
  let metadata;
  try { metadata = await ready; } catch (error) { await shutdown(); throw error; } finally { clearTimeout(timeout); }
  const assertHealthy = () => { if (failureLatch) throw failureLatch; if (exited || stopping) throw new Error('Local input fixture is unavailable'); };
  return {
    ...metadata,
    assertHealthy,
    async send(values) {
      assertHealthy();
      const id = ++counter;
      let timer;
      try {
        return await new Promise((resolve, reject) => {
          timer = setTimeout(() => reject(new Error('Local input fixture command acknowledgement timed out')), 10000);
          pending.set(id, { resolve, reject });
          child.stdin.write(JSON.stringify({ id, ...values }) + '\n');
        });
      } finally { clearTimeout(timer); pending.delete(id); }
    },
    async stop() {
      await shutdown();
      if (failureLatch) throw failureLatch;
    },
  };
}
