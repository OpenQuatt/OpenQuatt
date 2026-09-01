import { mkdir, open, readFile, rename, unlink, writeFile } from 'node:fs/promises';
import path from 'node:path';

import { asBoolean, asFiniteNumber } from './rest-client.mjs';
import { waitValue } from './wait.mjs';

export const SNAPSHOT_SCHEMA = 1;

export const controllerSettings = [
  { key: 'cmOverride', domain: 'select', name: 'CM Override', kind: 'select' },
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
  { key: 'demandRamp', domain: 'number', name: 'Demand filter ramp up', kind: 'number' },
  { key: 'flowSource', domain: 'select', name: 'Flow Source', kind: 'select' },
  { key: 'outdoorFlowMode', domain: 'select', name: 'Outdoor Unit Flow Mode', kind: 'select' },
  { key: 'flowMode', domain: 'select', name: 'Flow Control Mode', kind: 'select' },
  { key: 'flowSetpoint', domain: 'number', name: 'Flow Setpoint', kind: 'number' },
];

export const simulatorSettings = [
  { key: 'externalFlow', domain: 'switch', name: 'ODU external system pump flow', kind: 'switch' },
  { key: 'hp1NoFlow', domain: 'switch', name: 'ODU 1 force no flow', kind: 'switch' },
  { key: 'hp2NoFlow', domain: 'switch', name: 'ODU 2 force no flow', kind: 'switch' },
];

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

export async function snapshotSettings({ controller, simulator, targets, firmware }) {
  if (typeof firmware !== 'string' || firmware.length === 0) {
    throw new Error('firmware identity is required for a HIL snapshot');
  }
  return {
    schema: SNAPSHOT_SCHEMA,
    capturedAt: new Date().toISOString(),
    targets,
    firmware,
    controller: await readSettings(controller, controllerSettings),
    simulator: await readSettings(simulator, simulatorSettings),
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
  for (const setting of controllerSettings) {
    normalizeSettingValue(setting, snapshot.controller?.[setting.key]);
  }
  for (const setting of simulatorSettings) {
    normalizeSettingValue(setting, snapshot.simulator?.[setting.key]);
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
  await attempt('wait for safe CM0', () =>
    waitValue(controller, 'text_sensor', 'Control Mode', 'CM0', 'safe CM0 before restore', {
      timeoutMs: 50000,
    }),
  );

  const restoreGroup = async (client, settings, expectedValues, label) => {
    let currentValues;
    await attempt(`read ${label} before restore`, async () => {
      currentValues = await client.values(settings);
    });
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

export async function writeJsonAtomic(filePath, value) {
  await mkdir(path.dirname(filePath), { recursive: true });
  const temporaryPath = `${filePath}.tmp`;
  await writeFile(temporaryPath, `${JSON.stringify(value, null, 2)}\n`, { mode: 0o600 });
  await rename(temporaryPath, filePath);
}

export async function readSnapshot(filePath) {
  return validateSnapshot(JSON.parse(await readFile(filePath, 'utf8')));
}

export function runDirectory(rootDirectory, now = new Date()) {
  const timestamp = now.toISOString().replace(/[:.]/g, '-');
  return path.join(rootDirectory, `${timestamp}-input-sources`);
}

export async function acquireLock(rootDirectory, runDir) {
  await mkdir(rootDirectory, { recursive: true });
  const lockPath = path.join(rootDirectory, 'input-sources.lock');
  let handle;
  try {
    handle = await open(lockPath, 'wx', 0o600);
  } catch (error) {
    if (error.code !== 'EEXIST') throw error;
    const current = await readFile(lockPath, 'utf8').catch(() => 'unknown run');
    throw new Error(
      `another HIL run or recovery is active (${current.trim()}); restore it before continuing`,
    );
  }
  await handle.writeFile(`${runDir}\n`);
  await handle.close();
  return {
    path: lockPath,
    async release() {
      await unlink(lockPath).catch((error) => {
        if (error.code !== 'ENOENT') throw error;
      });
    },
  };
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

export async function acquireRecoveryLock(rootDirectory, runDir) {
  await mkdir(rootDirectory, { recursive: true });
  const lockPath = path.join(rootDirectory, 'input-sources-recovery.lock');
  for (let attempt = 0; attempt < 2; attempt += 1) {
    let handle;
    try {
      handle = await open(lockPath, 'wx', 0o600);
      await handle.writeFile(`${JSON.stringify({ pid: process.pid, runDir })}\n`);
      await handle.close();
      return {
        path: lockPath,
        async release() {
          await unlink(lockPath).catch((error) => {
            if (error.code !== 'ENOENT') throw error;
          });
        },
      };
    } catch (error) {
      await handle?.close().catch(() => {});
      if (error.code !== 'EEXIST') throw error;
      let owner;
      try {
        owner = JSON.parse(await readFile(lockPath, 'utf8'));
      } catch {
        throw new Error(`HIL recovery lock is unreadable: ${lockPath}`);
      }
      if (!Number.isSafeInteger(owner.pid) || owner.pid <= 0) {
        throw new Error(`HIL recovery lock has no valid owner: ${lockPath}`);
      }
      if (processIsAlive(owner.pid)) {
        throw new Error(`another HIL recovery is active (pid ${owner.pid})`);
      }
      await unlink(lockPath).catch((unlinkError) => {
        if (unlinkError.code !== 'ENOENT') throw unlinkError;
      });
    }
  }
  throw new Error(`could not acquire HIL recovery lock: ${lockPath}`);
}
