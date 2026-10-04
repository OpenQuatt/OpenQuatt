import { spawn } from 'node:child_process';
import { fileURLToPath } from 'node:url';

export async function startNativeHaFixture(host) {
  if (!process.env.OQ_HIL_NATIVE_API_KEY) throw new Error('Native HA HIL needs a private OQ_HIL_NATIVE_API_KEY and matching lab-only firmware');
  if (!['192.168.2.86', 'openquatt-test.local'].includes(host)) throw new Error('Native HA fixture is restricted to the documented desktop testcontroller');
  const child = spawn(process.env.OQ_HIL_PYTHON || 'python3', [fileURLToPath(new URL('./native-ha-fixture.py', import.meta.url)), '--host', host], { stdio: ['pipe', 'pipe', 'pipe'] });
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
  const timeout = setTimeout(() => readyReject(new Error('Native HA fixture did not verify controller identity within 45s')), 45000);
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
    throw new Error('Native HA fixture shutdown did not complete within 7.5s');
  };
  const failed = (error) => {
    failureLatch ??= error;
    readyReject(error);
    for (const callback of pending.values()) callback.reject(error);
    pending.clear();
  };
  child.once('error', failed);
  child.stdin.on('error', failed);
  child.once('exit', (code) => { exited = true; if (code !== 0 || !readySeen || !stopping) failed(new Error(`Native HA fixture exited ${code}`)); });
  child.stderr.on('data', () => {}); // Connection failures are reported by type on stdout; no sensitive client diagnostics.
  child.stdout.on('data', (chunk) => {
    buffer += chunk.toString();
    for (;;) {
      const newline = buffer.indexOf('\n');
      if (newline < 0) break;
      const line = buffer.slice(0, newline);
      buffer = buffer.slice(newline + 1);
      let message;
      try { message = JSON.parse(line); } catch { failed(new Error('Invalid native HA fixture protocol')); continue; }
      if (message.type === 'ready') {
        readySeen = true;
        console.log(`NATIVE_HA_IDENTITY ${JSON.stringify(message)}`);
        readyResolve(message);
      } else if (message.type === 'ack') pending.get(message.id)?.resolve(message.values);
      else if (message.type === 'retry') console.log(`NATIVE_HA_RETRY ${message.error}`);
    }
  });
  try { await ready; } catch (error) { await shutdown(); throw error; } finally { clearTimeout(timeout); }
  const assertHealthy = () => { if (failureLatch) throw failureLatch; if (exited || stopping) throw new Error('Native HA fixture is unavailable'); };
  return {
    assertHealthy,
    async send(values) {
      assertHealthy();
      const id = ++counter;
      let timer;
      try {
        return await new Promise((resolve, reject) => {
          timer = setTimeout(() => reject(new Error('Native HA fixture command acknowledgement timed out')), 10000);
          pending.set(id, { resolve, reject });
          child.stdin.write(JSON.stringify({ id, values }) + '\n');
        });
      } finally { clearTimeout(timer); pending.delete(id); }
    },
    async stop() {
      await shutdown();
      if (failureLatch) throw failureLatch;
    },
  };
}
