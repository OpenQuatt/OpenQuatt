import { fetchWithTimeout } from "./browser-utils.js";
import { getEntityValue, isDeviceTimeValid } from "./entity-store.js";
import { state } from "./state.js";

const MQTT_STATUS_TIMEOUT_MS = 3000;

export const USAGE_TELEMETRY_PREVIEW_ENTITY_KEYS = [
  "usageTelemetryInstallationId",
  "timeNowHhmm",
  "uptimeRaw",
  "projectVersionText",
  "releaseChannelText",
  "hardwareProfileText",
  "hardwareRevisionText",
  "installationTopology",
  "connectionText",
  "preferredConnection",
  "hpGeneration",
  "flowSource",
  "qFlowSource",
  "strategy",
  "roomTempSource",
  "roomSetpointSource",
  "outsideTempSource",
  "heatingEnableSource",
  "coolingEnableSource",
  "coolingDewPointSource",
  "externalHeatDemandSource",
  "heapFree",
  "heapMinFree",
  "heapLargestBlock",
  "psramFree",
  "loopTime",
  "espInternalTemp",
  "wifiSignal",
  "cicPollingEnabled",
  "cicCompatibilityMode",
  "otEnabled",
  "boilerCvAssistEnabled",
  "boilerConnection",
  "trendHistoryEnabled",
  "trendHistoryFlashEnabled",
  "decisionLogHistoryEnabled",
  "lifetimeEnergyHistoryEnabled",
];

const INVALID_TEXT_VALUES = new Set(["", "unknown", "unavailable", "nan"]);

function optionalText(value) {
  const normalized = String(value ?? "").trim();
  return INVALID_TEXT_VALUES.has(normalized.toLowerCase()) ? null : normalized;
}

function optionalNumber(value, decimals = null) {
  const rawValue = typeof value === "number" ? value : String(value ?? "").trim().replace(",", ".");
  if (rawValue === "") {
    return null;
  }
  const normalized = typeof rawValue === "number" ? rawValue : Number(rawValue);
  if (!Number.isFinite(normalized)) {
    return null;
  }
  return decimals === null ? Math.trunc(normalized) : Number(normalized.toFixed(decimals));
}

function optionalBoolean(value) {
  if (value === true || value === 1) {
    return true;
  }
  if (value === false || value === 0) {
    return false;
  }
  const normalized = String(value ?? "").trim().toLowerCase();
  if (["on", "true", "yes", "1"].includes(normalized)) {
    return true;
  }
  if (["off", "false", "no", "0"].includes(normalized)) {
    return false;
  }
  return null;
}

export function quattHybridGenerationWireValue(value) {
  return ({ V1: "v1", "V1.5": "v1_5", V2: "v2" })[optionalText(value)] ?? null;
}

export function heatingStrategyWireValue(value) {
  return ({
    "Power House": "power_house",
    "Water Temperature Control (heating curve)": "heating_curve",
  })[optionalText(value)] ?? null;
}

export function configuredSourceWireValue(value) {
  return ({
    Auto: "auto",
    Local: "local",
    "Outdoor unit": "outdoor_unit",
    CIC: "cic",
    "OT thermostat": "opentherm",
    "HA input": "home_assistant",
    "Home Assistant": "home_assistant",
    "API input": "api_input",
    MQTT: "mqtt",
    "CIC or HA input": "cic_or_home_assistant",
    Disabled: "disabled",
  })[optionalText(value)] ?? null;
}

export function flowSourceConfigWireValue(flowSource, qFlowSource, qSourceAvailable) {
  if (flowSource === "CIC") {
    return "cic";
  }
  if (flowSource !== "Outdoor unit") {
    return null;
  }
  if (!qSourceAvailable) {
    return "outdoor_unit";
  }
  if (qFlowSource === "Local") {
    return "controller_local";
  }
  return qFlowSource === "Auto" || qFlowSource === "Outdoor unit" ? "outdoor_unit" : null;
}

function boilerConnectionWireValue(values) {
  if (!Object.prototype.hasOwnProperty.call(values, "boilerConnection")) {
    return "on_off";
  }
  return ({ R1: "on_off", OpenTherm: "opentherm" })[optionalText(values.boilerConnection)] ?? null;
}

function hardwareRevisionValue(value) {
  const revision = optionalText(value);
  return revision === "Read error" || revision === "Not programmed" ? null : revision;
}

function connectionWireValue(value) {
  const connection = optionalText(value);
  if (connection === "Automatic") {
    return "auto";
  }
  if (connection === "WiFi") {
    return "wifi";
  }
  if (connection === "Ethernet") {
    return "eth";
  }
  if (connection === "Not connected") {
    return "none";
  }
  return connection;
}

export function createUsageTelemetryPreview(values = {}, options = {}) {
  const nowMs = Number.isFinite(options.nowMs) ? options.nowMs : Date.now();
  const qSourceAvailable = Object.prototype.hasOwnProperty.call(values, "qFlowSource");
  return {
    schema_version: 1,
    message_id: String(options.messageId || "preview"),
    installation_id: optionalText(values.usageTelemetryInstallationId),
    timestamp_s: isDeviceTimeValid(values.timeNowHhmm) ? Math.floor(nowMs / 1000) : null,
    uptime_s: optionalNumber(values.uptimeRaw),
    firmware_version: optionalText(values.projectVersionText),
    release_channel: optionalText(values.releaseChannelText),
    hardware_profile: optionalText(values.hardwareProfileText),
    hardware_revision: hardwareRevisionValue(values.hardwareRevisionText),
    topology: optionalText(values.installationTopology),
    connection: connectionWireValue(values.connectionText),
    connection_preference: connectionWireValue(values.preferredConnection || values.connectionText),
    quatt_hybrid_generation_config: quattHybridGenerationWireValue(values.hpGeneration),
    flow_source_config: flowSourceConfigWireValue(values.flowSource, values.qFlowSource, qSourceAvailable),
    heating_strategy: heatingStrategyWireValue(values.strategy),
    room_temperature_source: configuredSourceWireValue(values.roomTempSource),
    room_setpoint_source: configuredSourceWireValue(values.roomSetpointSource),
    outside_temperature_source: configuredSourceWireValue(values.outsideTempSource),
    heating_enable_source: configuredSourceWireValue(values.heatingEnableSource),
    cooling_enable_source: configuredSourceWireValue(values.coolingEnableSource),
    cooling_dew_point_source: configuredSourceWireValue(values.coolingDewPointSource),
    external_heat_demand_source: configuredSourceWireValue(values.externalHeatDemandSource),
    heap_free_b: optionalNumber(values.heapFree),
    heap_min_free_b: optionalNumber(values.heapMinFree),
    heap_largest_block_b: optionalNumber(values.heapLargestBlock),
    psram_free_b: optionalNumber(values.psramFree),
    loop_time_ms: optionalNumber(values.loopTime, 0),
    esp_internal_temp_c: optionalNumber(values.espInternalTemp, 1),
    wifi_rssi_dbm: optionalNumber(values.wifiSignal, 1),
    reset_reason: null,
    cic_polling_enabled: optionalBoolean(values.cicPollingEnabled),
    cic_compatibility_enabled: optionalBoolean(values.cicCompatibilityMode),
    ot_thermostat_enabled: optionalBoolean(values.otEnabled),
    boiler_assist_enabled: optionalBoolean(values.boilerCvAssistEnabled),
    boiler_connection: boilerConnectionWireValue(values),
    mqtt_inputs_enabled: optionalBoolean(options.mqttEnabled),
    trend_ram_enabled: optionalBoolean(values.trendHistoryEnabled),
    trend_flash_enabled: optionalBoolean(values.trendHistoryFlashEnabled),
    decision_log_flash_enabled: optionalBoolean(values.decisionLogHistoryEnabled),
    energy_history_flash_enabled: optionalBoolean(values.lifetimeEnergyHistoryEnabled),
    ram_log_history_enabled: true,
  };
}

export function createUsageTelemetryPreviewMessageId(cryptoSource = globalThis.crypto) {
  const bytes = new Uint8Array(16);
  if (cryptoSource?.getRandomValues) {
    cryptoSource.getRandomValues(bytes);
  } else {
    for (let index = 0; index < bytes.length; index += 1) {
      bytes[index] = Math.floor(Math.random() * 256);
    }
  }
  bytes[6] = (bytes[6] & 0x0f) | 0x40;
  bytes[8] = (bytes[8] & 0x3f) | 0x80;
  const hex = [...bytes].map((value) => value.toString(16).padStart(2, "0")).join("");
  return `${hex.slice(0, 8)}-${hex.slice(8, 12)}-${hex.slice(12, 16)}-${hex.slice(16, 20)}-${hex.slice(20)}`;
}

export async function loadUsageTelemetryPreviewMqttEnabled(request = fetchWithTimeout) {
  try {
    const payload = await request(
      "/mqtt/status",
      { cache: "no-store" },
      MQTT_STATUS_TIMEOUT_MS,
      "MQTT-status ophalen duurde te lang.",
      async (response) => {
        if (!response.ok) {
          throw new Error(`HTTP ${response.status}`);
        }
        return response.json();
      },
    );
    return typeof payload?.enabled === "boolean" ? payload.enabled : null;
  } catch (_error) {
    return null;
  }
}

export function captureUsageTelemetryPreview(surface, options = {}) {
  const values = {};
  USAGE_TELEMETRY_PREVIEW_ENTITY_KEYS.forEach((key) => {
    if (state.entities[key]) {
      values[key] = getEntityValue(key);
    }
  });
  state.usageTelemetryPreviewPayload = createUsageTelemetryPreview(values, {
    messageId: createUsageTelemetryPreviewMessageId(),
    mqttEnabled: options.mqttEnabled,
  });
  state.usageTelemetryPreviewSurface = surface;
}
