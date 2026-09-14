import { randomUUID } from 'node:crypto';
import { mkdir, readFile, rename, rmdir, unlink, writeFile } from 'node:fs/promises';
import os from 'node:os';
import path from 'node:path';

import { asBoolean, asFiniteNumber } from './rest-client.mjs';
import { waitValue } from './wait.mjs';

export const SNAPSHOT_SCHEMA = 3;

export const controllerSettings = [
  { key: 'cmOverride', domain: 'select', name: 'CM Override', kind: 'select' },
  { key: 'cicPolling', domain: 'switch', name: 'CIC - Enable polling', kind: 'switch' },
  { key: 'waterSupplySource', domain: 'select', name: 'Water Supply Source', kind: 'select' },
  { key: 'outsideSource', domain: 'select', name: 'Outside Temperature Source', kind: 'select' },
  { key: 'roomSource', domain: 'select', name: 'Room Temperature Source', kind: 'select' },
  { key: 'setpointSource', domain: 'select', name: 'Room Setpoint Source', kind: 'select' },
  { key: 'externalSource', domain: 'select', name: 'External Heat Demand Source', kind: 'select' },
  { key: 'heatingEnableSource', domain: 'select', name: 'Heating Enable Source', kind: 'select' },
  { key: 'coolingEnableSource', domain: 'select', name: 'Cooling Enable Source', kind: 'select' },
  { key: 'coolingDewPointSource', domain: 'select', name: 'Cooling Dew Point Source', kind: 'select' },
  { key: 'manualCooling', domain: 'switch', name: 'Manual Cooling Enable', kind: 'switch' },
  { key: 'heatingMode', domain: 'select', name: 'Heating Control Mode', kind: 'select' },
  { key: 'phReaction', domain: 'number', name: 'Power House temperature reaction', kind: 'number' },
  { key: 'phRise', domain: 'number', name: 'Power House demand rise time', kind: 'number' },
  { key: 'flowSource', domain: 'select', name: 'Flow Source', kind: 'select' },
  { key: 'outdoorFlowMode', domain: 'select', name: 'Outdoor Unit Flow Mode', kind: 'select' },
  { key: 'flowMode', domain: 'select', name: 'Flow Control Mode', kind: 'select' },
  { key: 'flowSetpoint', domain: 'number', name: 'Flow Setpoint', kind: 'number' },
];

export const simulatorSettings = [
  { key: 'hp1Profile', domain: 'select', name: 'ODU 1 profile (pending)', kind: 'select' },
  { key: 'hp2Profile', domain: 'select', name: 'ODU 2 profile (pending)', kind: 'select' },
  { key: 'hp1Address', domain: 'number', name: 'ODU 1 Modbus address (pending)', kind: 'number' },
  { key: 'hp2Address', domain: 'number', name: 'ODU 2 Modbus address (pending)', kind: 'number' },
  { key: 'externalFlow', domain: 'switch', name: 'ODU external system pump flow', kind: 'switch' },
  { key: 'hp1NoFlow', domain: 'switch', name: 'ODU 1 force no flow', kind: 'switch' },
  { key: 'hp2NoFlow', domain: 'switch', name: 'ODU 2 force no flow', kind: 'switch' },
  { key: 'hp1Defrost', domain: 'switch', name: 'ODU 1 defrost', kind: 'switch' },
  { key: 'hp2Defrost', domain: 'switch', name: 'ODU 2 defrost', kind: 'switch' },
  { key: 'hp1ManualTelemetry', domain: 'switch', name: 'ODU 1 manual telemetry override', kind: 'switch' },
  { key: 'hp2ManualTelemetry', domain: 'switch', name: 'ODU 2 manual telemetry override', kind: 'switch' },
  { key: 'hp1ManualWorkingMode', domain: 'number', name: 'ODU 1 manual working mode 2099', kind: 'number' },
  { key: 'hp2ManualWorkingMode', domain: 'number', name: 'ODU 2 manual working mode 2099', kind: 'number' },
  { key: 'hp1ManualVoltage', domain: 'number', name: 'ODU 1 manual AC voltage', kind: 'number' },
  { key: 'hp2ManualVoltage', domain: 'number', name: 'ODU 2 manual AC voltage', kind: 'number' },
  { key: 'hp1ManualCurrent', domain: 'number', name: 'ODU 1 manual AC current', kind: 'number' },
  { key: 'hp2ManualCurrent', domain: 'number', name: 'ODU 2 manual AC current', kind: 'number' },
  { key: 'hp1ManualFan', domain: 'number', name: 'ODU 1 manual fan speed', kind: 'number' },
  { key: 'hp2ManualFan', domain: 'number', name: 'ODU 2 manual fan speed', kind: 'number' },
  { key: 'hp1ManualStatus', domain: 'number', name: 'ODU 1 manual operating status 2108', kind: 'number' },
  { key: 'hp2ManualStatus', domain: 'number', name: 'ODU 2 manual operating status 2108', kind: 'number' },
  { key: 'hp1ManualPumpFeedback', domain: 'number', name: 'ODU 1 manual pump feedback 2137', kind: 'number' },
  { key: 'hp2ManualPumpFeedback', domain: 'number', name: 'ODU 2 manual pump feedback 2137', kind: 'number' },
  { key: 'hp1ManualWaterIn', domain: 'number', name: 'ODU 1 manual water-in temperature', kind: 'number' },
  { key: 'hp2ManualWaterIn', domain: 'number', name: 'ODU 2 manual water-in temperature', kind: 'number' },
  { key: 'hp1ManualWaterOut', domain: 'number', name: 'ODU 1 manual water-out temperature', kind: 'number' },
  { key: 'hp2ManualWaterOut', domain: 'number', name: 'ODU 2 manual water-out temperature', kind: 'number' },
  { key: 'hp1ManualFlow', domain: 'number', name: 'ODU 1 manual flow', kind: 'number' },
  { key: 'hp2ManualFlow', domain: 'number', name: 'ODU 2 manual flow', kind: 'number' },
];

export function parseOduActiveConfiguration(value, label = 'ODU diagnostics') {
  const text = String(value ?? '');
  const addressMatch = text.match(/(?:^|\s)addr=(\d+)(?:\s|$)/);
  const profileMatch = text.match(
    /(?:^|\s)profile=(V2 old model|V2 new model|V1\.5|V1|Disabled)(?:\s|$)/,
  );
  const address = addressMatch ? Number(addressMatch[1]) : null;
  if (!Number.isSafeInteger(address) || address < 1 || address > 247 || !profileMatch) {
    throw new Error(`${label} has no valid active address/profile: ${JSON.stringify(value)}`);
  }
  return { address, profile: profileMatch[1] };
}

function normalizeSettingValue(setting, value) {
  if (setting.kind === 'number') {
    const number = asFiniteNumber(value);
    if (number === null) throw new Error(`${setting.name} is unavailable or non-finite`);
    return number;
  }
  if (setting.kind === 'switch') return asBoolean(value);
  if (typeof value !== 'string' || value.length === 0) {
    throw new Error(`${setting.name} has no selected option`);
  }
  return value;
}

async function readSettings(client, settings) {
  const values = await client.values(settings);
  const result = {};
  for (const setting of settings) {
    result[setting.key] = normalizeSettingValue(setting, values[setting.key]);
  }
  return result;
}

export async function snapshotSettings({ controller, simulator, targets, firmware, scenario }) {
  if (typeof firmware !== 'string' || firmware.length === 0) {
    throw new Error('firmware identity is required for a HIL snapshot');
  }
  if (typeof scenario !== 'string' || scenario.length === 0) {
    throw new Error('scenario identity is required for a HIL snapshot');
  }
  const simulatorState = await readSettings(simulator, simulatorSettings);
  for (const key of [
    'hp1NoFlow',
    'hp2NoFlow',
    'hp1Defrost',
    'hp2Defrost',
    'hp1ManualTelemetry',
    'hp2ManualTelemetry',
  ]) {
    if (simulatorState[key]) {
      throw new Error(`unsafe simulator baseline: ${key} must be off before a HIL run`);
    }
  }
  const activeDiagnostics = await simulator.values([
    { key: 'hp1', domain: 'text_sensor', name: 'ODU 1 diagnostics' },
    { key: 'hp2', domain: 'text_sensor', name: 'ODU 2 diagnostics' },
  ]);
  return {
    schema: SNAPSHOT_SCHEMA,
    capturedAt: new Date().toISOString(),
    targets,
    firmware,
    scenario,
    controller: await readSettings(controller, controllerSettings),
    simulator: simulatorState,
    simulatorActive: {
      hp1: parseOduActiveConfiguration(activeDiagnostics.hp1, 'ODU 1 diagnostics'),
      hp2: parseOduActiveConfiguration(activeDiagnostics.hp2, 'ODU 2 diagnostics'),
    },
  };
}

async function applySetting(client, setting, value) {
  if (setting.kind === 'number') return client.setNumber(setting.name, value);
  if (setting.kind === 'switch') return client.setSwitch(setting.name, value);
  return client.setSelect(setting.name, value);
}

function valuesMatch(setting, actual, expected) {
  try {
    const normalized = normalizeSettingValue(setting, actual);
    if (setting.kind === 'number') return Math.abs(normalized - expected) < 0.011;
    return normalized === expected;
  } catch {
    return false;
  }
}

export function validateSnapshot(snapshot) {
  if (!snapshot || snapshot.schema !== SNAPSHOT_SCHEMA) {
    throw new Error(`unsupported HIL snapshot schema: ${snapshot?.schema ?? 'missing'}`);
  }
  if (typeof snapshot.firmware !== 'string' || snapshot.firmware.length === 0) {
    throw new Error('HIL snapshot has no firmware identity');
  }
  if (typeof snapshot.scenario !== 'string' || snapshot.scenario.length === 0) {
    throw new Error('HIL snapshot has no scenario identity');
  }
  for (const setting of controllerSettings) {
    normalizeSettingValue(setting, snapshot.controller?.[setting.key]);
  }
  for (const setting of simulatorSettings) {
    normalizeSettingValue(setting, snapshot.simulator?.[setting.key]);
  }
  for (const key of ['hp1', 'hp2']) {
    const active = snapshot.simulatorActive?.[key];
    if (!Number.isSafeInteger(active?.address) || active.address < 1 || active.address > 247) {
      throw new Error(`HIL snapshot has no valid active ${key} Modbus address`);
    }
    if (typeof active.profile !== 'string' || active.profile.length === 0) {
      throw new Error(`HIL snapshot has no valid active ${key} profile`);
    }
  }
  return snapshot;
}

export async function restoreSettings({
  controller,
  simulator,
  snapshot,
  restoreCmOverride = true,
  log = console.log,
}) {
  validateSnapshot(snapshot);
  const errors = [];
  const attempt = async (label, operation) => {
    try {
      await operation();
    } catch (error) {
      errors.push(new Error(`${label}: ${error.message}`));
    }
  };

  await attempt('force safe CM0', () => controller.setSelect('CM Override', 'Force CM0'));
  await attempt('disable API heating input', () =>
    controller.setSwitch('api_input_heating_enable', false),
  );
  await attempt('disable API cooling input', () =>
    controller.setSwitch('api_input_cooling_enable', false),
  );
  await attempt('clear HP1 no-flow injection', () =>
    simulator.setSwitch('ODU 1 force no flow', false),
  );
  await attempt('clear HP2 no-flow injection', () =>
    simulator.setSwitch('ODU 2 force no flow', false),
  );
  await attempt('clear HP1 defrost injection', () =>
    simulator.setSwitch('ODU 1 defrost', false),
  );
  await attempt('clear HP2 defrost injection', () =>
    simulator.setSwitch('ODU 2 defrost', false),
  );
  await attempt('disable HP1 manual telemetry override', () =>
    simulator.setSwitch('ODU 1 manual telemetry override', false),
  );
  await attempt('disable HP2 manual telemetry override', () =>
    simulator.setSwitch('ODU 2 manual telemetry override', false),
  );
  await attempt('wait for safe CM0', () =>
    waitValue(controller, 'text_sensor', 'Control Mode', 'CM0', 'safe CM0 before restore', {
      timeoutMs: 50000,
    }),
  );

  const restoreGroup = async (client, settings, expectedValues, label) => {
    let currentValues;
    try {
      currentValues = await client.values(settings);
    } catch (error) {
      // This read only avoids redundant writes. A rebooting simulator can be
      // temporarily unavailable here; the mandatory post-write read below is
      // the authoritative recovery check.
      log(`RETRY ${label} after optional pre-restore read failed: ${error.message}`);
    }
    for (const setting of settings) {
      const expected = expectedValues[setting.key];
      if (currentValues && valuesMatch(setting, currentValues[setting.key], expected)) continue;
      await attempt(`restore ${setting.name}`, () => applySetting(client, setting, expected));
    }
    let verifiedValues;
    await attempt(`read ${label} after restore`, async () => {
      verifiedValues = await client.values(settings);
    });
    if (!verifiedValues) return;
    for (const setting of settings) {
      const expected = expectedValues[setting.key];
      if (!valuesMatch(setting, verifiedValues[setting.key], expected)) {
        errors.push(
          new Error(
            `${setting.name} restore verification failed: expected ${JSON.stringify(expected)}, ` +
              `received ${JSON.stringify(verifiedValues[setting.key])}`,
          ),
        );
        continue;
      }
      log(`RESTORED ${setting.name}=${JSON.stringify(expected)}`);
    }
  };

  await restoreGroup(
    controller,
    controllerSettings.filter((item) => item.key !== 'cmOverride'),
    snapshot.controller,
    'controller settings',
  );
  await restoreGroup(simulator, simulatorSettings, snapshot.simulator, 'simulator settings');
  if (restoreCmOverride && errors.length === 0) {
    const cmOverride = controllerSettings.find((item) => item.key === 'cmOverride');
    await restoreGroup(
      controller,
      [cmOverride],
      snapshot.controller,
      'CM Override',
    );
  } else if (restoreCmOverride) {
    errors.push(new Error('CM Override remains Force CM0 because earlier restore steps failed'));
  }

  if (errors.length > 0) throw new AggregateError(errors, 'one or more HIL settings could not be restored');
}

export async function verifyRestoredSettings({ controller, simulator, snapshot }) {
  validateSnapshot(snapshot);
  const errors = [];
  const verifyGroup = async (client, settings, expectedValues, label) => {
    let values;
    try {
      values = await client.values(settings);
    } catch (error) {
      errors.push(new Error(`read ${label}: ${error.message}`));
      return;
    }
    for (const setting of settings) {
      if (!valuesMatch(setting, values[setting.key], expectedValues[setting.key])) {
        errors.push(new Error(
          `${setting.name} persistence verification failed: expected ` +
            `${JSON.stringify(expectedValues[setting.key])}, received ` +
            `${JSON.stringify(values[setting.key])}`,
        ));
      }
    }
  };
  await verifyGroup(controller, controllerSettings, snapshot.controller, 'controller settings');
  await verifyGroup(simulator, simulatorSettings, snapshot.simulator, 'simulator settings');
  if (errors.length > 0) {
    throw new AggregateError(errors, 'one or more HIL settings did not persist');
  }
}

export async function writeJsonAtomic(filePath, value) {
  await mkdir(path.dirname(filePath), { recursive: true });
  const temporaryPath = `${filePath}.tmp`;
  await writeFile(temporaryPath, `${JSON.stringify(value, null, 2)}\n`, { mode: 0o600 });
  await rename(temporaryPath, filePath);
}

export async function readSnapshot(filePath) {
  return validateSnapshot(JSON.parse(await readFile(filePath, 'utf8')));
}

export function runDirectory(rootDirectory, now = new Date(), scenario = 'input-sources') {
  const timestamp = now.toISOString().replace(/[:.]/g, '-');
  return path.join(rootDirectory, `${timestamp}-${scenario}`);
}

function processIsAlive(pid) {
  try {
    process.kill(pid, 0);
    return true;
  } catch (error) {
    if (error.code === 'EPERM') return true;
    if (error.code === 'ESRCH') return false;
    throw error;
  }
}

export function mutationLockRoot() {
  if (process.env.OQ_HIL_LOCK_ROOT) return path.resolve(process.env.OQ_HIL_LOCK_ROOT);
  return process.platform === 'darwin'
    ? path.join(os.homedir(), 'Library', 'Application Support', 'OpenQuatt', 'HIL', 'locks')
    : path.join(os.homedir(), '.local', 'state', 'openquatt', 'hil-locks');
}

export function mutationLockPath(targets) {
  if (typeof targets?.controller !== 'string' || typeof targets?.simulator !== 'string') {
    throw new Error('controller and simulator targets are required for the HIL mutation lock');
  }
  // This harness controls one physical HCQ desktop lab. Hostname and IP aliases
  // can address the same devices, so URL-based locks are not sufficiently safe.
  return path.join(mutationLockRoot(), 'hcq-desktop-lab');
}

async function readLockOwner(lockPath) {
  try {
    const owner = JSON.parse(await readFile(path.join(lockPath, 'owner.json'), 'utf8'));
    if (!Number.isSafeInteger(owner.pid) || owner.pid <= 0 ||
        typeof owner.runDir !== 'string' || typeof owner.token !== 'string' ||
        !['pre-mutation', 'mutation-started'].includes(owner.phase)) {
      throw new Error('invalid owner fields');
    }
    return owner;
  } catch (error) {
    throw new Error(`HIL mutation lock is unreadable: ${lockPath} (${error.message})`);
  }
}

async function removeClaimedStaleLock(lockPath) {
  await unlink(path.join(lockPath, 'owner.json'));
  await rmdir(lockPath);
}

async function createMutationLock(lockPath, owner) {
  await mkdir(lockPath, { mode: 0o700 });
  try {
    await writeFile(path.join(lockPath, 'owner.json'), `${JSON.stringify(owner)}\n`, {
      mode: 0o600,
      flag: 'wx',
    });
  } catch (error) {
    await rmdir(lockPath).catch(() => {});
    throw error;
  }
}

async function acquireMutationLock(targets, runDir, mode) {
  await mkdir(mutationLockRoot(), { recursive: true, mode: 0o700 });
  const lockPath = mutationLockPath(targets);
  const owner = {
    pid: process.pid,
    runDir: path.resolve(runDir),
    token: randomUUID(),
    mode,
    // A recovery owns already-mutated devices even before its first REST write.
    // Persist that fact in the initial atomic owner record so a crash cannot
    // make the lock look safe for a normal run to abandon.
    phase: mode === 'recovery' ? 'mutation-started' : 'pre-mutation',
  };
  try {
    await createMutationLock(lockPath, owner);
  } catch (error) {
    if (error.code !== 'EEXIST') throw error;
    const current = await readLockOwner(lockPath);
    if (processIsAlive(current.pid)) {
      throw new Error(`another HIL run or recovery is active (pid ${current.pid}, ${current.runDir})`);
    }
    const abandonedBeforeMutation = current.phase === 'pre-mutation';
    if (mode !== 'recovery' && !abandonedBeforeMutation) {
      throw new Error(`unfinished HIL run requires recovery first (${current.runDir})`);
    }
    if (!abandonedBeforeMutation && current.runDir !== owner.runDir) {
      throw new Error(
        `stale HIL lock belongs to ${current.runDir}; refusing recovery for ${owner.runDir}`,
      );
    }
    const stalePath = `${lockPath}.stale-${owner.token}`;
    await rename(lockPath, stalePath).catch((renameError) => {
      throw new Error(`HIL mutation lock changed while claiming recovery: ${renameError.message}`);
    });
    try {
      await createMutationLock(lockPath, owner);
    } catch (claimError) {
      await removeClaimedStaleLock(stalePath).catch(() => {});
      throw new Error(`another HIL run claimed the targets during recovery: ${claimError.message}`);
    }
    await removeClaimedStaleLock(stalePath);
  }
  return {
    path: lockPath,
    ownerPath: path.join(lockPath, 'owner.json'),
    token: owner.token,
    async markMutationStarted() {
      const current = await readLockOwner(lockPath);
      if (current.token !== owner.token) {
        throw new Error('refusing to update a HIL mutation lock owned by another run');
      }
      owner.phase = 'mutation-started';
      await writeJsonAtomic(path.join(lockPath, 'owner.json'), owner);
    },
    async release() {
      const current = await readLockOwner(lockPath);
      if (current.token !== owner.token) {
        throw new Error('refusing to release a HIL mutation lock owned by another run');
      }
      await unlink(path.join(lockPath, 'owner.json'));
      await rmdir(lockPath);
    },
  };
}

export function acquireLock(targets, runDir) {
  return acquireMutationLock(targets, runDir, 'run');
}

export function acquireRecoveryLock(targets, runDir) {
  return acquireMutationLock(targets, runDir, 'recovery');
}
