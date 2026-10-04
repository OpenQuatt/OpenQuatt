import assert from 'node:assert/strict';
import test from 'node:test';
import { chmod, mkdtemp, readFile, readdir, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import { describeHilFailure, parseArgs, run } from '../../scripts/hil/run-input-sources.mjs';
import { controllerSettings, simulatorSettings, SNAPSHOT_SCHEMA } from '../../scripts/hil/session.mjs';
import { WARMUP_STAGES } from '../../scripts/hil/run-controlled-warmup.mjs';
import { assertIdleBaseline, controlledWarmupScenario, validateWarmupExtra, warmupExtraSettings, warmupClose, warmupNumber, waitDurationHandback } from './scenarios/controlled-warmup.mjs';

test('duration handback waits for coherent status/goal and cannot accept a different cancellation', async () => {
  let calls = 0;
  const controller = { values: async () => ++calls === 1 ? { active: false, status: 'Warming', target: 19.1 } : { active: false, status: 'Time limit reached', target: 21 } };
  await waitDurationHandback(controller, () => false, 2500);
  assert.equal(calls, 2);
  controller.values = async () => ({ active: false, status: 'Disabled', target: 21 });
  await assert.rejects(waitDurationHandback(controller, () => false, 10), /coherent duration deadline handback/);
});

test('unavailable or boolean actuator telemetry cannot be treated as numeric zero', () => {
  for (const value of [null, undefined, '', ' ', false, true, NaN, [], [0], {}]) {
    assert.equal(warmupNumber(value), null);
    assert.equal(warmupClose(value, 0), false);
  }
  assert.equal(warmupClose('0', 0), true);
});

test('nested run and recovery failures retain both concrete causes', () => {
  const described = describeHilFailure(new AggregateError([new Error('CH withdrawal boundary'), new AggregateError([new Error('CM0 safety timeout')], 'recovery failed')], 'run and recovery failed'));
  assert.match(described, /CH withdrawal boundary/);
  assert.match(described, /CM0 safety timeout/);
});

test('controlled warmup HIL requires mutation opt-in and normal firmware recovery', () => {
  const args = ['--controller', 'http://controller.test', '--simulator', 'http://simulator.test', '--stage', 'duration'];
  assert.throws(() => parseArgs(args, { validStages: WARMUP_STAGES }), /--apply/);
  assert.throws(() => parseArgs([...args, '--apply'], { validStages: WARMUP_STAGES }), /--device and --restore-config/);
});

test('corrupt warmup recovery fails before locks, actuator writes and OTA', async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'warmup-recovery-test-'));
  const targets = { controller: 'http://controller.test', simulator: 'http://simulator.test' };
  const makeSettings = (settings) => Object.fromEntries(settings.map((setting) => [setting.key, setting.kind === 'number' ? 1 : setting.kind === 'switch' ? false : 'Auto']));
  const snapshot = {
    schema: SNAPSHOT_SCHEMA, targets, firmware: '2026.9.0 (config hash 0x12345678)', scenario: 'controlled-warmup',
    controller: makeSettings(controllerSettings), simulator: makeSettings(simulatorSettings),
    simulatorActive: { hp1: { address: 1, profile: 'V1.5' }, hp2: { address: 2, profile: 'V1.5' } },
    extra: { schema: 1, values: {} },
  };
  const file = path.join(directory, 'snapshot.json');
  await writeFile(file, JSON.stringify(snapshot));
  try {
    const options = parseArgs(['--controller', targets.controller, '--simulator', targets.simulator, '--restore-snapshot', file, '--device', 'controller.test', '--restore-config', 'configs/heatpump_controller_q/duo_hil.yaml', '--apply']);
    await assert.rejects(run(options, controlledWarmupScenario), /invalid recovery/);
    const reportFile = (await readdir(directory)).find((name) => name.startsWith('recovery-'));
    const report = JSON.parse(await readFile(path.join(directory, reportFile), 'utf8'));
    assert.equal(report.requests.length, 0);
    assert.equal(report.requestCounts.write, 0);
    assert.equal(report.restoredFirmware, undefined);
  } finally { await rm(directory, { recursive: true, force: true }); }
});

test('standalone regulation without native fixture fails before any device request', async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'warmup-regulation-preflight-'));
  const previous = process.env.OQ_HIL_NATIVE_API_KEY;
  delete process.env.OQ_HIL_NATIVE_API_KEY;
  try {
    const options = parseArgs(['--controller', 'http://controller.test', '--simulator', 'http://simulator.test', '--stage', 'regulation', '--device', 'controller.test', '--restore-config', 'configs/heatpump_controller_q/duo_hil.yaml', '--output-root', directory, '--apply'], { validStages: WARMUP_STAGES });
    await assert.rejects(run(options, controlledWarmupScenario), /before any device mutation/);
    const runDir = (await readdir(directory))[0];
    const report = JSON.parse(await readFile(path.join(directory, runDir, 'report.json'), 'utf8'));
    assert.equal(report.requests.length, 0);
    assert.equal(report.requestCounts.write, 0);
  } finally {
    if (previous === undefined) delete process.env.OQ_HIL_NATIVE_API_KEY; else process.env.OQ_HIL_NATIVE_API_KEY = previous;
    await rm(directory, { recursive: true, force: true });
  }
});

test('warmup recovery rejects missing/corrupt permission and parameter blocks', () => {
  const values = Object.fromEntries(warmupExtraSettings.map((setting) => [setting.key, setting.domain === 'number' ? setting.min : setting.domain === 'switch' ? false : setting.options[0]]));
  assert.equal(validateWarmupExtra({ schema: 1, values }).values.enabled, false);
  assert.throws(() => validateWarmupExtra(undefined), /missing controlled-warmup/);
  assert.throws(() => validateWarmupExtra({ schema: 1, values: { ...values, enabled: 'false' } }), /invalid recovery/);
  assert.throws(() => validateWarmupExtra({ schema: 1, values: { ...values, 'step time': 0 } }), /invalid recovery/);
  assert.throws(() => validateWarmupExtra({ schema: 1, values: { ...values, step: NaN } }), /invalid recovery/);
  assert.throws(() => validateWarmupExtra({ schema: 1, values: { ...values, supplySource: 'invalid option' } }), /invalid recovery/);
  assert.throws(() => validateWarmupExtra({ schema: 1, values: { ...values, curveProfile: 'invalid option' } }), /invalid recovery/);
});

test('legacy warmup snapshot passes real recovery preflight before an injected read failure', async () => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'warmup-legacy-recovery-'));
  const targets = { controller: 'http://controller.test', simulator: 'http://simulator.test' };
  const makeSettings = (settings) => Object.fromEntries(settings.map((setting) => [setting.key, setting.kind === 'number' ? 1 : setting.kind === 'switch' ? false : 'Auto']));
  const values = Object.fromEntries(warmupExtraSettings.filter((setting) => setting.key !== 'coolingRoomRequired').map((setting) => [setting.key, setting.domain === 'number' ? setting.min : setting.domain === 'switch' ? false : setting.options[0]]));
  const snapshot = { schema: SNAPSHOT_SCHEMA, targets, firmware: '2026.9.0 (config hash 0x12345678)', scenario: 'controlled-warmup', controller: makeSettings(controllerSettings), simulator: makeSettings(simulatorSettings), simulatorActive: { hp1: { address: 1, profile: 'V1.5' }, hp2: { address: 2, profile: 'V1.5' } }, extra: { schema: 1, values } };
  const file = path.join(directory, 'snapshot.json');
  await writeFile(file, JSON.stringify(snapshot));
  const previousFetch = globalThis.fetch;
  const previousLockRoot = process.env.OQ_HIL_LOCK_ROOT;
  process.env.OQ_HIL_LOCK_ROOT = path.join(directory, 'unit-locks');
  globalThis.fetch = async () => new Response('{}', { status: 503, statusText: 'Injected read failure' });
  try {
    const options = parseArgs(['--controller', targets.controller, '--simulator', targets.simulator, '--restore-snapshot', file, '--settings-only', '--apply']);
    await assert.rejects(run(options, controlledWarmupScenario), /503 Injected read failure/);
    const reportFile = (await readdir(directory)).find((name) => name.startsWith('recovery-'));
    const report = JSON.parse(await readFile(path.join(directory, reportFile), 'utf8'));
    assert(report.requests.length > 0);
    assert.equal(report.requestCounts.write, 0);
  } finally {
    globalThis.fetch = previousFetch;
    if (previousLockRoot === undefined) delete process.env.OQ_HIL_LOCK_ROOT; else process.env.OQ_HIL_LOCK_ROOT = previousLockRoot;
    await rm(directory, { recursive: true, force: true });
  }
});

test('schema 2 requires cooling permission while legacy schema 1 restores only recorded settings', async () => {
  const values = Object.fromEntries(warmupExtraSettings.map((setting) => [setting.key, setting.domain === 'number' ? setting.min : setting.domain === 'switch' ? false : setting.options[0]]));
  const legacy = { ...values }; delete legacy.coolingRoomRequired;
  assert.equal(validateWarmupExtra({ schema: 1, values: legacy }).values.coolingRoomRequired, undefined);
  assert.throws(() => validateWarmupExtra({ schema: 2, values: legacy }), /invalid recovery Cooling Room Request Required/);
  assert.throws(() => validateWarmupExtra({ schema: 1, values: { ...values, coolingRoomRequired: 'false' } }), /invalid recovery Cooling Room Request Required/);
  const state = Object.fromEntries(warmupExtraSettings.map((setting) => [setting.name, values[setting.key]]));
  state['Cooling Room Request Required'] = true;
  const writes = [];
  const setter = async (name, value) => { writes.push(name); state[name] = value; };
  const client = { value: async (domain, name) => state[name], setNumber: setter, setSelect: setter, setSwitch: setter };
  await controlledWarmupScenario.afterRestore({ controller: client, simulator: client, snapshot: { extra: { schema: 1, values: legacy } } });
  assert.equal(state['Cooling Room Request Required'], true);
  assert.equal(writes.includes('Cooling Room Request Required'), false);
});

test('active manual/service baseline is rejected using read-only requests', async () => {
  const entities = Object.fromEntries(['cm100Active', 'boilerPowerTestActive', 'airPurgeActive', 'manualFlowActive', 'manualHpActive', 'hpWaterCalibrationActive'].map((name) => [name, { value: false }]));
  const client = { value: async () => 'CM100', request: async () => ({ entities }), setSelect: () => assert.fail('unexpected write') };
  await assert.rejects(assertIdleBaseline(client), /active service/);
  client.value = async () => 'CM0';
  entities.manualHpActive.value = true;
  await assert.rejects(assertIdleBaseline(client), /active service/);
  entities.manualHpActive.value = false;
  await assertIdleBaseline(client);
  for (const mode of [null, 'unknown', 0]) {
    client.value = async () => mode;
    await assert.rejects(assertIdleBaseline(client), /baseline is unavailable or unknown/);
  }
});

test('lost manual-abort acknowledgement still attempts CM0 and all independent safety writes', async () => {
  const calls = [];
  const values = { 'Control Mode': 'CM0' };
  const client = {
    setSelect: async (name, value) => { calls.push(name); values[name] = value; },
    setSwitch: async (name, value) => { calls.push(name); values[name] = value; },
    press: async (name) => { calls.push(name); if (name === 'Manual Flow Abort') throw new Error('lost abort acknowledgement'); },
    setNumber: async (name, value) => { calls.push(name); values[name] = value; },
    value: async (domain, name) => values[name],
  };
  await assert.rejects(controlledWarmupScenario.beforeRestore({ controller: client }), /OTA blocked/);
  assert.deepEqual(calls, ['CM Override', 'Manual Flow Abort', 'CM100 Stop', 'api_input_outside_temperature', 'Manual Cooling Enable', 'api_input_cooling_enable', 'api_input_heating_enable']);
  assert.equal(values['CM Override'], 'Force CM0');
});
