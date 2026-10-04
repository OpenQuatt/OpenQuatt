import assert from 'node:assert/strict';
import test from 'node:test';
import { mkdtemp, readdir, readFile, rm } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { parseArgs, run } from '../../scripts/hil/run-input-sources.mjs';
import { execFileSync } from 'node:child_process';
import { validateLocalFixtureHost } from '../../scripts/hil/local-input-fixture.mjs';
import { warmupTransportScenario, validateTransportExtra } from './scenarios/warmup-transports.mjs';
import { warmupExtraSettings } from './scenarios/controlled-warmup.mjs';

const makeExtra = () => ({ schema: 1, values: Object.fromEntries(warmupExtraSettings.map((setting) => [setting.key, setting.domain === 'number' ? setting.min : setting.domain === 'switch' ? false : setting.options[0]])), localInputs: { cicUrl: '', cicPolling: false, mqtt: { broker: '', username: '', port: 1883, enabled: false, password_set: false, input_enabled: { room_temperature: false, room_setpoint: false }, input_accept_retained: { room_temperature: false, room_setpoint: false } } } });

test('fixture bind preflight rejects unsafe addresses before requests or mutation', () => {
  const previous = process.env.OQ_HIL_FIXTURE_HOST;
  try {
    for (const host of ['127.0.0.1', '192.168.2.86', '192.168.2.63', '192.168.2.999', '192.168.2.0', '192.168.2.255']) {
      process.env.OQ_HIL_FIXTURE_HOST = host;
      assert.throws(() => warmupTransportScenario.preflight({ stage: 'transports' }), /Local fixture/);
    }
    assert.equal(validateLocalFixtureHost('192.168.2.103'), '192.168.2.103');
  } finally {
    if (previous === undefined) delete process.env.OQ_HIL_FIXTURE_HOST; else process.env.OQ_HIL_FIXTURE_HOST = previous;
  }
});

test('invalid fixture binding in the real runner produces zero requests and zero mutations', async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'warmup-fixture-preflight-'));
  const previous = process.env.OQ_HIL_FIXTURE_HOST;
  try {
    process.env.OQ_HIL_FIXTURE_HOST = '192.168.2.86';
    const options = parseArgs(['--controller', 'http://controller.test', '--simulator', 'http://simulator.test', '--stage', 'transports', '--device', 'controller.test', '--restore-config', 'configs/heatpump_controller_q/duo_hil.yaml', '--output-root', directory, '--apply'], { validStages: new Set(['transports']) });
    await assert.rejects(run(options, warmupTransportScenario), /Local fixture/);
    const runDir = (await readdir(directory))[0];
    const report = JSON.parse(await readFile(path.join(directory, runDir, 'report.json'), 'utf8'));
    assert.equal(report.requests.length, 0);
    assert.equal(report.requestCounts.write, 0);
  } finally {
    if (previous === undefined) delete process.env.OQ_HIL_FIXTURE_HOST; else process.env.OQ_HIL_FIXTURE_HOST = previous;
    await rm(directory, { recursive: true, force: true });
  }
});

test('transport snapshot preserves credential flags and rejects unsafe clearing before writes', () => {
  const extra = makeExtra();
  assert.equal(validateTransportExtra(extra), extra);
  extra.localInputs.mqtt.password_set = true;
  assert.throws(() => validateTransportExtra(extra), /cannot be restored/);
  extra.localInputs.mqtt.broker = 'original.local';
  assert.equal(validateTransportExtra(extra), extra);
  extra.localInputs.mqtt.input_accept_retained.room_setpoint = null;
  assert.throws(() => validateTransportExtra(extra), /input recovery permission/);
});

test('minimal broker frames only bounded protocol messages and refuses credential CONNECT', () => {
  const program = `
import importlib.util, json
spec = importlib.util.spec_from_file_location('fixture', 'scripts/hil/local-input-fixture.py')
f = importlib.util.module_from_spec(spec); spec.loader.exec_module(f)
class Socket:
    def __init__(self, data): self.data = bytearray(data); self.sent = []
    def settimeout(self, value): pass
    def recv(self, count):
        result = bytes(self.data[:count]); del self.data[:count]; return result
    def sendall(self, data): self.sent.append(data.hex())
def connect(flags):
    body = bytes.fromhex('00044d51545404') + bytes([flags]) + bytes.fromhex('001e000178')
    sock = Socket(b'\\x10' + f.encode_length(len(body)) + body)
    f.MqttHandler(sock, ('192.168.2.86', 1), None)
    return sock.sent
print(json.dumps({'lengths': [f.encode_length(x).hex() for x in [0, 127, 128, 16384]], 'publish': f.publish_packet('t', '19').hex(), 'anonymous': connect(2), 'credentials': connect(194)}))
`;
  const result = JSON.parse(execFileSync(process.env.OQ_HIL_PYTHON || 'python3', ['-c', program], { encoding: 'utf8' }));
  assert.deepEqual(result.lengths, ['00', '7f', '8001', '808001']);
  assert.equal(result.publish, '30050001743139');
  assert.deepEqual(result.anonymous, ['20020000']);
  assert.deepEqual(result.credentials, ['20020005']);
});
