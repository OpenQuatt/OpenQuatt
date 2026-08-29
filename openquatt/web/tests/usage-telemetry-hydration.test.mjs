import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

import { ENTITY_DEFS, FAST_OVERVIEW_KEYS, OVERVIEW_KEYS, QUICK_STEPS, SETTINGS_KEYS } from "../js/src/core/config.js";

globalThis.__OQ_PREVIEW__ = false;

const { getPrimeBaseKeys } = await import("../js/src/core/entity-sync.js");
const {
  isUsageTelemetryChoiceConfirmed,
  shouldInitializeQuickStartUsageTelemetryChoice,
  waitForUsageTelemetryChoiceConfirmation,
} = await import("../js/src/core/usage-telemetry-domain.js");
const {
  createUsageTelemetryPreview,
  flowSourceConfigWireValue,
  loadUsageTelemetryPreviewMqttEnabled,
  USAGE_TELEMETRY_PREVIEW_ENTITY_KEYS,
} = await import("../js/src/core/usage-telemetry-preview.js");

test("usage telemetry is hydrated before Quick Start filters optional steps", () => {
  const usageStep = QUICK_STEPS.find((step) => step.id === "usage-telemetry");

  assert.equal(usageStep?.optionalEntity, "usageTelemetryEnabled");
  assert.ok(getPrimeBaseKeys().includes("usageTelemetryEnabled"));
  assert.ok(getPrimeBaseKeys().includes("usageTelemetryChoiceConfigured"));
  assert.ok(FAST_OVERVIEW_KEYS.includes("usageTelemetryEnabled"));
  assert.ok(FAST_OVERVIEW_KEYS.includes("usageTelemetryChoiceConfigured"));
  assert.ok(OVERVIEW_KEYS.includes("usageTelemetryEnabled"));
  assert.ok(OVERVIEW_KEYS.includes("usageTelemetryChoiceConfigured"));
});

test("usage telemetry preview maps live entity values to the wire contract", () => {
  const preview = createUsageTelemetryPreview({
    usageTelemetryInstallationId: "7df1c1f8-fc47-4ac8-b0d7-94d8c42d772f",
    timeValid: true,
    uptimeRaw: 86420.9,
    projectVersionText: "v0.48.0",
    releaseChannelText: "dev",
    hardwareProfileText: "heatpump_controller_q",
    hardwareRevisionText: "1.0 (batch 42)",
    installationTopology: "duo",
    connectionText: "WiFi",
    preferredConnection: "Automatic",
    hpGeneration: "V1.5",
    flowSource: "Outdoor unit",
    qFlowSource: "Local",
    strategy: "Power House",
    roomTempSource: "OT thermostat",
    roomSetpointSource: "HA input",
    outsideTempSource: "Auto",
    heatingEnableSource: "Disabled",
    coolingEnableSource: "CIC or HA input",
    coolingDewPointSource: "MQTT",
    externalHeatDemandSource: "API input",
    heapFree: 178432,
    heapMinFree: 151008,
    heapLargestBlock: 98304,
    psramFree: 7023616,
    loopTime: 14.4,
    espInternalTemp: 47.84,
    wifiSignal: -61.04,
    cicPollingEnabled: "ON",
    cicCompatibilityMode: false,
    otEnabled: true,
    boilerCvAssistEnabled: true,
    boilerConnection: "OpenTherm",
    trendHistoryEnabled: true,
    trendHistoryFlashEnabled: false,
    decisionLogHistoryEnabled: false,
    lifetimeEnergyHistoryEnabled: true,
  }, {
    messageId: "preview-message-id",
    mqttEnabled: false,
    nowMs: 1784527200123,
  });

  assert.deepEqual(preview, {
    schema_version: 1,
    message_id: "preview-message-id",
    installation_id: "7df1c1f8-fc47-4ac8-b0d7-94d8c42d772f",
    timestamp_s: 1784527200,
    uptime_s: 86420,
    firmware_version: "v0.48.0",
    release_channel: "dev",
    hardware_profile: "heatpump_controller_q",
    hardware_revision: "1.0 (batch 42)",
    topology: "duo",
    connection: "wifi",
    connection_preference: "auto",
    quatt_hybrid_generation_config: "v1_5",
    flow_source_config: "controller_local",
    heating_strategy: "power_house",
    room_temperature_source: "opentherm",
    room_setpoint_source: "home_assistant",
    outside_temperature_source: "auto",
    heating_enable_source: "disabled",
    cooling_enable_source: "cic_or_home_assistant",
    cooling_dew_point_source: "mqtt",
    external_heat_demand_source: "api_input",
    heap_free_b: 178432,
    heap_min_free_b: 151008,
    heap_largest_block_b: 98304,
    psram_free_b: 7023616,
    loop_time_ms: 14,
    esp_internal_temp_c: 47.8,
    wifi_rssi_dbm: -61,
    reset_reason: null,
    cic_polling_enabled: true,
    cic_compatibility_enabled: false,
    ot_thermostat_enabled: true,
    boiler_assist_enabled: true,
    boiler_connection: "opentherm",
    mqtt_inputs_enabled: false,
    trend_ram_enabled: true,
    trend_flash_enabled: false,
    decision_log_flash_enabled: false,
    energy_history_flash_enabled: true,
    ram_log_history_enabled: true,
  });
  assert.equal(flowSourceConfigWireValue("Outdoor unit", "Auto", true), "outdoor_unit");
  assert.equal(flowSourceConfigWireValue("Outdoor unit", undefined, false), "outdoor_unit");
  assert.ok(USAGE_TELEMETRY_PREVIEW_ENTITY_KEYS.includes("psramFree"));
  assert.ok(!USAGE_TELEMETRY_PREVIEW_ENTITY_KEYS.includes("webServerLogHistoryEnabled"));
});

test("usage telemetry preview normalizes runtime connection states", () => {
  const preview = createUsageTelemetryPreview({
    connectionText: "Ethernet",
    preferredConnection: "WiFi",
  });
  assert.equal(preview.connection, "eth");
  assert.equal(preview.connection_preference, "wifi");
  assert.equal(createUsageTelemetryPreview({ connectionText: "Not connected" }).connection, "none");
});

test("usage telemetry preview bounds MQTT status and fails closed", async () => {
  let timeoutMs = 0;
  const enabled = await loadUsageTelemetryPreviewMqttEnabled(async (...args) => {
    timeoutMs = args[2];
    return { enabled: true };
  });

  assert.equal(enabled, true);
  assert.equal(timeoutMs, 3000);
  assert.equal(await loadUsageTelemetryPreviewMqttEnabled(async () => ({ enabled: "true" })), null);
  assert.equal(await loadUsageTelemetryPreviewMqttEnabled(async () => { throw new Error("offline"); }), null);
});

test("telemetry choice confirmation requires persisted state and the expected value", () => {
  assert.equal(isUsageTelemetryChoiceConfirmed({
    telemetryValue: "ON",
    choiceValue: "ON",
    expectedEnabled: true,
  }), true);
  assert.equal(isUsageTelemetryChoiceConfirmed({
    telemetryValue: "OFF",
    choiceValue: "ON",
    expectedEnabled: false,
  }), true);
  assert.equal(isUsageTelemetryChoiceConfirmed({
    telemetryValue: "OFF",
    choiceValue: "OFF",
    expectedEnabled: false,
  }), false);
  assert.equal(isUsageTelemetryChoiceConfirmed({
    telemetryValue: "ON",
    choiceValue: "ON",
    expectedEnabled: false,
  }), false);
  assert.equal(isUsageTelemetryChoiceConfirmed({
    telemetryValue: undefined,
    choiceValue: "ON",
    expectedEnabled: false,
  }), false);
  assert.equal(isUsageTelemetryChoiceConfirmed({
    telemetryValue: "unknown",
    choiceValue: "ON",
    expectedEnabled: false,
  }), false);
});

test("telemetry choice confirmation waits for a deferred controller write", async () => {
  let telemetryValue = "OFF";
  let choiceValue = "OFF";
  let refreshCount = 0;
  let waitCount = 0;

  const confirmed = await waitForUsageTelemetryChoiceConfirmation({
    refresh: async () => {
      refreshCount += 1;
      if (refreshCount === 3) {
        telemetryValue = "ON";
        choiceValue = "ON";
      }
      return [telemetryValue, choiceValue];
    },
    expectedEnabled: true,
    wait: async (delayMs) => {
      assert.equal(delayMs, 200);
      waitCount += 1;
    },
  });

  assert.equal(confirmed, true);
  assert.equal(refreshCount, 3);
  assert.equal(waitCount, 3);
});

test("telemetry choice confirmation retries a transient refresh failure", async () => {
  let telemetryValue = "OFF";
  let choiceValue = "OFF";
  let refreshCount = 0;

  const confirmed = await waitForUsageTelemetryChoiceConfirmation({
    refresh: async () => {
      refreshCount += 1;
      if (refreshCount === 1) {
        throw new Error("temporary read failure");
      }
      telemetryValue = "ON";
      choiceValue = "ON";
      return [telemetryValue, choiceValue];
    },
    expectedEnabled: true,
    wait: async () => {},
  });

  assert.equal(confirmed, true);
  assert.equal(refreshCount, 2);
});

test("telemetry choice confirmation stops after its bounded retry window", async () => {
  let refreshCount = 0;

  const confirmed = await waitForUsageTelemetryChoiceConfirmation({
    refresh: async () => {
      refreshCount += 1;
      return ["OFF", "ON"];
    },
    expectedEnabled: true,
    wait: async () => {},
  });

  assert.equal(confirmed, false);
  assert.equal(refreshCount, 10);
});

test("telemetry choice confirmation does not start more reads after its deadline", async () => {
  let nowMs = 0;
  let refreshCount = 0;

  const confirmed = await waitForUsageTelemetryChoiceConfirmation({
    refresh: async () => {
      refreshCount += 1;
      nowMs = 3000;
      return ["OFF", "ON"];
    },
    expectedEnabled: true,
    wait: async () => {},
    now: () => nowMs,
  });

  assert.equal(confirmed, false);
  assert.equal(refreshCount, 1);
});

test("Quick Start applies default-on only while no telemetry choice is recorded", () => {
  const base = {
    stepId: "usage-telemetry",
    telemetryAvailable: true,
    choiceAvailable: true,
  };
  assert.equal(shouldInitializeQuickStartUsageTelemetryChoice({ ...base, choiceValue: "OFF" }), true);
  assert.equal(shouldInitializeQuickStartUsageTelemetryChoice({ ...base, choiceValue: "ON" }), false);
  assert.equal(shouldInitializeQuickStartUsageTelemetryChoice({ ...base, choiceValue: "unknown" }), false);
  assert.equal(shouldInitializeQuickStartUsageTelemetryChoice({ ...base, stepId: "confirm", choiceValue: "OFF" }), false);
});

test("Quick Start locks telemetry consent before hydration starts", async () => {
  const quickStartSource = await readFile(new URL("../js/src/features/quickstart.js", import.meta.url), "utf8");
  const quickStartActionsSource = await readFile(new URL("../js/src/features/quickstart-actions.js", import.meta.url), "utf8");
  const quickStartUiActionsSource = await readFile(new URL("../js/src/features/quickstart-ui-actions.js", import.meta.url), "utf8");
  const preparationLockIndex = quickStartUiActionsSource.indexOf("state.busyAction = USAGE_TELEMETRY_PREPARATION_ACTION");
  const hydrationIndex = quickStartUiActionsSource.indexOf("await refreshQuickStartStepHydration(stepId)");

  assert.match(quickStartSource, /state\.loadingEntities \|\| Boolean\(state\.busyAction\)/);
  assert.ok(preparationLockIndex >= 0 && preparationLockIndex < hydrationIndex);
  assert.match(quickStartUiActionsSource, /preparationId !== quickStartPreparationId/);
  assert.match(quickStartUiActionsSource, /"close-quickstart-modal"[\s\S]*quickStartPreparationId \+= 1/);
  const initializeChoiceSource = quickStartActionsSource.slice(
    quickStartActionsSource.indexOf("export async function initializeQuickStartUsageTelemetryChoice"),
    quickStartActionsSource.indexOf("export async function applyQuickStartFlowSourceConfiguration"),
  );
  assert.match(initializeChoiceSource, /usageTelemetryInstallationId/);
});

test("usage telemetry disclosure matches the hourly payload scope", async () => {
  const quickStartSource = await readFile(new URL("../js/src/features/quickstart.js", import.meta.url), "utf8");
  const quickStartActionsSource = await readFile(new URL("../js/src/features/quickstart-actions.js", import.meta.url), "utf8");
  const entityWriteSource = await readFile(new URL("../js/src/core/entity-write-actions.js", import.meta.url), "utf8");
  const settingsSource = await readFile(new URL("../js/src/settings/privacy.js", import.meta.url), "utf8");
  const disclosureSource = await readFile(new URL("../js/src/features/usage-telemetry.js", import.meta.url), "utf8");
  const previewSource = await readFile(new URL("../js/src/core/usage-telemetry-preview.js", import.meta.url), "utf8");
  const entitySyncSource = await readFile(new URL("../js/src/core/entity-sync.js", import.meta.url), "utf8");
  const telemetryYaml = await readFile(new URL("../../oq_usage_telemetry.yaml", import.meta.url), "utf8");
  const telemetryCpp = await readFile(new URL("../../../components/openquatt_usage_telemetry/OpenQuattUsageTelemetry.cpp", import.meta.url), "utf8");

  assert.match(quickStartSource, /renderUsageTelemetryDisclosure\(\)/);
  assert.match(quickStartSource, /data-oq-action="confirm-no-usage-telemetry"/);
  assert.match(entityWriteSource, /commitUsageTelemetrySwitch/);
  assert.match(entityWriteSource, /waitForUsageTelemetryChoiceConfirmation/);
  assert.match(quickStartActionsSource, /waitForUsageTelemetryChoiceConfirmation/);
  assert.match(quickStartActionsSource, /USAGE_TELEMETRY_PREVIEW_ENTITY_KEYS/);
  assert.match(entityWriteSource, /"turn_off"/);
  assert.match(settingsSource, /renderUsageTelemetryDisclosure\(\{ collapsible: true/);
  assert.match(settingsSource, /usageTelemetryDetailsOpen/);
  assert.deepEqual(ENTITY_DEFS.usageTelemetryInstallationId, {
    domain: "text_sensor",
    name: "Usage statistics installation ID",
    optional: true,
  });
  assert.ok(SETTINGS_KEYS.includes("usageTelemetryInstallationId"));
  assert.match(telemetryYaml, /name: "Usage statistics installation ID"[\s\S]*internal: true/);
  assert.match(disclosureSource, /settings && enabled && hasEntity\("usageTelemetryInstallationId"\)/);
  assert.match(disclosureSource, /oq-usage-consent-installation-id/);
  assert.match(disclosureSource, /vrijwel direct en daarna ongeveer elk uur/);
  assert.match(disclosureSource, /OpenQuatt-loggingserver/);
  assert.match(disclosureSource, /actieve verbinding, verbindingsmodus/);
  assert.match(disclosureSource, /wifi-signaal/);
  assert.match(disclosureSource, /Quatt Hybrid-versie, verwarmingsstrategie, flowbron en regelbronnen/);
  assert.match(disclosureSource, /Aan\/uit-status van CiC, OpenTherm-thermostaat, ketelondersteuning, MQTT-inputs en lokale historie/);
  assert.match(disclosureSource, /ketelaansluiting \(aan\/uit of OpenTherm\)/);
  assert.match(disclosureSource, /Geen gemeten of ingestelde temperaturen, grenzen, MQTT-topics of logs/);
  assert.match(disclosureSource, /Nooit een wifi-netwerknaam, wifi-wachtwoord, gebruikersnaam, ander wachtwoord of inloggegevens/);
  assert.match(disclosureSource, /Voorbeeld van het verzonden bericht \(JSON\)/);
  assert.match(disclosureSource, /Live momentopname bij het openen van deze pagina/);
  assert.match(previewSource, /captureUsageTelemetryPreview/);
  assert.match(previewSource, /schema_version/);
  assert.match(previewSource, /timestamp_s/);
  assert.match(entitySyncSource, /shouldCaptureUsageTelemetryPreview/);
  const configFields = [
    "quatt_hybrid_generation_config",
    "flow_source_config",
    "heating_strategy",
    "room_temperature_source",
    "room_setpoint_source",
    "outside_temperature_source",
    "heating_enable_source",
    "cooling_enable_source",
    "cooling_dew_point_source",
    "external_heat_demand_source",
  ];
  for (const field of configFields) {
    assert.match(previewSource, new RegExp(field));
    assert.match(telemetryCpp, new RegExp(`"${field}"`));
  }
  assert.match(disclosureSource, /oq-usage-disclosure--collapsible/);
  assert.match(disclosureSource, /data-oq-action="toggle-usage-telemetry-details"/);
  assert.match(disclosureSource, /technisch wel het bron-IP-adres zien/);
  assert.match(disclosureSource, /OpenQuatt slaat dit IP-adres niet op/);
  assert.doesNotMatch(disclosureSource, /poort 1883|brokercredential|MQTT-broker|startvertraging|jitter/);
});
