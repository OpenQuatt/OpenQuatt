import assert from 'node:assert/strict';
import test from 'node:test';
import {
  controlContext, incidentObservation, prepareControlRegression,
} from '../../scripts/hil/control-regression.mjs';
import { parseDuoArgs } from '../../scripts/hil/run-duo.mjs';
import { parseMonoCommunicationsArgs } from '../../scripts/hil/run-communications-mono.mjs';
import { parseCommunicationsArgs } from '../../scripts/hil/run-communications.mjs';
import {
  controllerSettings, simulatorSettings, controlRegressionControllerSettings,
  controlRegressionSimulatorSettings, snapshotSettings, restoreSettings, validateSnapshot,
  verifyRestoredSettings,
} from '../../scripts/hil/session.mjs';
import { runDuoScenarios } from './scenarios/duo.mjs';
import { runCommunicationsScenarios } from './scenarios/communications.mjs';

function bench(hpCount = 2) {
  let time = 0;
  const responses = Array(hpCount).fill(true);
  const writes = [];
  let mode = 0;
  let transition = false;
  let controller;
  const simulator = {
    async setSwitch(name, value) {
      const index = /^ODU ([12]) responses enabled$/.exec(name);
      if (!index) throw new Error(`unexpected simulator switch ${name}`);
      writes.push([Number(index[1]), value]);
      responses[Number(index[1]) - 1] = value;
      if (value && mode === 4 && responses.every(Boolean)) transition = true;
    },
    async value(domain, name) {
      return responses[Number(/^ODU ([12]) responses enabled$/.exec(name)[1]) - 1];
    },
  };
  controller = {
    async values() { return { outside: 7, room: 20, setpoint: 24, demand: 4000, enable: true, valid: true }; },
    async setNumber() {},
    async setSwitch() {},
    async setSelect(name, option) {
      if (name === 'CM Override') {
        mode = option === 'Force CM0' ? 0 : 1;
        transition = option === 'Auto';
      }
    },
    async value() { return mode === 4; },
    async request() {
      if (mode !== 0) {
        if (responses.every((v) => !v)) mode = 4;
        else if (transition) { mode = 1; transition = false; }
        else mode = 2;
      }
      return {
        schema_version: 1, catalog_version: 1, action_csrf_token: 'secret-do-not-retain',
        system: { control_mode: mode, boiler_command_active: mode === 4 },
        heat_pumps: responses.map((responding, i) => ({
          index: i + 1, link_state: responding ? 'healthy' : 'lost',
          available_for_start: responding, running_confirmed: responding && mode === 2,
          stop_confirmed: responding && mode === 0, must_stop: !responding,
          stop_unconfirmed: !responding,
          stop_unconfirmed_due_to_link_loss: !responding,
        })),
      };
    },
  };
  return {
    controller, simulator, responses, writes,
    now: () => time, delay: async (ms) => { time += ms; },
    timeoutMs: 20, holdMs: 3, intervalMs: 1,
  };
}

test('Duo stages drive both outage directions and require physical start feedback', async () => {
  for (const stage of ['start', 'peer-loss', 'all']) {
    const fixture = bench();
    const result = await runDuoScenarios({ ...fixture, stage });
    const expected = stage === 'all' ? 4 : 2;
    assert.equal(result.cases.length, expected);
    assert.deepEqual([...new Set(result.cases.map((item) => item.lost))], [1, 2]);
    assert.deepEqual(fixture.responses, [true, true]);
    assert.ok(result.samples.some((s) => s.mode === 1));
    assert.ok(result.samples.some((s) => s.hp[0].link_state === 'lost' && s.hp[1].running_confirmed));
    assert.ok(result.samples.some((s) => s.hp[1].link_state === 'lost' && s.hp[0].running_confirmed));
    assert.ok(!JSON.stringify(result).includes('secret-do-not-retain'));
  }
});

test('communications proves causal fallback and CM1 handback in one outage', async () => {
  const fixture = bench();
  const result = await runCommunicationsScenarios({ ...fixture, stage: 'fallback' });
  assert.equal(result.cases.length, 2);
  assert.deepEqual(fixture.writes, [[1, false], [2, false], [1, true], [2, true]]);
  const fallback = result.samples.findIndex((s) => s.mode === 4);
  assert.ok(fallback >= 0);
  assert.ok(result.samples.slice(fallback).some((s) => s.mode === 1));
  assert.ok(result.samples.slice(fallback).some((s) => s.mode === 2 &&
    s.hp.every((hp) => !hp.stop_unconfirmed_due_to_link_loss)));
});

test('CM2 alone is insufficient: missing compressor feedback fails and releases response gate', async () => {
  const fixture = bench();
  const request = fixture.controller.request;
  fixture.controller.request = async () => {
    const payload = await request();
    payload.heat_pumps.forEach((hp) => { hp.running_confirmed = false; });
    return payload;
  };
  await assert.rejects(runDuoScenarios({ ...fixture, stage: 'start' }), /cleanup failed/);
  assert.deepEqual(fixture.responses, [true, true]);
});

test('unexpected partial-outage boiler activation fails instead of being swallowed by polling', async () => {
  const fixture = bench();
  const value = fixture.controller.value;
  fixture.controller.value = async (...args) => fixture.responses.some((r) => !r) || await value(...args);
  await assert.rejects(runDuoScenarios({ ...fixture, stage: 'start' }), /cleanup failed/);
  assert.deepEqual(fixture.responses, [true, true]);
});

test('communications rejects early fallback without provenance and CM4 relapse after CM1', async () => {
  for (const fault of ['early', 'relapse']) {
    const fixture = bench();
    const request = fixture.controller.request;
    let recovering = false;
    let altered = false;
    fixture.controller.request = async () => {
      const payload = await request();
      if (!altered && fault === 'early' && payload.system.control_mode === 4) {
        payload.heat_pumps[0].stop_unconfirmed_due_to_link_loss = false;
        altered = true;
      }
      if (fault === 'relapse' && fixture.writes.some(([hp, enabled]) => hp === 2 && enabled)) {
        if (payload.system.control_mode === 1) recovering = true;
        else if (recovering && !altered) { payload.system.control_mode = 4; altered = true; }
      }
      return payload;
    };
    await assert.rejects(runCommunicationsScenarios({ ...fixture, stage: 'fallback' }),
      fault === 'early' ? /before causal stop timeout/ : /handback reverted/);
    assert.ok(altered);
    assert.deepEqual(fixture.responses, [true, true]);
  }
});

test('hold rejects wrong CM with stale running feedback and loss of feedback during handback', async () => {
  for (const fault of ['duo-mode', 'handback-feedback']) {
    const fixture = bench();
    const request = fixture.controller.request;
    let matches = 0;
    fixture.controller.request = async () => {
      const payload = await request();
      const inDuoHold = fault === 'duo-mode' && !fixture.responses[0] && fixture.responses[1] &&
        payload.system.control_mode === 2;
      const inHandbackHold = fault === 'handback-feedback' &&
        fixture.writes.some(([hp, enabled]) => hp === 2 && enabled) && payload.system.control_mode === 2;
      if ((inDuoHold || inHandbackHold) && ++matches > 1) {
        if (inDuoHold) payload.system.control_mode = 1;
        else payload.heat_pumps.forEach((hp) => { hp.running_confirmed = false; });
      }
      return payload;
    };
    await assert.rejects(fault === 'duo-mode'
      ? runDuoScenarios({ ...fixture, stage: 'peer-loss' })
      : runCommunicationsScenarios({ ...fixture, stage: 'fallback' }),
    fault === 'duo-mode' ? /remaining peer lost/ : /not stable/);
    assert.ok(matches > 1);
    assert.deepEqual(fixture.responses, [true, true]);
  }
});

test('stale or wrongly selected API demand is not allowed to produce a hardware PASS', async () => {
  const fixture = bench();
  fixture.controller.values = async () => ({ outside: 7, room: 20, setpoint: 24,
    demand: null, enable: true, valid: true });
  await assert.rejects(runDuoScenarios({ ...fixture, stage: 'start' }), /API demand selected and fresh.*timed out/);
  assert.deepEqual(fixture.responses, [true, true]);
});

test('ambiguous suppression ACK, interrupts and cleanup failures attempt every affected gate', async () => {
  for (const fault of ['ack', 'interrupt', 'cleanup']) {
    const fixture = bench();
    const original = fixture.simulator.setSwitch;
    fixture.simulator.setSwitch = async (name, value) => {
      await original(name, value);
      if ((fault === 'ack' && !value && name.includes(' 2 ')) ||
          (fault === 'cleanup' && value && name.includes(' 1 '))) throw new Error(fault);
    };
    let stop = false;
    const ctx = controlContext({ ...fixture, interrupted: () => stop });
    await assert.rejects(ctx.withLoss([1, 2], async () => {
      if (fault === 'interrupt') { stop = true; await ctx.until('interrupt', () => false); }
      if (fault === 'cleanup') throw new Error('stage failure');
    }), /cleanup failed/);
    assert.deepEqual(fixture.responses, [true, true]);
    assert.ok(fixture.writes.some(([hp, enabled]) => hp === 2 && enabled));
  }
});

test('snapshot projection fails closed on missing peers, provenance or unsupported schema', async () => {
  const fixture = bench();
  const payload = await fixture.controller.request();
  for (const mutate of [
    (p) => { p.schema_version = 2; },
    (p) => { p.heat_pumps.pop(); },
    (p) => { delete p.heat_pumps[1].stop_unconfirmed_due_to_link_loss; },
    (p) => { p.heat_pumps[1].index = 1; },
  ]) {
    const bad = structuredClone(payload);
    mutate(bad);
    assert.throws(() => incidentObservation(bad));
  }
});

class SettingsClient {
  constructor(settings) {
    this.state = new Map(settings.map((s) => [s.name, s.kind === 'number' ? 22.5 :
      s.kind === 'switch' ? s.name.includes('responses enabled') : 'Auto']));
    this.state.set('ODU 1 diagnostics', 'addr=1 profile=V1.5');
    this.state.set('ODU 2 diagnostics', 'addr=2 profile=V1.5');
    this.state.set('Control Mode', 'CM0');
  }
  async values(settings) {
    for (const item of settings) {
      if (item.name === 'Setup Complete') assert.equal(item.domain, 'binary_sensor');
    }
    return Object.fromEntries(settings.map((s) => [s.key, this.state.get(s.name)]));
  }
  async value(domain, name) { return this.state.get(name); }
  async setSwitch(name, value) { this.state.set(name, value); }
  async setNumber(name, value) { this.state.set(name, value); }
  async setSelect(name, value) { this.state.set(name, value); }
}

test('preparation uses valid flow selectors and fails before settings writes on an unsafe baseline', async () => {
  for (const unsafe of [false, true]) {
    const controller = new SettingsClient([...controllerSettings, ...controlRegressionControllerSettings]);
    const simulator = new SettingsClient([...simulatorSettings, ...controlRegressionSimulatorSettings]);
    for (const name of ['Setup Complete', 'OpenQuatt Enabled', 'Auxiliary heat source connected']) {
      controller.state.set(name, true);
    }
    controller.state.set('Power House run extension', unsafe);
    controller.state.set('Boiler active', false);
    controller.state.set('Water Supply Temp (Selected)', 22.5);
    controller.state.set('HP1 - Flow', 600);
    controller.state.set('HP2 - Flow', 600);
    for (const name of ['Quatt ODU simulation enabled', 'ODU responses enabled']) simulator.state.set(name, true);
    for (const name of ['ODU timeout injection enabled', 'ODU exception injection enabled',
      'ODU reboot on matching request', 'M2 UART fault injection enabled',
      'ODU 1 freeze measured frequency', 'ODU 2 freeze measured frequency']) simulator.state.set(name, false);
    let writes = 0;
    const select = controller.setSelect.bind(controller);
    controller.setSelect = async (name, option) => {
      writes++;
      if (name === 'Outdoor Unit Flow Mode') assert.equal(option, 'Local aggregate HP1/HP2');
      await select(name, option);
    };
    controller.request = bench().controller.request;
    const snapshot = { simulatorActive: { hp1: { address: 1, profile: 'V1.5' },
      hp2: { address: 2, profile: 'V1.5' } } };
    if (unsafe) {
      await assert.rejects(prepareControlRegression(controller, simulator, () => false, snapshot), /run extension/);
      assert.equal(writes, 0);
    } else {
      await prepareControlRegression(controller, simulator, () => false, snapshot);
      assert.equal(controller.state.get('Q Flow Source'), 'Outdoor unit');
      assert.equal(controller.state.get('Power House demand rise time'), 2);
    }
  }
});

test('domain snapshot restores extra gates, water fixtures and permissions without changing legacy schema-3', async () => {
  for (const scenario of ['duo', 'communications', 'communications-mono', 'input-sources']) {
    const controller = new SettingsClient([...controllerSettings, ...controlRegressionControllerSettings]);
    const simulator = new SettingsClient([...simulatorSettings, ...controlRegressionSimulatorSettings]);
    const snapshot = await snapshotSettings({ controller, simulator, scenario, firmware: 'baseline', targets: {} });
    assert.equal(snapshot.schema, 3);
    assert.equal('hp1Responses' in snapshot.simulator, scenario !== 'input-sources');
    if (scenario !== 'input-sources') {
      const truncated = structuredClone(snapshot);
      delete truncated.simulator.hp2Responses;
      assert.throws(() => validateSnapshot(truncated), /boolean/);
      await simulator.setSwitch('ODU 1 responses enabled', false);
      await simulator.setNumber('ODU 2 water-in temperature', 99);
      await controller.setSwitch('Boiler fallback on heat-pump fault', true);
    }
    await restoreSettings({ controller, simulator, snapshot, log: () => {} });
    await verifyRestoredSettings({ controller, simulator, snapshot });
    if (scenario !== 'input-sources') {
      assert.equal(await simulator.value('switch', 'ODU 1 responses enabled'), true);
      assert.equal(await simulator.value('number', 'ODU 2 water-in temperature'), 22.5);
      assert.equal(await controller.value('switch', 'Boiler fallback on heat-pump fault'), false);
    }
  }
});

test('new domain runners enforce apply, firmware restore and domain-specific stages', () => {
  for (const [parse, stage] of [[parseDuoArgs, 'start'], [parseCommunicationsArgs, 'fallback']]) {
    const targets = ['--controller', 'http://controller.local', '--simulator', 'http://simulator.local'];
    assert.throws(() => parse([...targets, '--stage', stage]), /--apply/);
    assert.throws(() => parse([...targets, '--stage', stage, '--apply']), /--device/);
    const options = parse([...targets, '--stage', stage, '--apply', '--device', 'test.local',
      '--restore-config', 'restore.yaml']);
    assert.equal(options.expectedProfile, 'control-regression-v1');
    assert.throws(() => parse([...targets, '--stage', 'reboot-reset']), /unsupported stage/);
  }
});

test('Mono communication loss and handback only suppress HP1 and reject Duo firmware', async () => {
  const fixture = bench(1);
  const result = await runCommunicationsScenarios({ ...fixture, stage: 'all', hpCount: 1 });
  assert.deepEqual(fixture.writes, [[1, false], [1, true]]);
  assert.ok(result.samples.every((s) => s.hp.length === 1));
  assert.ok(result.samples.some((s) => s.mode === 4 && s.hp[0].stop_unconfirmed_due_to_link_loss));
  assert.deepEqual(fixture.responses, [true]);
  const duo = bench();
  await assert.rejects(runCommunicationsScenarios({ ...duo, stage: 'all', hpCount: 1 }), /exactly 1/);
  assert.deepEqual(duo.writes, []);
});

test('Mono rejects early fallback and missing handback feedback, restoring HP1', async () => {
  for (const fault of ['early', 'feedback', 'ack']) {
    const fixture = bench(1);
    const request = fixture.controller.request;
    fixture.controller.request = async () => {
      const payload = await request();
      if (fault === 'early' && payload.system.control_mode === 4) {
        payload.heat_pumps[0].stop_unconfirmed_due_to_link_loss = false;
      }
      if (fault === 'feedback' && fixture.writes.some(([, enabled]) => enabled)) {
        payload.heat_pumps[0].running_confirmed = false;
      }
      return payload;
    };
    if (fault === 'ack') {
      const write = fixture.simulator.setSwitch;
      fixture.simulator.setSwitch = async (name, enabled) => {
        await write(name, enabled);
        if (!enabled) throw new Error('lost ACK');
      };
    }
    await assert.rejects(runCommunicationsScenarios({ ...fixture, stage: 'fallback', hpCount: 1 }),
      fault === 'early' ? /before causal stop timeout/ : fault === 'ack' ? /lost ACK/ : /timed out/);
    assert.deepEqual(fixture.responses, [true]);
    assert.deepEqual(fixture.writes, [[1, false], [1, true]]);
  }
});

test('Mono preparation validates topology before writes and does not require HP2 flow/profile', async () => {
  const controller = new SettingsClient([...controllerSettings, ...controlRegressionControllerSettings]);
  const simulator = new SettingsClient([...simulatorSettings, ...controlRegressionSimulatorSettings]);
  for (const name of ['Setup Complete', 'OpenQuatt Enabled', 'Auxiliary heat source connected']) controller.state.set(name, true);
  controller.state.set('Power House run extension', false);
  controller.state.set('Boiler active', false);
  controller.state.set('Water Supply Temp (Selected)', 22.5);
  controller.state.set('HP1 - Flow', 600);
  for (const name of ['Quatt ODU simulation enabled', 'ODU responses enabled']) simulator.state.set(name, true);
  for (const name of ['ODU timeout injection enabled', 'ODU exception injection enabled',
    'ODU reboot on matching request', 'M2 UART fault injection enabled',
    'ODU 1 freeze measured frequency']) simulator.state.set(name, false);
  const waterWrites = [];
  const write = simulator.setNumber.bind(simulator);
  simulator.setNumber = async (name, value) => { waterWrites.push(name); await write(name, value); };
  controller.request = bench().controller.request;
  let changed = false;
  const select = controller.setSelect.bind(controller);
  controller.setSelect = async (...args) => { changed = true; await select(...args); };
  const snapshot = { simulatorActive: { hp1: { address: 1, profile: 'V1.5' } } };
  await assert.rejects(prepareControlRegression(controller, simulator, () => false, snapshot, 1), /exactly 1/);
  assert.equal(changed, false);
  controller.request = bench(1).controller.request;
  await prepareControlRegression(controller, simulator, () => false, snapshot, 1);
  assert.deepEqual(waterWrites, ['ODU 1 water-in temperature']);
});

test('Mono runner uses distinct firmware marker and recovery domain', () => {
  const options = parseMonoCommunicationsArgs(['--controller', 'http://controller.local',
    '--simulator', 'http://simulator.local', '--stage', 'smoke']);
  assert.equal(options.expectedProfile, 'control-regression-mono-v1');
});
