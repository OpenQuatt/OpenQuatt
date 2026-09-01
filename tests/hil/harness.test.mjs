import assert from 'node:assert/strict';
import { mkdtemp, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import { firmwareCommand } from '../../scripts/hil/firmware.mjs';
import { parseArgs } from '../../scripts/hil/run-input-sources.mjs';
import {
  HilRestClient,
  RequestGate,
  normalizeBaseUrl,
} from '../../scripts/hil/rest-client.mjs';
import {
  acquireRecoveryLock,
  controllerSettings,
  restoreSettings,
  simulatorSettings,
  snapshotSettings,
  validateSnapshot,
} from '../../scripts/hil/session.mjs';

test('recovery lock blocks concurrent recovery and reclaims a dead owner', async (context) => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'openquatt-hil-lock-'));
  context.after(() => rm(directory, { recursive: true, force: true }));
  const first = await acquireRecoveryLock(directory, '/run/first');
  await assert.rejects(
    () => acquireRecoveryLock(directory, '/run/second'),
    /another HIL recovery is active/,
  );
  await first.release();

  await writeFile(
    path.join(directory, 'input-sources-recovery.lock'),
    `${JSON.stringify({ pid: 99_999_999, runDir: '/run/stale' })}\n`,
  );
  const reclaimed = await acquireRecoveryLock(directory, '/run/reclaimed');
  await reclaimed.release();
});

test('REST client is read-only unless writes are explicitly enabled', async () => {
  let called = false;
  const client = new HilRestClient({
    baseUrl: 'http://controller.local',
    fetchImpl: async () => {
      called = true;
      return new Response('', { status: 200 });
    },
  });
  await assert.rejects(() => client.setSwitch('test', true), /--apply/);
  assert.equal(called, false);
});

test('one shared request gate enforces the global write interval', async () => {
  let now = 0;
  const waits = [];
  const gate = new RequestGate({
    now: () => now,
    sleep: async (milliseconds) => {
      waits.push(milliseconds);
      now += milliseconds;
    },
    writeIntervalMs: 1500,
  });
  const fetchImpl = async () => new Response('', { status: 200 });
  const controller = new HilRestClient({
    baseUrl: 'http://controller.local',
    allowWrites: true,
    fetchImpl,
    gate,
  });
  const simulator = new HilRestClient({
    baseUrl: 'http://simulator.local',
    allowWrites: true,
    fetchImpl,
    gate,
  });
  await controller.setSwitch('first', true);
  await simulator.setSwitch('second', false);
  assert.deepEqual(waits, [1500]);
  assert.deepEqual(gate.counts, { read: 0, write: 2 });
});

test('one shared request gate prevents concurrent controller and simulator calls', async () => {
  let releaseFirst;
  const firstBlocked = new Promise((resolve) => {
    releaseFirst = resolve;
  });
  let active = 0;
  let maximumActive = 0;
  let calls = 0;
  const fetchImpl = async () => {
    calls += 1;
    active += 1;
    maximumActive = Math.max(maximumActive, active);
    if (calls === 1) await firstBlocked;
    active -= 1;
    return new Response(JSON.stringify({ value: calls }), { status: 200 });
  };
  const gate = new RequestGate({ readIntervalMs: 0 });
  const controller = new HilRestClient({
    baseUrl: 'http://controller.local',
    fetchImpl,
    gate,
  });
  const simulator = new HilRestClient({
    baseUrl: 'http://simulator.local',
    fetchImpl,
    gate,
  });
  const first = controller.value('sensor', 'first');
  const second = simulator.value('sensor', 'second');
  await new Promise((resolve) => setTimeout(resolve, 0));
  assert.equal(calls, 1);
  releaseFirst();
  await Promise.all([first, second]);
  assert.equal(maximumActive, 1);
});

test('controller bulk reads use one read-only POST without enabling writes', async () => {
  const gate = new RequestGate();
  let receivedBody = '';
  const client = new HilRestClient({
    baseUrl: 'http://controller.local',
    bulkReads: true,
    gate,
    fetchImpl: async (_url, options) => {
      receivedBody = options.body;
      return new Response(
        JSON.stringify({
          entities: {
            heap: { value: 42000 },
            profile: { value: 'input-sources-fast-v1' },
          },
          missing: [],
        }),
        { status: 200 },
      );
    },
  });
  const values = await client.values([
    { key: 'heap', domain: 'sensor', name: 'Heap Min Free' },
    { key: 'profile', domain: 'text_sensor', name: 'HIL Test Profile' },
  ]);
  assert.deepEqual(values, { heap: 42000, profile: 'input-sources-fast-v1' });
  assert.match(receivedBody, /entities=heap%09sensor%09Heap\+Min\+Free/);
  assert.deepEqual(gate.counts, { read: 1, write: 0 });
});

test('target URLs reject credentials and non-HTTP protocols', () => {
  assert.equal(normalizeBaseUrl('http://controller.local/'), 'http://controller.local');
  assert.throws(() => normalizeBaseUrl('ftp://controller.local'), /http or https/);
  assert.throws(() => normalizeBaseUrl('http://user:secret@controller.local'), /credentials/);
});

test('snapshots require the exact pre-test firmware identity', () => {
  assert.throws(() => validateSnapshot({ schema: 1 }), /firmware identity/);
});

test('mutating CLI modes require apply plus an automatic firmware restore', () => {
  const targets = ['--controller', 'http://controller.local', '--simulator', 'http://simulator.local'];
  assert.equal(parseArgs(targets).stage, 'smoke');
  assert.throws(() => parseArgs([...targets, '--stage', 'all']), /--apply/);
  assert.throws(
    () => parseArgs([...targets, '--stage', 'all', '--apply']),
    /--device and --restore-config/,
  );
  const parsed = parseArgs([
    ...targets,
    '--stage',
    'all',
    '--apply',
    '--device',
    'controller.local',
    '--test-config',
    'test.yaml',
    '--restore-config',
    'production.yaml',
  ]);
  assert.equal(parsed.apply, true);
  assert.equal(parsed.writeIntervalMs, 1500);
  assert.throws(
    () => parseArgs([...targets, '--stage', 'all', '--apply', '--settings-only']),
    /--device and --restore-config|--settings-only/,
  );
});

test('firmware upload uses argument arrays without a shell', () => {
  assert.deepEqual(
    firmwareCommand({ config: 'test profile.yaml', device: 'controller.local', esphome: '/bin/esphome' }),
    {
      executable: '/bin/esphome',
      args: [
        'run',
        '--device',
        'controller.local',
        '--ota-platform',
        'esphome',
        '--no-logs',
        'test profile.yaml',
      ],
    },
  );
});

class FakeClient {
  constructor(settings) {
    this.state = new Map(settings.map((setting, index) => [
      `${setting.domain}:${setting.name}`,
      setting.kind === 'number'
        ? index + 0.5
        : setting.kind === 'switch'
          ? index % 2 === 0
          : `option-${index}`,
    ]));
  }

  async value(domain, name) {
    return this.state.get(`${domain}:${name}`);
  }

  async values(settings) {
    return Object.fromEntries(
      settings.map((setting) => [setting.key, this.state.get(`${setting.domain}:${setting.name}`)]),
    );
  }

  async setNumber(name, value) {
    this.state.set(`number:${name}`, Number(value));
  }

  async setSelect(name, value) {
    this.state.set(`select:${name}`, value);
    if (name === 'CM Override' && value === 'Force CM0') {
      this.state.set('text_sensor:Control Mode', 'CM0');
    }
  }

  async setSwitch(name, value) {
    this.state.set(`switch:${name}`, Boolean(value));
  }
}

test('snapshot restore reinstates every captured setting after a failed scenario', async () => {
  const controller = new FakeClient(controllerSettings);
  const simulator = new FakeClient(simulatorSettings);
  const targets = {
    controller: 'http://controller.local',
    simulator: 'http://simulator.local',
  };
  const snapshot = await snapshotSettings({
    controller,
    simulator,
    targets,
    firmware: '2026.8.2 (config hash 0x12345678)',
  });
  for (const setting of controllerSettings) {
    controller.state.set(`${setting.domain}:${setting.name}`, setting.kind === 'number' ? 999 : 'changed');
  }
  for (const setting of simulatorSettings) {
    simulator.state.set(`${setting.domain}:${setting.name}`, true);
  }
  await restoreSettings({ controller, simulator, snapshot, log: () => {} });
  for (const setting of controllerSettings) {
    assert.equal(
      await controller.value(setting.domain, setting.name),
      snapshot.controller[setting.key],
      setting.name,
    );
  }
  for (const setting of simulatorSettings) {
    assert.equal(
      await simulator.value(setting.domain, setting.name),
      snapshot.simulator[setting.key],
      setting.name,
    );
  }
});

test('pre-OTA restore keeps CM0 until normal firmware is confirmed', async () => {
  const controller = new FakeClient(controllerSettings);
  const simulator = new FakeClient(simulatorSettings);
  const snapshot = await snapshotSettings({
    controller,
    simulator,
    targets: {
      controller: 'http://controller.local',
      simulator: 'http://simulator.local',
    },
    firmware: '2026.8.2 (config hash 0x12345678)',
  });
  controller.state.set('select:Room Temperature Source', 'changed');
  await restoreSettings({
    controller,
    simulator,
    snapshot,
    restoreCmOverride: false,
    log: () => {},
  });
  assert.equal(await controller.value('select', 'CM Override'), 'Force CM0');
  assert.equal(
    await controller.value('select', 'Room Temperature Source'),
    snapshot.controller.roomSource,
  );
});

test('restore failures remain blocking while other settings are still attempted', async () => {
  class FailingClient extends FakeClient {
    async setNumber(name, value) {
      if (name === 'Power House temperature reaction') throw new Error('injected write failure');
      await super.setNumber(name, value);
    }
  }

  const controller = new FailingClient(controllerSettings);
  const simulator = new FakeClient(simulatorSettings);
  const snapshot = await snapshotSettings({
    controller,
    simulator,
    targets: {
      controller: 'http://controller.local',
      simulator: 'http://simulator.local',
    },
    firmware: '2026.8.2 (config hash 0x12345678)',
  });
  controller.state.set('number:Power House temperature reaction', 999);
  controller.state.set('select:Room Temperature Source', 'changed');
  await assert.rejects(
    () => restoreSettings({ controller, simulator, snapshot, log: () => {} }),
    /one or more HIL settings could not be restored/,
  );
  assert.equal(
    await controller.value('select', 'Room Temperature Source'),
    snapshot.controller.roomSource,
  );
  assert.equal(await controller.value('select', 'CM Override'), 'Force CM0');
});
