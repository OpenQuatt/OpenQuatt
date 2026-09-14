#!/usr/bin/env node

import { access, chmod, mkdir } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import {
  flashFirmware,
  flashFirmwareArtifact,
  prepareFirmwareRestore,
  verifyFirmwareArtifact,
} from './firmware.mjs';
import {
  HilRestClient,
  RequestGate,
  asFiniteNumber,
  normalizeBaseUrl,
} from './rest-client.mjs';
import {
  acquireLock,
  acquireRecoveryLock,
  readSnapshot,
  restoreSettings,
  runDirectory,
  snapshotSettings,
  verifyRestoredSettings,
  writeJsonAtomic,
} from './session.mjs';
import { waitFor } from './wait.mjs';
import {
  prepareInputSourceScenario,
  runInputSourceScenarios,
} from '../../tests/hil/scenarios/input-sources.mjs';

const HIL_PROFILE = 'input-sources-fast-v1';
const SIMULATOR_CONTRACT = 'openquatt-modbus-opentherm-v2';
const HIL_PREFERENCE_SETTLE_MS = 2000;
const VALID_STAGES = new Set([
  'smoke',
  'inputs',
  'enable-expiry',
  'active-switch',
  'reboot-reset',
  'all',
]);

function usage() {
  return `Usage:
  node scripts/hil/run-input-sources.mjs --controller URL --simulator URL [--stage smoke]

Mutating run:
  node scripts/hil/run-input-sources.mjs --controller URL --simulator URL \\
    --device HOST --test-config configs/hil/input_sources_fast_duo_wifi.yaml \\
    --restore-config configs/heatpump_controller_q/duo_wifi.yaml --stage all --apply

Recovery:
  node scripts/hil/run-input-sources.mjs --controller URL --simulator URL \\
    --device HOST --restore-config configs/heatpump_controller_q/duo_wifi.yaml \\
    --restore-snapshot .tmp/hil/<run>/snapshot.json --apply

Options:
  --controller URL          Controller web-server URL; no default.
  --simulator URL           ODU simulator web-server URL; no default.
  --stage NAME              smoke, inputs, enable-expiry, active-switch,
                            reboot-reset, or all (default: smoke).
  --apply                   Explicitly allow REST writes and OTA uploads.
  --device HOST             ESPHome OTA device address.
  --test-config PATH        Optional test profile to compile and upload first.
  --restore-config PATH     Normal firmware config uploaded in finally/recovery.
  --expected-profile NAME   Required HIL marker (default: ${HIL_PROFILE}).
  --expected-simulator-contract NAME
                            Required simulator contract (default: ${SIMULATOR_CONTRACT}).
  --write-interval-ms N     Global REST-write spacing, minimum 1000 (default: 1500).
  --output-root PATH        Run artifacts (default: .tmp/hil).
  --restore-snapshot PATH   Recover settings from an earlier snapshot.
  --settings-only           Recovery only: do not restore firmware.
  --min-heap-min-free N     Optional failure threshold in bytes.
  --min-largest-block N     Optional failure threshold in bytes.
  --help                    Show this help.
`;
}

function requiredValue(argv, index, option) {
  const value = argv[index + 1];
  if (!value || value.startsWith('--')) throw new Error(`${option} requires a value`);
  return value;
}

export function parseArgs(argv, {
  validStages = VALID_STAGES,
  defaultProfile = HIL_PROFILE,
  defaultSimulatorContract = SIMULATOR_CONTRACT,
} = {}) {
  const options = {
    stage: 'smoke',
    apply: false,
    expectedProfile: defaultProfile,
    expectedSimulatorContract: defaultSimulatorContract,
    writeIntervalMs: 1500,
    outputRoot: '.tmp/hil',
    settingsOnly: false,
    minHeapMinFree: 0,
    minLargestBlock: 0,
    help: false,
  };
  for (let index = 0; index < argv.length; index += 1) {
    const option = argv[index];
    if (option === '--apply') options.apply = true;
    else if (option === '--settings-only') options.settingsOnly = true;
    else if (option === '--help') options.help = true;
    else if (option === '--controller') options.controller = requiredValue(argv, index++, option);
    else if (option === '--simulator') options.simulator = requiredValue(argv, index++, option);
    else if (option === '--stage') options.stage = requiredValue(argv, index++, option);
    else if (option === '--device') options.device = requiredValue(argv, index++, option);
    else if (option === '--test-config') options.testConfig = requiredValue(argv, index++, option);
    else if (option === '--restore-config') options.restoreConfig = requiredValue(argv, index++, option);
    else if (option === '--expected-profile') options.expectedProfile = requiredValue(argv, index++, option);
    else if (option === '--expected-simulator-contract') {
      options.expectedSimulatorContract = requiredValue(argv, index++, option);
    }
    else if (option === '--output-root') options.outputRoot = requiredValue(argv, index++, option);
    else if (option === '--restore-snapshot') options.restoreSnapshot = requiredValue(argv, index++, option);
    else if (option === '--write-interval-ms') {
      options.writeIntervalMs = Number(requiredValue(argv, index++, option));
    } else if (option === '--min-heap-min-free') {
      options.minHeapMinFree = Number(requiredValue(argv, index++, option));
    } else if (option === '--min-largest-block') {
      options.minLargestBlock = Number(requiredValue(argv, index++, option));
    } else {
      throw new Error(`unknown option: ${option}`);
    }
  }
  if (options.help) return options;
  if (!options.controller || !options.simulator) {
    throw new Error('--controller and --simulator are required');
  }
  options.controller = normalizeBaseUrl(options.controller, 'controller');
  options.simulator = normalizeBaseUrl(options.simulator, 'simulator');
  if (!validStages.has(options.stage)) throw new Error(`unsupported stage: ${options.stage}`);
  if (!Number.isFinite(options.writeIntervalMs) || options.writeIntervalMs < 1000) {
    throw new Error('--write-interval-ms must be at least 1000');
  }
  for (const key of ['minHeapMinFree', 'minLargestBlock']) {
    if (!Number.isFinite(options[key]) || options[key] < 0) {
      throw new Error(`${key} must be a non-negative number`);
    }
  }
  const mutating = options.stage !== 'smoke' || Boolean(options.restoreSnapshot);
  if (mutating && !options.apply) throw new Error('mutating HIL runs require --apply');
  if (options.testConfig && options.stage === 'smoke') {
    throw new Error('--test-config is only valid for a mutating stage');
  }
  if (options.testConfig && !options.restoreConfig) {
    throw new Error('--test-config requires --restore-config');
  }
  if (mutating && !options.settingsOnly && (!options.restoreConfig || !options.device)) {
    throw new Error('mutating HIL runs require --device and --restore-config');
  }
  if (options.settingsOnly && !options.restoreSnapshot) {
    throw new Error('--settings-only is only valid with --restore-snapshot');
  }
  return options;
}

function assert(condition, message) {
  if (!condition) throw new Error(message);
}

async function waitProfile(controller, expectedProfile, interrupted) {
  return waitFor(async () => {
    const profile = await controller.value('text_sensor', 'HIL Test Profile', { optional: true });
    return profile === expectedProfile ? profile : false;
  }, `HIL profile ${expectedProfile}`, { timeoutMs: 180000, intervalMs: 1500, interrupted });
}

export function verifyRestoredFirmware(actual, expected) {
  assert(
    actual === expected,
    `restored firmware differs: expected ${expected}, received ${actual ?? 'missing'}`,
  );
  return actual;
}

export function verifyRestoreArtifact(expectedFirmware, configHash) {
  const expectedHash = String(expectedFirmware).match(/config hash (0x[0-9a-fA-F]{8})/i)?.[1]?.toLowerCase();
  assert(expectedHash, `baseline firmware identity has no config hash: ${expectedFirmware}`);
  assert(
    configHash === expectedHash,
    `restore artifact differs: expected ${expectedHash}, compiled ${configHash ?? 'missing'}`,
  );
  return configHash;
}

export async function runGuardedMutation({ interrupted, arm = async () => {}, mutate }) {
  if (interrupted()) throw new Error('HIL run interrupted before device mutation');
  await arm();
  if (interrupted()) throw new Error('HIL run interrupted before device mutation');
  return mutate();
}

async function waitForDurableSettings(controller, simulator, snapshot) {
  await new Promise((resolve) => setTimeout(resolve, HIL_PREFERENCE_SETTLE_MS));
  await verifyRestoredSettings({ controller, simulator, snapshot });
}

export async function waitForSafeCm0Persisted(controller) {
  await new Promise((resolve) => setTimeout(resolve, HIL_PREFERENCE_SETTLE_MS));
  const values = await controller.values([
    { key: 'override', domain: 'select', name: 'CM Override' },
    { key: 'mode', domain: 'text_sensor', name: 'Control Mode' },
  ]);
  assert(
    values.override === 'Force CM0',
    `safe CM0 persistence check failed: CM Override is ${JSON.stringify(values.override)}`,
  );
  assert(
    values.mode === 'CM0',
    `safe CM0 persistence check failed: Control Mode is ${JSON.stringify(values.mode)}`,
  );
}

async function restoreArtifactPath(snapshot, runDir) {
  const fileName = snapshot.restoreArtifact?.file;
  assert(
    typeof fileName === 'string' && path.basename(fileName) === fileName,
    'snapshot has no safe prevalidated restore artifact',
  );
  verifyRestoreArtifact(snapshot.firmware, snapshot.restoreArtifact.configHash);
  const artifactPath = path.join(runDir, fileName);
  await access(artifactPath).catch((error) => {
    throw new Error(`prevalidated restore artifact is unavailable: ${artifactPath} (${error.message})`);
  });
  await verifyFirmwareArtifact(artifactPath, snapshot.restoreArtifact);
  return artifactPath;
}

async function waitNormalFirmware(controller, expectedFirmware, interrupted) {
  const actualFirmware = await waitFor(async () => {
    const values = await controller.values([
      { key: 'version', domain: 'text_sensor', name: 'ESPHome Version' },
      { key: 'profile', domain: 'text_sensor', name: 'HIL Test Profile', optional: true },
    ]);
    return values.version && values.profile === null ? values.version : false;
  }, 'normal firmware after OTA restore', { timeoutMs: 180000, intervalMs: 1500, interrupted });
  return verifyRestoredFirmware(actualFirmware, expectedFirmware);
}

async function diagnostics(controller, simulator) {
  const controllerValues = await controller.values([
    { key: 'firmware', domain: 'text_sensor', name: 'ESPHome Version' },
    { key: 'profile', domain: 'text_sensor', name: 'HIL Test Profile', optional: true },
    { key: 'heapFree', domain: 'sensor', name: 'Heap Free', optional: true },
    { key: 'heapMinFree', domain: 'sensor', name: 'Heap Min Free', optional: true },
    { key: 'largestBlock', domain: 'sensor', name: 'Heap Max Block', optional: true },
    { key: 'fragmentationPercent', domain: 'sensor', name: 'Heap Fragmentation', optional: true },
    { key: 'psramFree', domain: 'sensor', name: 'PSRAM Free', optional: true },
  ]);
  const simulatorValues = await simulator.values([
    { key: 'contract', domain: 'text_sensor', name: 'OpenQuatt Simulator Contract', optional: true },
    { key: 'version', domain: 'text_sensor', name: 'OpenQuatt Simulator Version', optional: true },
    { key: 'hp1', domain: 'text_sensor', name: 'ODU 1 diagnostics' },
    { key: 'hp2', domain: 'text_sensor', name: 'ODU 2 diagnostics' },
  ]);
  return {
    capturedAt: new Date().toISOString(),
    firmware: controllerValues.firmware,
    profile: controllerValues.profile,
    memory: {
      heapFree: asFiniteNumber(controllerValues.heapFree),
      heapMinFree: asFiniteNumber(controllerValues.heapMinFree),
      largestBlock: asFiniteNumber(controllerValues.largestBlock),
      fragmentationPercent: asFiniteNumber(controllerValues.fragmentationPercent),
      psramFree: asFiniteNumber(controllerValues.psramFree),
    },
    simulator: {
      contract: simulatorValues.contract,
      version: simulatorValues.version,
    },
    hp1: simulatorValues.hp1,
    hp2: simulatorValues.hp2,
  };
}

export function verifyDiagnostics(result, options) {
  assert(
    result.simulator?.contract === options.expectedSimulatorContract,
    `simulator contract differs: expected ${options.expectedSimulatorContract}, received ${result.simulator?.contract ?? 'missing'}`,
  );
  assert(result.simulator.version, 'simulator version is missing');
  for (const [name, text] of [['HP1', result.hp1], ['HP2', result.hp2]]) {
    for (const field of ['exc', 'bad_addr', 'bad_write', 'cap']) {
      const match = String(text).match(new RegExp(`${field}=(\\d+)`));
      assert(match && Number(match[1]) === 0, `${name} ${field} nonzero: ${text}`);
    }
  }
  if (options.minHeapMinFree > 0) {
    assert(
      result.memory.heapMinFree !== null && result.memory.heapMinFree >= options.minHeapMinFree,
      `Heap Min Free ${result.memory.heapMinFree} is below ${options.minHeapMinFree}`,
    );
  }
  if (options.minLargestBlock > 0) {
    assert(
      result.memory.largestBlock !== null && result.memory.largestBlock >= options.minLargestBlock,
      `Heap Max Block ${result.memory.largestBlock} is below ${options.minLargestBlock}`,
    );
  }
}

function assertSnapshotTargets(snapshot, options) {
  assert(snapshot.targets?.controller === options.controller, 'snapshot controller URL differs');
  assert(snapshot.targets?.simulator === options.simulator, 'snapshot simulator URL differs');
}

export function assertSnapshotScenario(snapshot, scenario) {
  assert(
    snapshot.scenario === scenario.name,
    `snapshot scenario differs: expected ${scenario.name}, received ${snapshot.scenario ?? 'missing'}`,
  );
}

export async function restoreFirmwareAndSettings({
  options,
  controller,
  simulator,
  snapshot,
  interrupted,
  restoreSettingsImpl = restoreSettings,
  flashFirmwareArtifactImpl = flashFirmwareArtifact,
  waitForSafeCm0PersistedImpl = waitForSafeCm0Persisted,
}) {
  const errors = [];
  let restoredFirmware = null;
  const attempt = async (label, operation) => {
    try {
      await operation();
      return true;
    } catch (error) {
      errors.push(new Error(`${label}: ${error.message}`));
      return false;
    }
  };
  if (options.settingsOnly) {
    const safeSettingsRestored = await attempt('restore settings while keeping safe CM0', () =>
      restoreSettingsImpl({ controller, simulator, snapshot, restoreCmOverride: false }),
    );
    if (!safeSettingsRestored) {
      throw new AggregateError(errors, 'safe CM0 restore failed; firmware mutation blocked');
    }
    restoredFirmware = snapshot.firmware;
  } else {
    const safeSettingsRestored = await attempt('restore settings in safe CM0 before firmware OTA', () =>
      restoreSettingsImpl({ controller, simulator, snapshot, restoreCmOverride: false }),
    );
    if (!safeSettingsRestored) {
      throw new AggregateError(errors, 'safe CM0 restore failed; firmware OTA blocked');
    }
    const safeCm0Persisted = await attempt('confirm persisted safe CM0 before firmware OTA', () =>
      waitForSafeCm0PersistedImpl(controller),
    );
    if (!safeCm0Persisted) {
      throw new AggregateError(errors, 'safe CM0 persistence failed; firmware OTA blocked');
    }
    const artifactVerified = await attempt('verify restore artifact immediately before OTA', () =>
      verifyFirmwareArtifact(options.restoreArtifactPath, snapshot.restoreArtifact),
    );
    if (!artifactVerified) {
      throw new AggregateError(errors, 'restore artifact verification failed; firmware OTA blocked');
    }
    const firmwareRestored = await attempt('restore normal firmware', () =>
      flashFirmwareArtifactImpl({
        config: options.restoreConfig,
        device: options.device,
        artifact: options.restoreArtifactPath,
      }),
    );
    const normalFirmwareConfirmed = firmwareRestored
      ? await attempt('wait for normal firmware', async () => {
          restoredFirmware = await waitNormalFirmware(controller, snapshot.firmware, interrupted);
        })
      : false;
    if (normalFirmwareConfirmed) {
      await attempt('restore settings after normal firmware reboot while keeping safe CM0', () =>
        restoreSettingsImpl({ controller, simulator, snapshot, restoreCmOverride: false }),
      );
    }
  }
  if (errors.length > 0) throw new AggregateError(errors, 'HIL recovery incomplete');
  return restoredFirmware;
}

async function verifySettingsOnlyFirmware(controller, snapshot) {
  const values = await controller.values([
    { key: 'firmware', domain: 'text_sensor', name: 'ESPHome Version' },
    { key: 'profile', domain: 'text_sensor', name: 'HIL Test Profile', optional: true },
  ]);
  assert(values.profile === null, '--settings-only requires normal firmware without HIL profile');
  assert(
    values.firmware === snapshot.firmware,
    `--settings-only firmware differs: expected ${snapshot.firmware}, received ${values.firmware}`,
  );
}

export async function run(options, scenario = {
  name: 'input-sources',
  prepare: prepareInputSourceScenario,
  execute: runInputSourceScenarios,
}) {
  const startedAt = new Date();
  const outputRoot = path.resolve(options.outputRoot);
  const runDir = options.restoreSnapshot
    ? path.dirname(path.resolve(options.restoreSnapshot))
    : runDirectory(outputRoot, startedAt, scenario.name);
  const reportPath = options.restoreSnapshot
    ? path.join(runDir, `recovery-${startedAt.toISOString().replace(/[:.]/g, '-')}.json`)
    : path.join(runDir, 'report.json');
  await mkdir(runDir, { recursive: true, mode: 0o700 });
  await chmod(runDir, 0o700);
  const requests = [];
  const gate = new RequestGate({ writeIntervalMs: options.writeIntervalMs });
  const clientOptions = {
    allowWrites: options.apply,
    gate,
    onRequest: (request) => requests.push({ at: new Date().toISOString(), ...request }),
  };
  const controller = new HilRestClient({
    baseUrl: options.controller,
    bulkReads: true,
    ...clientOptions,
  });
  const simulator = new HilRestClient({ baseUrl: options.simulator, ...clientOptions });
  let interruptedFlag = false;
  const interrupt = () => {
    if (interruptedFlag) process.exit(130);
    interruptedFlag = true;
    console.error('Interrupt received; recovery will run after the current operation yields.');
  };
  process.on('SIGINT', interrupt);
  process.on('SIGTERM', interrupt);
  const interrupted = () => interruptedFlag;
  let lock;
  let recoveryLock;
  let snapshot;
  let success = false;
  let failure;
  let mutationStarted = false;
  let recoverySucceeded = false;
  let before;
  let after;
  let scenarioResult;
  let restoredFirmware;
  try {
    if (options.restoreSnapshot) {
      snapshot = await readSnapshot(path.resolve(options.restoreSnapshot));
      assertSnapshotTargets(snapshot, options);
      assertSnapshotScenario(snapshot, scenario);
      const recoveryOptions = options.settingsOnly
        ? options
        : { ...options, restoreArtifactPath: await restoreArtifactPath(snapshot, runDir) };
      recoveryLock = await acquireRecoveryLock(
        { controller: options.controller, simulator: options.simulator },
        runDir,
      );
      await recoveryLock.markMutationStarted();
      mutationStarted = true;
      if (options.settingsOnly) await verifySettingsOnlyFirmware(controller, snapshot);
      restoredFirmware = await restoreFirmwareAndSettings({
        options: recoveryOptions,
        controller,
        simulator,
        snapshot,
        interrupted,
      });
      if (scenario.afterRestore) {
        await scenario.afterRestore({ controller, simulator, snapshot, interrupted });
      }
      await restoreSettings({ controller, simulator, snapshot });
      await waitForDurableSettings(controller, simulator, snapshot);
      recoverySucceeded = true;
      success = true;
    } else {
      before = await diagnostics(controller, simulator);
      console.log(`BASELINE ${JSON.stringify(before)}`);
      verifyDiagnostics(before, options);
      if (options.stage === 'smoke') {
        success = true;
      } else {
        lock = await acquireLock(
          { controller: options.controller, simulator: options.simulator },
          runDir,
        );
        snapshot = await snapshotSettings({
          controller,
          simulator,
          targets: { controller: options.controller, simulator: options.simulator },
          firmware: before.firmware,
          scenario: scenario.name,
        });
        const preparedRestore = await prepareFirmwareRestore({
          config: options.restoreConfig,
          artifactDirectory: runDir,
        });
        verifyRestoreArtifact(snapshot.firmware, preparedRestore.configHash);
        snapshot.restoreArtifact = {
          file: path.basename(preparedRestore.artifact),
          configHash: preparedRestore.configHash,
          sha256: preparedRestore.sha256,
          size: preparedRestore.size,
        };
        await writeJsonAtomic(path.join(runDir, 'snapshot.json'), snapshot);
        console.log(`SNAPSHOT ${path.join(runDir, 'snapshot.json')}`);

        if (options.testConfig) {
          await runGuardedMutation({
            interrupted,
            arm: async () => {
              await lock.markMutationStarted();
              mutationStarted = true;
            },
            mutate: () => flashFirmware({ config: options.testConfig, device: options.device }),
          });
        }
        await waitProfile(controller, options.expectedProfile, interrupted);
        await runGuardedMutation({
          interrupted,
          arm: mutationStarted
            ? undefined
            : async () => {
                await lock.markMutationStarted();
                mutationStarted = true;
              },
          mutate: () => scenario.prepare(controller, simulator, interrupted, snapshot),
        });
        scenarioResult = await scenario.execute({
          stage: options.stage,
          controller,
          simulator,
          interrupted,
          waitForProfile: () => waitProfile(controller, options.expectedProfile, interrupted),
        });
        after = await diagnostics(controller, simulator);
        verifyDiagnostics(after, options);
        console.log(`DIAGNOSTICS ${JSON.stringify(after)}`);
        success = true;
      }
    }
  } catch (error) {
    failure = error;
  } finally {
    if (snapshot && mutationStarted && !options.restoreSnapshot) {
      try {
        restoredFirmware = await restoreFirmwareAndSettings({
          options: {
            ...options,
            restoreArtifactPath: await restoreArtifactPath(snapshot, runDir),
          },
          controller,
          simulator,
          snapshot,
          interrupted: () => false,
        });
        if (scenario.afterRestore) {
          await scenario.afterRestore({ controller, simulator, snapshot, interrupted: () => false });
        }
        await restoreSettings({ controller, simulator, snapshot });
        await waitForDurableSettings(controller, simulator, snapshot);
        recoverySucceeded = true;
      } catch (restoreError) {
        failure = failure
          ? new AggregateError([failure, restoreError], 'HIL run and recovery failed')
          : restoreError;
        success = false;
      }
    }
    const recoveryComplete = !mutationStarted || recoverySucceeded;
    const recordCleanupFailure = (cleanupError) => {
      failure = failure
        ? new AggregateError([failure, cleanupError], 'HIL run and lock cleanup failed')
        : cleanupError;
      success = false;
    };
    if (recoveryLock && recoveryComplete) {
      try {
        await recoveryLock.release();
      } catch (cleanupError) {
        recordCleanupFailure(cleanupError);
      }
    }
    if (lock && recoveryComplete) {
      await lock.release().catch(recordCleanupFailure);
    }
    if (snapshot && !recoveryComplete) {
      console.error(`RECOVERY REQUIRED: ${path.join(runDir, 'snapshot.json')}`);
    }
    const report = {
      schema: 1,
      scenario: scenario.name,
      stage: options.stage,
      startedAt: startedAt.toISOString(),
      finishedAt: new Date().toISOString(),
      success: success && !failure,
      failure: failure ? String(failure.stack || failure) : null,
      requestCounts: gate.counts,
      requestIntervalMs: {
        read: gate.readIntervalMs,
        write: gate.writeIntervalMs,
      },
      diagnostics: { before, after },
      scenarioResult,
      restoredFirmware,
      requests,
    };
    try {
      await writeJsonAtomic(reportPath, report);
    } catch (reportError) {
      failure = failure
        ? new AggregateError([failure, reportError], 'HIL run and report write failed')
        : reportError;
      success = false;
    }
    process.off('SIGINT', interrupt);
    process.off('SIGTERM', interrupt);
  }
  if (failure) throw failure;
  if (options.restoreSnapshot) {
    console.log(`PASS HIL recovery; report: ${reportPath}`);
  } else if (options.stage === 'smoke') {
    console.log(`PASS read-only HIL smoke; report: ${reportPath}`);
  } else {
    console.log(`PASS ${scenario.name} HIL ${options.stage}; report: ${reportPath}`);
  }
}

const isMain = process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url);
if (isMain) {
  try {
    const options = parseArgs(process.argv.slice(2));
    if (options.help) {
      console.log(usage());
    } else {
      await run(options);
    }
  } catch (error) {
    console.error(`FAIL ${error.stack || error}`);
    process.exitCode = 1;
  }
}
