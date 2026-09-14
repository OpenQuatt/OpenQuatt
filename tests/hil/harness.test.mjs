import assert from 'node:assert/strict';
import { mkdtemp, readFile, rm, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';

import {
  firmwareArtifactIdentity,
  firmwareArtifactUploadCommand,
  firmwareCommand,
  firmwareCompileCommand,
  verifyFirmwareArtifact,
} from '../../scripts/hil/firmware.mjs';
import {
  assertSnapshotScenario,
  parseArgs,
  runGuardedMutation,
  restoreFirmwareAndSettings,
  verifyDiagnostics,
  verifyRestoreArtifact,
  verifyRestoredFirmware,
} from '../../scripts/hil/run-input-sources.mjs';
import {
  HilRestClient,
  RequestGate,
  normalizeBaseUrl,
} from '../../scripts/hil/rest-client.mjs';
import {
  acquireLock,
  acquireRecoveryLock,
  controllerSettings,
  mutationLockPath,
  parseOduActiveConfiguration,
  restoreSettings,
  simulatorSettings,
  snapshotSettings,
  validateSnapshot,
  verifyRestoredSettings,
} from '../../scripts/hil/session.mjs';
import {
  heatingPower,
  v2PowerInput,
} from './scenarios/v2-performance.mjs';
import { waitNumber } from '../../scripts/hil/wait.mjs';

test('target lock excludes runs and recovery, reclaims stale owner, and releases by token', async (context) => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'openquatt-hil-lock-'));
  const previousLockRoot = process.env.OQ_HIL_LOCK_ROOT;
  process.env.OQ_HIL_LOCK_ROOT = directory;
  context.after(async () => {
    if (previousLockRoot === undefined) delete process.env.OQ_HIL_LOCK_ROOT;
    else process.env.OQ_HIL_LOCK_ROOT = previousLockRoot;
    await rm(directory, { recursive: true, force: true });
  });
  const targets = {
    controller: `http://controller-${path.basename(directory)}.local`,
    simulator: `http://simulator-${path.basename(directory)}.local`,
  };
  const first = await acquireLock(targets, '/run/first');
  await assert.rejects(
    () => acquireRecoveryLock(targets, '/run/first'),
    /another HIL run or recovery is active/,
  );
  await first.release();

  const stale = await acquireLock(targets, '/run/stale');
  await stale.markMutationStarted();
  const staleOwner = JSON.parse(await readFile(stale.ownerPath, 'utf8'));
  await writeFile(stale.ownerPath, `${JSON.stringify({ ...staleOwner, pid: 99_999_999 })}\n`);
  const reclaimed = await acquireRecoveryLock(targets, '/run/stale');
  const recoveryOwner = JSON.parse(await readFile(reclaimed.ownerPath, 'utf8'));
  assert.equal(recoveryOwner.phase, 'mutation-started');
  await assert.rejects(
    () => stale.release(),
    /owned by another run/,
  );
  await assert.rejects(
    () => acquireLock(targets, '/run/other-output-root'),
    /another HIL run or recovery is active/,
  );
  await writeFile(
    reclaimed.ownerPath,
    `${JSON.stringify({ ...recoveryOwner, pid: 99_999_999 })}\n`,
  );
  await assert.rejects(
    () => acquireLock(targets, '/run/other-output-root'),
    /unfinished HIL run requires recovery first/,
  );
  await reclaimed.release();
});

test('a stale pre-mutation lock can be safely abandoned by the next normal run', async (context) => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'openquatt-hil-premutation-lock-'));
  const previousLockRoot = process.env.OQ_HIL_LOCK_ROOT;
  process.env.OQ_HIL_LOCK_ROOT = directory;
  context.after(async () => {
    if (previousLockRoot === undefined) delete process.env.OQ_HIL_LOCK_ROOT;
    else process.env.OQ_HIL_LOCK_ROOT = previousLockRoot;
    await rm(directory, { recursive: true, force: true });
  });
  const targets = {
    controller: 'http://controller.local',
    simulator: 'http://simulator.local',
  };
  const abandoned = await acquireLock(targets, '/run/compile-crashed');
  const owner = JSON.parse(await readFile(abandoned.ownerPath, 'utf8'));
  assert.equal(owner.phase, 'pre-mutation');
  await writeFile(abandoned.ownerPath, `${JSON.stringify({ ...owner, pid: 99_999_999 })}\n`);

  const replacement = await acquireLock(targets, '/run/next');
  await assert.rejects(() => abandoned.release(), /owned by another run/);
  await replacement.release();
});

test('HCQ lab aliases resolve to one global mutation lock', () => {
  assert.equal(
    mutationLockPath({ controller: 'http://openquatt-test.local', simulator: 'http://sim.local' }),
    mutationLockPath({ controller: 'http://192.168.2.86', simulator: 'http://192.168.2.63' }),
  );
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

test('numeric waits accept zero as a valid matched sample', async () => {
  const controller = {
    value: async () => 0,
  };
  assert.equal(
    await waitNumber(controller, 'zero', (value) => value === 0, 'zero sample', {
      timeoutMs: 20,
      intervalMs: 1,
    }),
    0,
  );
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
  assert.throws(() => validateSnapshot({ schema: 3 }), /firmware identity/);
  assert.equal(
    verifyRestoredFirmware(
      '2026.8.2 (config hash 0x12345678)',
      '2026.8.2 (config hash 0x12345678)',
    ),
    '2026.8.2 (config hash 0x12345678)',
  );
  assert.throws(
    () => verifyRestoredFirmware(
      '2026.8.2 (config hash 0x87654321)',
      '2026.8.2 (config hash 0x12345678)',
    ),
    /restored firmware differs/,
  );
});

test('restore artifact must be precompiled with the exact baseline config hash', () => {
  assert.equal(
    verifyRestoreArtifact('2026.8.2 (config hash 0x12345678)', '0x12345678'),
    '0x12345678',
  );
  assert.throws(
    () => verifyRestoreArtifact('2026.8.2 (config hash 0x12345678)', '0x87654321'),
    /restore artifact differs/,
  );
});

test('restore artifact integrity binds exact bytes and size', async (context) => {
  const directory = await mkdtemp(path.join(os.tmpdir(), 'openquatt-hil-artifact-'));
  context.after(() => rm(directory, { recursive: true, force: true }));
  const artifact = path.join(directory, 'restore-firmware.ota.bin');
  await writeFile(artifact, Buffer.from('known firmware bytes'));
  const identity = await firmwareArtifactIdentity(artifact);
  await assert.doesNotReject(() => verifyFirmwareArtifact(artifact, identity));
  await writeFile(artifact, Buffer.from('changed firmware bytes'));
  await assert.rejects(() => verifyFirmwareArtifact(artifact, identity), /integrity differs/);
});

test('snapshot recovery rejects a different scenario runner before mutation', () => {
  const snapshot = { scenario: 'issue-667-v2-performance' };
  assert.doesNotThrow(() => assertSnapshotScenario(snapshot, { name: snapshot.scenario }));
  assert.throws(
    () => assertSnapshotScenario(snapshot, { name: 'input-sources' }),
    /snapshot scenario differs/,
  );
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
  assert.equal(parsed.expectedSimulatorContract, 'openquatt-modbus-opentherm-v2');
  assert.equal(
    parseArgs([...targets, '--expected-simulator-contract', 'next-contract']).expectedSimulatorContract,
    'next-contract',
  );
  assert.throws(
    () => parseArgs([...targets, '--stage', 'all', '--apply', '--settings-only']),
    /--device and --restore-config|--settings-only/,
  );
});

test('issue 667 golden vectors keep auxiliary loads and defrost distinct', () => {
  const fixture = { voltageV: 230, currentA: 1.3, fanSpeed: 200, pumpPowerW: 30 };
  const base = v2PowerInput(fixture);
  assert.ok(Math.abs(base - 309.97883) < 0.0001);
  assert.ok(Math.abs(v2PowerInput({ ...fixture, status2108: 0x0004 }) - (base + 140)) < 0.0001);
  assert.ok(Math.abs(v2PowerInput({ ...fixture, status2108: 0x0008 }) - (base + 33.62)) < 0.0001);
  assert.ok(Math.abs(v2PowerInput({ ...fixture, status2108: 0x0800 }) - (base + 30)) < 0.0001);
  assert.equal(v2PowerInput({ ...fixture, status2108: 0x0010 }), base);
});

test('issue 667 field heat-power fixture uses production water constant', () => {
  assert.ok(Math.abs(heatingPower({ flowLph: 1020, inletC: 22.5, outletC: 24.53 }) - 2407.6477) < 0.001);
});

test('diagnostics reject an absent or incompatible simulator contract', () => {
  const result = {
    simulator: { contract: 'openquatt-modbus-opentherm-v1', version: 'v0.1.0' },
    memory: { heapMinFree: 40000, largestBlock: 50000 },
    hp1: 'exc=0 bad_addr=0 bad_write=0 cap=0',
    hp2: 'exc=0 bad_addr=0 bad_write=0 cap=0',
  };
  const options = {
    expectedSimulatorContract: 'openquatt-modbus-opentherm-v1',
    minHeapMinFree: 0,
    minLargestBlock: 0,
  };
  assert.doesNotThrow(() => verifyDiagnostics(result, options));
  assert.throws(
    () => verifyDiagnostics({ ...result, simulator: { contract: null, version: null } }, options),
    /simulator contract differs/,
  );
  assert.throws(
    () => verifyDiagnostics(result, { ...options, expectedSimulatorContract: 'next-contract' }),
    /simulator contract differs/,
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
  assert.deepEqual(firmwareCompileCommand({ config: 'production.yaml', esphome: '/bin/esphome' }), {
    executable: '/bin/esphome',
    args: ['compile', 'production.yaml'],
  });
  assert.deepEqual(
    firmwareArtifactUploadCommand({
      config: 'production.yaml',
      device: 'controller.local',
      artifact: '/run/restore-firmware.ota.bin',
      esphome: '/bin/esphome',
    }),
    {
      executable: '/bin/esphome',
      args: [
        'upload',
        '--device',
        'controller.local',
        '--ota-platform',
        'esphome',
        '--file',
        '/run/restore-firmware.ota.bin',
        'production.yaml',
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
          ? false
          : `option-${index}`,
    ]));
    this.state.set('text_sensor:ODU 1 diagnostics', 'addr=1 profile=V1.5 exc=0 bad_addr=0 bad_write=0 cap=0');
    this.state.set('text_sensor:ODU 2 diagnostics', 'addr=2 profile=V2 old model exc=0 bad_addr=0 bad_write=0 cap=0');
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

test('ODU active configuration parser rejects incomplete or invalid diagnostics', () => {
  assert.deepEqual(
    parseOduActiveConfiguration('addr=2 profile=V2 new model exc=0', 'HP2'),
    { address: 2, profile: 'V2 new model' },
  );
  assert.throws(() => parseOduActiveConfiguration('addr=0 profile=V1', 'HP1'), /no valid active/);
  assert.throws(() => parseOduActiveConfiguration('addr=1 exc=0', 'HP1'), /no valid active/);
});

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
    scenario: 'input-sources',
  });
  assert.deepEqual(snapshot.simulatorActive, {
    hp1: { address: 1, profile: 'V1.5' },
    hp2: { address: 2, profile: 'V2 old model' },
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
  await verifyRestoredSettings({ controller, simulator, snapshot });
  controller.state.set('select:CM Override', 'changed after persistence window');
  await assert.rejects(
    () => verifyRestoredSettings({ controller, simulator, snapshot }),
    /did not persist/,
  );
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
    scenario: 'input-sources',
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

test('failed safe-CM0 restore blocks firmware OTA fail-closed', async () => {
  let flashCalls = 0;
  await assert.rejects(
    () => restoreFirmwareAndSettings({
      options: {
        settingsOnly: false,
        restoreConfig: 'production.yaml',
        device: 'controller.local',
      },
      controller: {},
      simulator: {},
      snapshot: { firmware: 'baseline' },
      interrupted: () => false,
      restoreSettingsImpl: async () => {
        throw new Error('injected CM0 confirmation failure');
      },
      flashFirmwareArtifactImpl: async () => {
        flashCalls += 1;
      },
    }),
    /firmware OTA blocked/,
  );
  assert.equal(flashCalls, 0);
});

test('failed post-settle CM0 confirmation blocks firmware OTA fail-closed', async () => {
  let flashCalls = 0;
  await assert.rejects(
    () => restoreFirmwareAndSettings({
      options: {
        settingsOnly: false,
        restoreConfig: 'production.yaml',
        restoreArtifactPath: 'restore.ota.bin',
        device: 'controller.local',
      },
      controller: {},
      simulator: {},
      snapshot: { firmware: 'baseline' },
      interrupted: () => false,
      restoreSettingsImpl: async () => {},
      waitForSafeCm0PersistedImpl: async () => {
        throw new Error('injected post-settle CM0 confirmation failure');
      },
      flashFirmwareArtifactImpl: async () => {
        flashCalls += 1;
      },
    }),
    /safe CM0 persistence failed; firmware OTA blocked/,
  );
  assert.equal(flashCalls, 0);
});

test('an interrupt raised during preparation prevents the first device mutation', async () => {
  let armed = false;
  let flashCalls = 0;
  await assert.rejects(
    () => runGuardedMutation({
      interrupted: () => true,
      arm: async () => {
        armed = true;
      },
      mutate: async () => {
        flashCalls += 1;
      },
    }),
    /interrupted before device mutation/,
  );
  assert.equal(armed, false);
  assert.equal(flashCalls, 0);
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
    scenario: 'input-sources',
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

test('a transient optional pre-restore read does not invalidate a verified restore', async () => {
  class TransientReadClient extends FakeClient {
    constructor(settings) {
      super(settings);
      this.failNextRead = false;
    }

    async values(settings) {
      if (this.failNextRead) {
        this.failNextRead = false;
        throw new Error('injected reboot read gap');
      }
      return super.values(settings);
    }
  }

  const controller = new FakeClient(controllerSettings);
  const simulator = new TransientReadClient(simulatorSettings);
  const snapshot = await snapshotSettings({
    controller,
    simulator,
    targets: { controller: 'http://controller.local', simulator: 'http://simulator.local' },
    firmware: '2026.8.2 (config hash 0x12345678)',
    scenario: 'input-sources',
  });
  simulator.failNextRead = true;
  await restoreSettings({ controller, simulator, snapshot, log: () => {} });
  await verifyRestoredSettings({ controller, simulator, snapshot });
});
