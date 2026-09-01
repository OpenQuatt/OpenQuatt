import { asFiniteNumber } from './rest-client.mjs';

const sleep = (milliseconds) => new Promise((resolve) => setTimeout(resolve, milliseconds));

export async function waitFor(check, label, {
  timeoutMs = 90000,
  intervalMs = 1000,
  interrupted = () => false,
} = {}) {
  const deadline = Date.now() + timeoutMs;
  let lastError;
  while (Date.now() < deadline) {
    if (interrupted()) throw new Error('HIL run interrupted; starting recovery');
    try {
      const result = await check();
      if (result) return result;
    } catch (error) {
      lastError = error;
    }
    await sleep(intervalMs);
  }
  throw new Error(`${label} timed out${lastError ? `: ${lastError.message}` : ''}`);
}

export function waitValue(client, domain, name, expected, label, options = {}) {
  return waitFor(
    async () => String(await client.value(domain, name)) === String(expected),
    label,
    options,
  );
}

export function waitNumber(controller, name, predicate, label, options = {}) {
  return waitFor(async () => {
    const current = asFiniteNumber(await controller.value('sensor', name));
    return current !== null && predicate(current) ? current : false;
  }, label, options);
}

export function waitUnavailable(controller, name, label, options = {}) {
  return waitFor(
    async () => asFiniteNumber(await controller.value('sensor', name)) === null,
    label,
    options,
  );
}
