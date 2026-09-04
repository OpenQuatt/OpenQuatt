import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  clearTimeout: globalThis.clearTimeout,
  location: { pathname: "/" },
  setTimeout: globalThis.setTimeout,
};

const { SENSOR_CALIBRATION_KEYS, SETTINGS_BACKUP_SECTIONS, SUPPLY_CALIBRATION_BACKUP_KEYS } = await import("../js/src/core/config.js");
const { state } = await import("../js/src/core/state.js");
const { isUsageTelemetrySetupCompletionSafe, parseDecisionLogStorageMetadata, parseTrendHistoryMetadata, restoreSettingsBackup, shouldDisableUsageTelemetryForSetupRestore } = await import("../js/src/features/storage-history.js");

test("trend history metadata exposes bounded flash timing evidence", () => {
  const metadata = parseTrendHistoryMetadata([
    "@now|1783851000000",
    "@flash|30 dagen|1 uur geleden|nu|nu|1.5|3",
    "@flash_io|2|0|83|91|0|2|4|97|103|5|7",
  ].join("\n"));

  assert.equal(metadata.writes, 3);
  assert.equal(metadata.eraseCount, 2);
  assert.equal(metadata.eraseFailures, 0);
  assert.equal(metadata.lastEraseDurationMs, 83);
  assert.equal(metadata.maxEraseDurationMs, 91);
  assert.equal(metadata.writeFailures, 0);
  assert.equal(metadata.lastWriteDurationMs, 2);
  assert.equal(metadata.maxWriteDurationMs, 4);
  assert.equal(metadata.lastFlushDurationMs, 97);
  assert.equal(metadata.maxFlushDurationMs, 103);
  assert.equal(metadata.lastIndexUpdateDurationMs, 5);
  assert.equal(metadata.maxIndexUpdateDurationMs, 7);
});

test("trend history flash metrics clamp malformed endpoint values", () => {
  const metadata = parseTrendHistoryMetadata(
    "@flash_io|Infinity|-1|not-a-number|3.9|4294967296|2|4|9.8|-3|4294967297|12",
  );

  assert.equal(metadata.eraseCount, 0);
  assert.equal(metadata.eraseFailures, 0);
  assert.equal(metadata.lastEraseDurationMs, 0);
  assert.equal(metadata.maxEraseDurationMs, 3);
  assert.equal(metadata.writeFailures, 4294967295);
  assert.equal(metadata.lastFlushDurationMs, 9);
  assert.equal(metadata.maxFlushDurationMs, 0);
  assert.equal(metadata.lastIndexUpdateDurationMs, 4294967295);
  assert.equal(metadata.maxIndexUpdateDurationMs, 12);
});

test("trend history flash metrics remain compatible with older firmware", () => {
  const metadata = parseTrendHistoryMetadata("@flash_io|2|0|3|4|0|1|2");

  assert.equal(metadata.maxWriteDurationMs, 2);
  assert.equal(metadata.lastFlushDurationMs, 0);
  assert.equal(metadata.maxFlushDurationMs, 0);
  assert.equal(metadata.lastIndexUpdateDurationMs, 0);
  assert.equal(metadata.maxIndexUpdateDurationMs, 0);
});

test("calibration records are restored before the supply source selection", () => {
  const keys = SETTINGS_BACKUP_SECTIONS.find(({ id }) => id === "sensor_sources").keys;
  assert.deepEqual(
    keys.slice(0, SENSOR_CALIBRATION_KEYS.length + SUPPLY_CALIBRATION_BACKUP_KEYS.length),
    [...SENSOR_CALIBRATION_KEYS, ...SUPPLY_CALIBRATION_BACKUP_KEYS],
  );
  assert.ok(keys.indexOf("waterSupplyHaInputCalibrationIdentity") < keys.indexOf("waterSupplyHaInputCalibrationOffset"));
});

test("installation backup includes the electrical current limit", () => {
  const keys = SETTINGS_BACKUP_SECTIONS.find(({ id }) => id === "installation").keys;
  assert.ok(keys.includes("electricalCurrentLimit"));
});

test("cooling backup contains both Schedule boundaries in restore order", () => {
  const keys = SETTINGS_BACKUP_SECTIONS.find(({ id }) => id === "cooling").keys;
  assert.deepEqual(keys.slice(0, 2), [
    "coolingScheduleStartTime",
    "coolingScheduleEndTime",
  ]);
});

function createCoolingScheduleRestoreFetch({
  currentSource = "Schedule",
  failEndTimeWrite = false,
  failInitialSourceRead = false,
  omitSourceFromBulk = false,
  reactivateAfterStartTimeWrite = false,
} = {}) {
  const values = new Map([
    ["Cooling Enable Source", currentSource],
    ["Cooling schedule start time", "00:00:00"],
    ["Cooling schedule end time", "00:00:00"],
  ]);
  const calls = [];
  let sourceReadFailed = false;

  const response = (payload = {}, ok = true, status = ok ? 200 : 500) => ({
    ok,
    status,
    json: async () => payload,
  });
  const defaultValue = (domain) => {
    if (domain === "switch" || domain === "binary_sensor") return false;
    if (domain === "number" || domain === "sensor") return 0;
    if (domain === "time") return "00:00:00";
    if (domain === "datetime") return "2000-01-01 00:00:00";
    if (domain === "select") return "Disabled";
    return "";
  };

  const fetch = async (rawUrl, options = {}) => {
    const url = new URL(String(rawUrl), "http://openquatt.local");
    if (url.pathname === "/openquatt/entities") {
      const rows = String(new URLSearchParams(options.body).get("entities") || "")
        .split("\n")
        .filter(Boolean);
      const entities = Object.fromEntries(rows.filter((row) =>
        !omitSourceFromBulk || !row.startsWith("coolingEnableSource\t")
      ).map((row) => {
        const [key, domain, name] = row.split("\t");
        const value = values.has(name) ? values.get(name) : defaultValue(domain);
        return [key, { value, state: value }];
      }));
      return response({ entities, missing: [] });
    }
    if (url.pathname === "/openquatt/service-status") {
      return response({ entities: {} });
    }

    const parts = url.pathname.split("/").filter(Boolean).map(decodeURIComponent);
    const [domain, name, action] = parts;
    if (options.method === "POST" && action === "set") {
      const value = domain === "select" ? url.searchParams.get("option") : url.searchParams.get("value");
      calls.push(`POST ${name}=${value}`);
      if (failEndTimeWrite && name === "Cooling schedule end time") {
        return response({}, false, 500);
      }
      values.set(name, value);
      if (reactivateAfterStartTimeWrite && name === "Cooling schedule start time") {
        values.set("Cooling Enable Source", "Schedule");
        calls.push("EXTERNAL Cooling Enable Source=Schedule");
      }
      return response();
    }
    if (failInitialSourceRead && !sourceReadFailed && name === "Cooling Enable Source") {
      sourceReadFailed = true;
      calls.push(`GET ${name}=HTTP 500`);
      return response({}, false, 500);
    }
    calls.push(`GET ${name}=${values.get(name)}`);
    const value = values.has(name) ? values.get(name) : defaultValue(domain);
    return response({ value, state: value });
  };

  return { calls, fetch, values };
}

async function runCoolingScheduleRestore(fetchImpl, {
  backupSource = "Schedule",
  includeEndTime = true,
  includeSource = true,
  includeStartTime = true,
} = {}) {
  const originalFetch = globalThis.fetch;
  const previousNativeOpen = state.nativeOpen;
  const previousEntities = state.entities;
  const previousDrafts = state.drafts;
  try {
    globalThis.fetch = fetchImpl;
    state.nativeOpen = true;
    state.entities = {};
    state.drafts = {};
    state.settingsBackupBusy = false;
    state.settingsBackupMqttPassword = "";
    state.settingsBackupRestoreResult = null;
    state.settingsBackupDraft = {
      schema_version: 2,
      settings: {
        sensor_sources: includeSource ? { coolingEnableSource: backupSource } : {},
        cooling: {
          ...(includeStartTime ? { coolingScheduleStartTime: "08:00:00" } : {}),
          ...(includeEndTime ? { coolingScheduleEndTime: "18:00:00" } : {}),
        },
      },
      mqtt: null,
    };
    await restoreSettingsBackup();
    return state.settingsBackupRestoreResult;
  } finally {
    globalThis.fetch = originalFetch;
    state.nativeOpen = previousNativeOpen;
    state.entities = previousEntities;
    state.drafts = previousDrafts;
    state.settingsBackupDraft = null;
    state.settingsBackupBusy = false;
  }
}

test("Schedule restore confirms Disabled and both times before reactivation", async () => {
  const harness = createCoolingScheduleRestoreFetch();
  const result = await runCoolingScheduleRestore(harness.fetch);

  const disabledWrite = harness.calls.indexOf("POST Cooling Enable Source=Disabled");
  const startWrite = harness.calls.indexOf("POST Cooling schedule start time=08:00:00");
  const startRead = harness.calls.indexOf("GET Cooling schedule start time=08:00:00");
  const endWrite = harness.calls.indexOf("POST Cooling schedule end time=18:00:00");
  const endRead = harness.calls.indexOf("GET Cooling schedule end time=18:00:00");
  const disabledRead = harness.calls.lastIndexOf("GET Cooling Enable Source=Disabled");
  const scheduleWrite = harness.calls.lastIndexOf("POST Cooling Enable Source=Schedule");
  assert.ok(disabledWrite >= 0 && disabledWrite < startWrite);
  assert.ok(startWrite < startRead && startRead < endWrite);
  assert.ok(endWrite < endRead && endRead < disabledRead && disabledRead < scheduleWrite);
  assert.equal(harness.values.get("Cooling Enable Source"), "Schedule");
  assert.ok(result.applied.includes("coolingScheduleStartTime"));
  assert.ok(result.applied.includes("coolingScheduleEndTime"));
  assert.ok(result.applied.includes("coolingEnableSource"));
});

test("Schedule restore stays Disabled when one time cannot be confirmed", async () => {
  const harness = createCoolingScheduleRestoreFetch({ failEndTimeWrite: true });
  const result = await runCoolingScheduleRestore(harness.fetch);

  assert.equal(harness.values.get("Cooling Enable Source"), "Disabled");
  assert.equal(result.applied.includes("coolingEnableSource"), false);
  assert.ok(result.skipped.some(({ key, reason }) =>
    key === "coolingScheduleEndTime" && reason === "Schrijven mislukt"));
  assert.ok(result.skipped.some(({ key, reason }) =>
    key === "coolingEnableSource" && reason === "Bron niet toegepast"));
});

test("restore defers a non-Schedule cooling source while the current Schedule is active", async () => {
  const harness = createCoolingScheduleRestoreFetch();
  const result = await runCoolingScheduleRestore(harness.fetch, { backupSource: "HA input" });

  const disabledWrite = harness.calls.indexOf("POST Cooling Enable Source=Disabled");
  const startWrite = harness.calls.indexOf("POST Cooling schedule start time=08:00:00");
  const endWrite = harness.calls.indexOf("POST Cooling schedule end time=18:00:00");
  const restoredSourceWrite = harness.calls.lastIndexOf("POST Cooling Enable Source=HA input");
  assert.ok(disabledWrite >= 0 && disabledWrite < startWrite);
  assert.ok(startWrite < endWrite && endWrite < restoredSourceWrite);
  assert.equal(harness.values.get("Cooling Enable Source"), "HA input");
  assert.ok(result.applied.includes("coolingEnableSource"));
});

test("Schedule restore fails closed across a missing bulk source and failed initial read", async () => {
  const harness = createCoolingScheduleRestoreFetch({
    failInitialSourceRead: true,
    omitSourceFromBulk: true,
  });
  await runCoolingScheduleRestore(harness.fetch, {
    backupSource: "HA input",
    includeEndTime: false,
    includeStartTime: false,
  });

  const failedRead = harness.calls.indexOf("GET Cooling Enable Source=HTTP 500");
  const disabledWrite = harness.calls.indexOf("POST Cooling Enable Source=Disabled");
  assert.ok(failedRead >= 0 && failedRead < disabledWrite);
  assert.equal(harness.values.get("Cooling Enable Source"), "HA input");
});

test("Schedule restore trims the selected source before applying its safety gate", async () => {
  const harness = createCoolingScheduleRestoreFetch({ currentSource: "HA input" });
  const result = await runCoolingScheduleRestore(harness.fetch, { backupSource: " Schedule " });

  assert.ok(harness.calls.indexOf("POST Cooling Enable Source=Disabled") >= 0);
  assert.equal(harness.values.get("Cooling Enable Source"), "Schedule");
  assert.ok(result.applied.includes("coolingEnableSource"));
});

test("Schedule restore disables again when the source changes between time writes", async () => {
  const harness = createCoolingScheduleRestoreFetch({
    currentSource: "HA input",
    reactivateAfterStartTimeWrite: true,
  });
  await runCoolingScheduleRestore(harness.fetch, { backupSource: "HA input" });

  const startWrite = harness.calls.indexOf("POST Cooling schedule start time=08:00:00");
  const externalChange = harness.calls.indexOf("EXTERNAL Cooling Enable Source=Schedule");
  const secondDisable = harness.calls.indexOf("POST Cooling Enable Source=Disabled", externalChange);
  const endWrite = harness.calls.indexOf("POST Cooling schedule end time=18:00:00");
  assert.ok(startWrite < externalChange && externalChange < secondDisable);
  assert.ok(secondDisable < endWrite);
});

test("backup without cooling window values preserves the current Schedule source", async () => {
  const harness = createCoolingScheduleRestoreFetch();
  await runCoolingScheduleRestore(harness.fetch, {
    includeEndTime: false,
    includeSource: false,
    includeStartTime: false,
  });

  assert.equal(harness.values.get("Cooling Enable Source"), "Schedule");
  assert.equal(harness.calls.some((call) => call.startsWith("POST Cooling Enable Source=")), false);
});

test("requested Schedule with one boundary stays Disabled", async () => {
  const harness = createCoolingScheduleRestoreFetch();
  const result = await runCoolingScheduleRestore(harness.fetch, { includeEndTime: false });

  assert.equal(harness.values.get("Cooling Enable Source"), "Disabled");
  assert.equal(harness.calls.includes("POST Cooling Enable Source=Schedule"), false);
  assert.ok(result.skipped.some(({ key, reason }) =>
    key === "coolingEnableSource" && reason === "Bron niet toegepast"));
});

test("completed backup restore disables telemetry only for incomplete setup", () => {
  assert.equal(shouldDisableUsageTelemetryForSetupRestore(true, false), true);
  assert.equal(shouldDisableUsageTelemetryForSetupRestore(true, true), false);
  assert.equal(shouldDisableUsageTelemetryForSetupRestore(false, false), false);
  assert.equal(isUsageTelemetrySetupCompletionSafe(true, false, false), false);
  assert.equal(isUsageTelemetrySetupCompletionSafe(true, false, true), true);
  assert.equal(isUsageTelemetrySetupCompletionSafe(true, true, false), true);
});

test("decision log storage metadata normalizes firmware values", () => {
  const metadata = parseDecisionLogStorageMetadata({
    enabled: true,
    available: true,
    stored_events: 420,
    capacity_events: 5120,
    retention_days: 7,
    oldest_epoch_s: 1783700000,
    newest_epoch_s: 1783850000,
    last_flush_epoch_s: 1783851000,
    storage_bytes: 131072,
    write_count: 44,
  });

  assert.deepEqual(metadata, {
    enabled: true,
    available: true,
    storedEvents: 420,
    capacityEvents: 5120,
    retentionDays: 7,
    oldestEpochS: 1783700000,
    newestEpochS: 1783850000,
    lastFlushEpochS: 1783851000,
    storageBytes: 131072,
    writeCount: 44,
  });
});

test("decision log storage metadata clamps invalid counters", () => {
  const metadata = parseDecisionLogStorageMetadata({
    stored_events: -3,
    capacity_events: 0,
    storage_bytes: "invalid",
  });

  assert.equal(metadata.enabled, false);
  assert.equal(metadata.available, false);
  assert.equal(metadata.storedEvents, 0);
  assert.equal(metadata.capacityEvents, 5120);
  assert.equal(metadata.retentionDays, 7);
  assert.equal(metadata.storageBytes, 0);
});
