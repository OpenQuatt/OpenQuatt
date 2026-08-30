import { getEntityNumericValue, hasEntity } from "../core/app-shared.js";
import { renderOqIcon, SENSOR_SELECTION_KEYS } from "../core/config.js";
import { getInputDraftValue } from "../core/control-drafts.js";
import { getEntityValue } from "../core/entity-store.js";
import { getHeatingEnableAdvice } from "../core/heating-strategy-matrix.js";
import { isInstallationMonitoringBinaryActive, isInstallationMonitoringIntegrationEnabled } from "../core/installation-monitoring.js";
import { state } from "../core/state.js";
import { formatMqttSensorValiditySummary, getMqttStatusDetail, getMqttStatusLabel, getMqttValidityLabel } from "../features/mqtt.js";
import { formatSettingsOptionLabel, getSelectEntityOptions, getSettingsStatValue, getSettingsTextStatValue, renderSettingsInfoToggle, renderSettingsIntegrationSwitchCard, renderSettingsSection } from "./controls.js";
import { getWaterSupplyCorrectionView } from "./water.js";
import { escapeHtml } from "../core/html.js";

  export function renderSettingsOpenThermCicSection() {
    const hasOpenThermConfig = hasEntity("otEnabled");
    const hasCicConfig = hasEntity("cicPollingEnabled") || hasEntity("cicFeedUrl");
    const hasCicCompatibilityConfig = hasEntity("cicCompatibilityMode");
    const hasStatus = hasEntity("otLinkProblem") || hasEntity("otbLinkAvailable") || hasEntity("boilerCommandValid") || hasEntity("cicDataStale") || hasEntity("cicJsonFeedOk");
    if (!hasOpenThermConfig && !hasCicConfig && !hasCicCompatibilityConfig && !hasStatus) {
      return "";
    }

    const cicPollingEnabled = isInstallationMonitoringIntegrationEnabled("cicPollingEnabled");
    const otEnabled = isInstallationMonitoringIntegrationEnabled("otEnabled");
    const boilerConnection = String(getEntityValue("boilerConnection") || "R1");
    const otbSelected = boilerConnection === "OpenTherm";
    const renderDiagnosticItem = ({ label, value, active = false }) => `
      <div class="oq-settings-integration-diagnostic-item${active ? " is-warning" : ""}">
        <dt>${escapeHtml(label)}</dt>
        <dd>${escapeHtml(value)}</dd>
      </div>
    `;

    const renderBinaryDiagnosticItem = (key, label, activeLabel = "Actief", inactiveLabel = "Normaal", options = {}) => {
      if (!hasEntity(key)) {
        return "";
      }
      const active = isInstallationMonitoringBinaryActive(key);
      return renderDiagnosticItem({
        label,
        value: active ? activeLabel : inactiveLabel,
        active: (options.warningWhenActive && active) || (options.warningWhenInactive && !active),
      });
    };

    const renderValueDiagnosticItem = (key, label, options = {}) => {
      const fallbackKey = options.fallbackKey || "";
      if (!hasEntity(key) && !(fallbackKey && hasEntity(fallbackKey))) {
        return "";
      }
      return renderDiagnosticItem({
        label,
        value: getSettingsStatValue(hasEntity(key) ? key : fallbackKey, options),
      });
    };

    const renderDiagnosticGroup = (title, rows) => {
      const content = rows.filter(Boolean).join("");
      if (!content) {
        return "";
      }
      return `
        <article class="oq-settings-integration-diagnostic-group">
          <h4>${escapeHtml(title)}</h4>
          <dl>${content}</dl>
        </article>
      `;
    };

    const urlField = hasEntity("cicFeedUrl") ? `
      <article class="oq-settings-integration-card oq-settings-integration-card--wide" data-oq-settings-field="cicFeedUrl">
        <div class="oq-settings-integration-card-head">
          <h4>CIC feed URL</h4>
          <span class="oq-settings-integration-pill">Lokaal</span>
        </div>
        <label class="oq-settings-control oq-settings-control--text">
          <input
            class="oq-helper-input oq-settings-integration-url-input"
            type="url"
            data-oq-field="cicFeedUrl"
            value="${escapeHtml(String(getInputDraftValue("cicFeedUrl") || ""))}"
            placeholder="http://<host>:<poort>/beta/feed/data.json"
            autocomplete="off"
            spellcheck="false"
            ${state.loadingEntities ? "disabled" : ""}
          >
        </label>
        <p>Gebruik de lokale JSON-feed van de CiC.</p>
      </article>
    ` : "";

    const otDiagnosticPanel = renderDiagnosticGroup("OpenTherm thermostaat (OTT)", [
      hasEntity("otLinkProblem") ? renderDiagnosticItem({
        label: "Thermostaatlink",
        value: !otEnabled
          ? "Uitgeschakeld"
          : isInstallationMonitoringBinaryActive("otLinkProblem") ? "Probleem" : "OK",
        active: otEnabled && isInstallationMonitoringBinaryActive("otLinkProblem"),
      }) : "",
      renderBinaryDiagnosticItem("otThermostatStatusValid", "Statusbericht (ID 0) actueel", "Ja", "Nee"),
      renderBinaryDiagnosticItem("otThermostatChEnable", "Thermostaat CH", "Actief", "Normaal"),
      renderBinaryDiagnosticItem("otThermostatCoolingEnable", "Thermostaat koeling", "Actief", "Normaal"),
      renderValueDiagnosticItem("otControlSetpoint", "Control setpoint"),
      renderValueDiagnosticItem("otRoomSetpoint", "Room setpoint", { fallbackKey: "roomSetpoint" }),
      renderValueDiagnosticItem("otRoomTemp", "Room temperature", { fallbackKey: "roomTemp" }),
    ]);

    const boilerDiagnosticPanel = renderDiagnosticGroup("Ketelregeling", [
      hasEntity("boilerConnection") ? renderDiagnosticItem({
        label: "Aansluiting",
        value: otbSelected ? "OpenTherm (OTB)" : "Aan/uit (R1)",
      }) : "",
      renderBinaryDiagnosticItem("boilerCommandValid", "Commando geldig", "Ja", "Nee", { warningWhenInactive: true }),
      renderBinaryDiagnosticItem("boilerCommandActive", "Warmtevraag", "Actief", "Uit"),
      renderValueDiagnosticItem("boilerCommandSource", "Bron"),
      renderValueDiagnosticItem("boilerCommandTargetTemperature", "Doeltemperatuur"),
      renderValueDiagnosticItem("boilerCommandRequestedPower", "Gevraagd vermogen"),
      renderValueDiagnosticItem("boilerCommandAge", "Commando-ouderdom"),
      renderValueDiagnosticItem("boilerBlockReason", "Blokkadereden"),
      renderValueDiagnosticItem("boilerStartThermalGuard", "Warme-startbeslissing"),
      renderValueDiagnosticItem("boilerStartThermalSafeCeiling", "Warme-startgrens"),
    ]);

    const otbDiagnosticRows = [
      hasEntity("otbLinkAvailable") ? renderDiagnosticItem({
        label: "Ketellink",
        value: !otbSelected
          ? "Niet geselecteerd"
          : isInstallationMonitoringBinaryActive("otbLinkAvailable") ? "OK" : "Niet verbonden",
        active: otbSelected && !isInstallationMonitoringBinaryActive("otbLinkAvailable"),
      }) : "",
    ];
    if (otbSelected) {
      otbDiagnosticRows.push(
        renderBinaryDiagnosticItem("otbChCommand", "CH-commando", "Actief", "Uit"),
        renderValueDiagnosticItem("otbControlSetpointCommand", "TSet-commando"),
        renderBinaryDiagnosticItem("otbChActive", "CV actief", "Actief", "Uit"),
        renderBinaryDiagnosticItem("otbFlameOn", "Vlam", "Aan", "Uit"),
        renderBinaryDiagnosticItem("otbDhwActive", "Tapwater actief", "Actief", "Uit"),
        renderValueDiagnosticItem("otbRelativeModulation", "Modulatie"),
        renderValueDiagnosticItem("otbChPressure", "Waterdruk"),
        renderValueDiagnosticItem("otbBoilerWaterTemp", "Keteltemperatuur"),
        renderValueDiagnosticItem("otbReturnWaterTemp", "Retourtemperatuur"),
        renderValueDiagnosticItem("otbDhwTemp", "Tapwatertemperatuur"),
        renderBinaryDiagnosticItem("otbFaultIndication", "Ketelfout", "Actief", "Geen", { warningWhenActive: true }),
        renderBinaryDiagnosticItem("otbDiagnosticIndication", "Diagnosemelding", "Actief", "Geen", { warningWhenActive: true }),
        renderBinaryDiagnosticItem("otbServiceRequest", "Service gevraagd", "Ja", "Nee", { warningWhenActive: true }),
        renderBinaryDiagnosticItem("otbLowWaterPressure", "Lage waterdruk", "Ja", "Nee", { warningWhenActive: true }),
        renderBinaryDiagnosticItem("otbFlameFault", "Vlamstoring", "Ja", "Nee", { warningWhenActive: true }),
        renderBinaryDiagnosticItem("otbAirPressureFault", "Luchtdrukstoring", "Ja", "Nee", { warningWhenActive: true }),
        renderBinaryDiagnosticItem("otbWaterOverTemp", "Overtemperatuur", "Ja", "Nee", { warningWhenActive: true }),
        renderValueDiagnosticItem("otbOemFaultCode", "OEM-foutcode"),
        renderValueDiagnosticItem("otbOemDiagnosticCode", "OEM-diagnosecode"),
        renderValueDiagnosticItem("otbLastResponseAge", "Laatste response"),
        renderValueDiagnosticItem("otbLastResponseId", "Laatste message-ID"),
        renderValueDiagnosticItem("otbResponseCount", "Geldige responses"),
      );
    }
    const otbDiagnosticPanel = renderDiagnosticGroup("OpenTherm ketel (OTB)", otbDiagnosticRows);

    const cicDiagnosticPanel = renderDiagnosticGroup("CIC-feed", [
      hasEntity("cicJsonFeedOk") ? renderDiagnosticItem({
        label: "JSON-feed",
        value: !cicPollingEnabled
          ? "Polling uit"
          : isInstallationMonitoringBinaryActive("cicJsonFeedOk") ? "OK" : "Probleem",
        active: cicPollingEnabled && !isInstallationMonitoringBinaryActive("cicJsonFeedOk"),
      }) : "",
      hasEntity("cicDataStale") ? renderDiagnosticItem({
        label: "Data",
        value: !cicPollingEnabled
          ? "Polling uit"
          : isInstallationMonitoringBinaryActive("cicDataStale") ? "Verouderd" : "Actueel",
        active: cicPollingEnabled && isInstallationMonitoringBinaryActive("cicDataStale"),
      }) : "",
      renderBinaryDiagnosticItem("cicChEnabled", "CH-vraag", "Actief", "Normaal"),
      renderBinaryDiagnosticItem("cicCoolingEnabled", "Koeling", "Actief", "Normaal"),
      renderValueDiagnosticItem("cicBoilerWaterPressure", "Waterdruk"),
      renderValueDiagnosticItem("cicControlSetpoint", "Control setpoint"),
      renderValueDiagnosticItem("cicRoomSetpoint", "Room setpoint"),
      renderValueDiagnosticItem("cicRoomTemp", "Room temperature"),
      renderValueDiagnosticItem("cicFlowrate", "Flow"),
      renderValueDiagnosticItem("cicLastSuccessAge", "Laatste succes"),
    ]);

    const diagnosticsPanel = otDiagnosticPanel || boilerDiagnosticPanel || otbDiagnosticPanel || cicDiagnosticPanel ? `
      <details class="oq-settings-integration-diagnostics"${state.integrationDiagnosticsOpen ? " open" : ""}>
        <summary data-oq-action="toggle-integration-diagnostics">
          <strong>Diagnostiek</strong>
          <span>Thermostaat-, ketel- en CiC-signalen</span>
        </summary>
        <div class="oq-settings-integration-diagnostic-grid">
          ${otDiagnosticPanel}
          ${boilerDiagnosticPanel}
          ${otbDiagnosticPanel}
          ${cicDiagnosticPanel}
        </div>
      </details>
    ` : "";

    return renderSettingsSection(
      "Integratie",
      "OpenTherm en CiC",
      "Configureer de thermostaatbus, externe CiC-feed en Quatt app-compatibiliteit.",
      `
        <div class="oq-settings-integration-grid">
          <p class="oq-settings-action-note oq-settings-integration-card--wide">
            De aansluiting van de cv-ketel — OpenTherm of aan/uit via R1 — stel je in onder <strong>Instellingen → Installatie</strong>. Daarom wordt deze hier niet apart weergegeven.
          </p>
          ${renderSettingsIntegrationSwitchCard("otEnabled", "OpenTherm-thermostaat", "Thermostaatbus voor warmtevraag en kamerwaarden.")}
          ${renderSettingsIntegrationSwitchCard("cicPollingEnabled", "CIC-polling", "JSON-feed uitlezen voor setpoint, kamerwaarden en flow.")}
          ${renderSettingsIntegrationSwitchCard("cicCompatibilityMode", "CiC-compatibiliteit", "Gegevens doorgeven zodat de Quatt app kan blijven meekijken.")}
          ${urlField}
        </div>
        ${diagnosticsPanel}
      `,
    );
  }

  export function renderSettingsSensorSelectionSection() {
    const hasSelectors = SENSOR_SELECTION_KEYS.some((key) => hasEntity(key));
    if (!hasSelectors) {
      return "";
    }

    const cicAvailable = isInstallationMonitoringIntegrationEnabled("cicPollingEnabled");
    const otAvailable = isInstallationMonitoringIntegrationEnabled("otEnabled");
    const getHaValueKey = (config = {}) => config.haValueKey || (config.haKeys || []).find((key) => !/valid$/i.test(key)) || "";
    const getHaValidKey = (config = {}) => config.haValidKey || (config.haKeys || []).find((key) => /valid$/i.test(key)) || "";
    const hasValidHaSource = (valueKey = "", validKey = "") => (
      Boolean(valueKey)
      && Boolean(validKey)
      && hasEntity(valueKey)
      && hasEntity(validKey)
      && isInstallationMonitoringBinaryActive(validKey)
    );
    const hasHaSource = (config = {}) => hasValidHaSource(getHaValueKey(config), getHaValidKey(config));
    const mqttTopicKeyByValueKey = {
      mqttCoolingDewPoint: "cooling_dew_point",
      mqttOutsideTemperature: "outside_temperature",
      mqttRoomTemperature: "room_temperature",
      mqttRoomSetpoint: "room_setpoint",
      mqttHeatingEnable: "heating_enable",
      mqttCoolingEnable: "cooling_enable",
    };
    const mqttValidKeyByTopicKey = {
      cooling_dew_point: "mqttCoolingDewPointValid",
      outside_temperature: "mqttOutsideTemperatureValid",
      room_temperature: "mqttRoomTemperatureValid",
      room_setpoint: "mqttRoomSetpointValid",
      heating_enable: "mqttHeatingEnableValid",
      cooling_enable: "mqttCoolingEnableValid",
    };
    const isHaInputOption = (option) => /^(?:ha input|home assistant)$/i.test(String(option || "").trim());
    const isApiInputOption = (option) => /^api input$/i.test(String(option || "").trim());
    const hasApiInputSource = (config = {}) => Boolean(config.apiValueKey) && hasEntity(config.apiValueKey);
    const mqttAvailable = state.mqttStatus?.enabled !== false;
    const getMqttTopicKey = (config = {}) => config.mqttTopicKey || mqttTopicKeyByValueKey[config.valueKey] || "";
    const isMqttInputTopicEnabled = (topicKey = "") => {
      if (!mqttAvailable) {
        return false;
      }
      if (!topicKey) {
        return true;
      }
      const inputEnabled = state.mqttStatus?.input_enabled;
      if (inputEnabled && typeof inputEnabled === "object" && Object.prototype.hasOwnProperty.call(inputEnabled, topicKey)) {
        return inputEnabled[topicKey] !== false;
      }
      return true;
    };
    const getMqttUnavailableSourceReason = (topicKey = "") => {
      if (!mqttAvailable) {
        return "MQTT staat uit";
      }
      return isMqttInputTopicEnabled(topicKey) ? "" : "MQTT-topic staat uit";
    };
    const isMqttOption = (option) => /\bMQTT\b/i.test(String(option || ""));
    const isSourceAvailable = (option, config = {}) => {
      if (option === "CIC") {
        return cicAvailable;
      }
      if (option === "OT thermostat") {
        return otAvailable;
      }
      if (isHaInputOption(option)) {
        return hasHaSource(config);
      }
      if (option === "CIC or HA input") {
        return cicAvailable || hasHaSource(config);
      }
      if (isApiInputOption(option)) {
        return hasApiInputSource(config);
      }
      if (isMqttOption(option)) {
        return isMqttInputTopicEnabled(getMqttTopicKey(config));
      }
      if (option === "Flowmeter HP2") {
        return hasEntity("hp2Flow");
      }
      if (option === "Local aggregate HP1/HP2") {
        return hasEntity("flowLocal") || hasEntity("hp2Flow");
      }
      return true;
    };
    const getUnavailableSourceReason = (option, config = {}) => {
      if (option === "CIC" && !cicAvailable) {
        return "CIC-polling staat uit";
      }
      if (option === "OT thermostat" && !otAvailable) {
        return "OpenTherm staat uit";
      }
      if (isHaInputOption(option) && !hasHaSource(config)) {
        return "HA-bron ongeldig";
      }
      if (option === "CIC or HA input" && !cicAvailable && !hasHaSource(config)) {
        return "CIC en HA ontbreken";
      }
      if (isApiInputOption(option) && !hasApiInputSource(config)) {
        return "API-invoer ontbreekt";
      }
      if (isMqttOption(option)) {
        return getMqttUnavailableSourceReason(getMqttTopicKey(config));
      }
      if (option === "Flowmeter HP2" && !hasEntity("hp2Flow")) {
        return "HP2-flow ontbreekt";
      }
      if (option === "Local aggregate HP1/HP2" && !hasEntity("flowLocal") && !hasEntity("hp2Flow")) {
        return "Lokale flow ontbreekt";
      }
      return "";
    };
    const sourceStateText = (key, activeLabel = "Actief", inactiveLabel = "Normaal") => {
      if (!hasEntity(key)) {
        return "";
      }
      return isInstallationMonitoringBinaryActive(key) ? activeLabel : inactiveLabel;
    };
    const hasUsableSourceValue = (key) => {
      if (!key || !hasEntity(key)) {
        return true;
      }
      const value = getEntityValue(key);
      if (typeof value === "number") {
        return Number.isFinite(value);
      }
      if (typeof value === "boolean") {
        return true;
      }
      const text = String(value ?? state.entities[key]?.state ?? "").trim().toLowerCase();
      return Boolean(text) && !["nan", "unknown", "unavailable", "none", "null", "—"].includes(text);
    };
    const invalidSourceValueWarning = (key) => hasUsableSourceValue(key)
      ? ""
      : "De ingestelde bron levert momenteel geen geldige waarde.";
    const formatSourceOptionLabel = (option, config = {}) => {
      const value = String(option || "").trim();
      if (!value) {
        return "";
      }
      return config.optionLabels?.[value] || formatSettingsOptionLabel(value);
    };
    const formattedSourceValue = (key, config = {}) => {
      const value = String(getEntityValue(key) || "").trim();
      return value ? formatSourceOptionLabel(value, config) : "";
    };
    const formattedTextSourceValue = (key) => {
      const value = getSettingsTextStatValue(key, "");
      return value ? formatSettingsOptionLabel(value) : "";
    };
    const formattedEffectivePermissionSourceValue = (key) => {
      const value = String(getSettingsTextStatValue(key, "") || "").trim();
      if (!value || value === "None") {
        return "";
      }
      return formatSettingsOptionLabel(value);
    };
    const firstAvailableSourceLabel = (...values) => values.find((value) => String(value || "").trim()) || "";
    const getWaterSupplyUsedSource = () => {
      const effectiveSource = getSettingsTextStatValue("waterSupplyTempEffectiveSource", "");
      if (effectiveSource) {
        return formatSettingsOptionLabel(effectiveSource);
      }
      const source = formattedSourceValue("waterSupplySource");
      if (String(getEntityValue("waterSupplySource") || "") === "Local" && hasEntity("localWaterSupplyTempSource")) {
        const local = formattedSourceValue("localWaterSupplyTempSource");
        return local ? `${source} - ${local}` : source;
      }
      return source;
    };
    const getFlowUsedSource = () => {
      const source = String(getEntityValue("flowSource") || "").trim();
      if (source === "Outdoor unit") {
        if (hasEntity("qFlowSource")) {
          const qSource = String(getEntityValue("qFlowSource") || "").trim();
          const hpGeneration = String(getEntityValue("hpGeneration") || "").trim();
          if (qSource === "Local" || (qSource === "Auto" && hpGeneration === "V1")) {
            return qSource === "Auto" ? "Lokaal (auto)" : "Lokaal";
          }
          return firstAvailableSourceLabel(formattedSourceValue("outdoorUnitFlowMode"), qSource === "Auto" ? "Buitenunit (auto)" : "Buitenunit");
        }
        return firstAvailableSourceLabel(formattedSourceValue("outdoorUnitFlowMode"), "Quatt-flow");
      }
      return formatSettingsOptionLabel(source);
    };
    const getOutsideTempUsedSource = () => {
      const source = String(getEntityValue("outsideTempSource") || "").trim();
      const mqttUnavailableReason = getMqttUnavailableSourceReason("outside_temperature");
      if (source === "MQTT" && mqttUnavailableReason) {
        return mqttUnavailableReason;
      }
      if (source !== "Auto") {
        return formatSettingsOptionLabel(source);
      }
      const unitTemp = getEntityNumericValue("outsideTempLocalAggregated");
      const haTemp = getEntityNumericValue("outsideTempHa");
      const mqttTemp = getEntityNumericValue("mqttOutsideTemperature");
      const apiTemp = getEntityNumericValue("apiInputOutsideTemperature");
      const unitValid = !Number.isNaN(unitTemp);
      const haValid = hasEntity("outsideTempHaValid")
        ? isInstallationMonitoringBinaryActive("outsideTempHaValid") && !Number.isNaN(haTemp)
        : !Number.isNaN(haTemp);
      const apiValid = hasEntity("apiInputOutsideTemperatureValid")
        && isInstallationMonitoringBinaryActive("apiInputOutsideTemperatureValid")
        && !Number.isNaN(apiTemp);
      const mqttValid = isMqttInputTopicEnabled("outside_temperature")
        && hasEntity("mqttOutsideTemperatureValid")
        && isInstallationMonitoringBinaryActive("mqttOutsideTemperatureValid")
        && !Number.isNaN(mqttTemp);
      const candidates = [
        unitValid ? { label: "Buitenunit", value: unitTemp } : null,
        haValid ? { label: "HA-invoer", value: haTemp } : null,
        apiValid ? { label: "API-invoer", value: apiTemp } : null,
        mqttValid ? { label: "MQTT", value: mqttTemp } : null,
      ].filter(Boolean);
      if (candidates.length) {
        return candidates.reduce((best, item) => (item.value < best.value ? item : best), candidates[0]).label;
      }
      return "Auto";
    };
    const getNumericSourceValue = (key) => {
      if (!hasEntity(key)) {
        return NaN;
      }
      const rawNumeric = Number(getEntityValue(key));
      if (Number.isFinite(rawNumeric)) {
        return rawNumeric;
      }
      const stateText = String(state.entities[key]?.state ?? "").trim().replace(",", ".");
      const match = stateText.match(/-?\d+(?:\.\d+)?/);
      return match ? Number(match[0]) : NaN;
    };
    const isValidNumericSource = (valueKey, validKey = "") => {
      if (!hasEntity(valueKey)) {
        return false;
      }
      const value = getNumericSourceValue(valueKey);
      const valid = validKey
        ? isInstallationMonitoringBinaryActive(validKey)
        : true;
      return valid && Number.isFinite(value);
    };
    const getCoolingDewPointUsedSource = () => {
      const source = String(getEntityValue("coolingDewPointSource") || "").trim();
      if (source === "Home Assistant") {
        return isValidNumericSource("coolingDewPointHa", "coolingDewPointHaValid") ? "HA-invoer" : "HA-invoer ontbreekt";
      }
      if (source === "API input") {
        return isValidNumericSource("apiInputCoolingDewPoint", "apiInputCoolingDewPointValid") ? "API-invoer" : "API-invoer ontbreekt of verouderd";
      }
      if (source === "MQTT") {
        const mqttUnavailableReason = getMqttUnavailableSourceReason("cooling_dew_point");
        if (mqttUnavailableReason) {
          return mqttUnavailableReason;
        }
        return isValidNumericSource("mqttCoolingDewPoint", "mqttCoolingDewPointValid") ? "MQTT" : "MQTT ontbreekt of verouderd";
      }

      const haValid = isValidNumericSource("coolingDewPointHa", "coolingDewPointHaValid");
      const apiValid = isValidNumericSource("apiInputCoolingDewPoint", "apiInputCoolingDewPointValid");
      const mqttValid = isMqttInputTopicEnabled("cooling_dew_point") &&
        isValidNumericSource("mqttCoolingDewPoint", "mqttCoolingDewPointValid");
      const candidates = [
        haValid ? { label: "HA-invoer", value: getNumericSourceValue("coolingDewPointHa") } : null,
        apiValid ? { label: "API-invoer", value: getNumericSourceValue("apiInputCoolingDewPoint") } : null,
        mqttValid ? { label: "MQTT", value: getNumericSourceValue("mqttCoolingDewPoint") } : null,
      ].filter((candidate) => candidate && Number.isFinite(candidate.value));
      if (candidates.length) {
        return candidates.reduce((best, item) => (item.value > best.value ? item : best), candidates[0]).label;
      }
      return source ? formatSettingsOptionLabel(source) : "Auto";
    };
    const sourceKinds = (value = "") => {
      const text = String(value || "").trim().toLowerCase();
      const kinds = new Set();
      if (/\b(ha|home assistant)\b/.test(text)) {
        kinds.add("ha");
      }
      if (/\bapi\b/.test(text)) {
        kinds.add("api");
      }
      if (/\bmqtt\b/.test(text)) {
        kinds.add("mqtt");
      }
      if (/\bcic\b/.test(text)) {
        kinds.add("cic");
      }
      if (/\b(opentherm|ot thermostat|ot-thermostaat)\b/.test(text)) {
        kinds.add("ot");
      }
      if (/\b(outdoor unit|buitenunit|quatt-flow)\b/.test(text)) {
        kinds.add("outdoor");
      }
      if (/\b(local|lokaal|controller)\b/.test(text)) {
        kinds.add("local");
      }
      if (/\bpt1000\b/.test(text)) {
        kinds.add("pt1000");
      }
      if (/\bds18b20\b/.test(text)) {
        kinds.add("ds18b20");
      }
      if (/\b(manual|handmatig)\b/.test(text)) {
        kinds.add("manual");
      }
      if (/\bhp1\b/.test(text)) {
        kinds.add("hp1");
      }
      if (/\bhp2\b/.test(text)) {
        kinds.add("hp2");
      }
      if (/\b(disabled|niet gebruiken|none)\b/.test(text) || text === "—" || text === "") {
        kinds.add("disabled");
      }
      if (!kinds.size) {
        kinds.add(text.replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "") || "other");
      }
      return kinds;
    };
    const sourcesMatch = (left = "", right = "") => {
      const leftKinds = sourceKinds(left);
      return [...sourceKinds(right)].some((kind) => leftKinds.has(kind));
    };
    const isConfiguredSource = (key, source) => sourcesMatch(getEntityValue(key), source);
    const renderSourceRow = ({
      label,
      value = "",
      key = "",
      active = false,
      status = "",
      statusTone = "",
      statusTitle = "",
      sourceKind = "",
      sourceState = "",
      effective = false,
    }) => {
      const text = value || (key ? getSettingsStatValue(key) : "");
      if (!text && !status) {
        return "";
      }
      const safeStatusTone = String(statusTone || "").replace(/[^a-z0-9_-]/gi, "");
      const safeSourceKind = String(sourceKind || "").replace(/[^a-z0-9_-]/gi, "");
      const safeSourceState = String(sourceState || "").replace(/[^a-z0-9_-]/gi, "");
      const infoText = statusTitle || status;
      const statusMarkup = status
        ? renderSettingsInfoToggle(`${key}-info`, label, infoText, status, `oq-settings-source-info oq-settings-source-info--${safeStatusTone}${status === "i" ? " oq-settings-source-info--circle" : ""}`)
        : "";
      return `
        <div
          class="oq-settings-source-row${active ? " is-warning" : ""}${status ? " has-status" : ""}${effective ? " is-effective" : ""}"
          ${safeSourceKind ? `data-source-kind="${escapeHtml(safeSourceKind)}"` : ""}
          ${safeSourceState ? `data-source-state="${escapeHtml(safeSourceState)}"` : ""}
          ${effective ? 'data-source-effective="true"' : ""}
        >
          <div class="oq-settings-source-row-label">${escapeHtml(label)}${statusMarkup}</div>
          <strong>${escapeHtml(text)}</strong>
        </div>
      `;
    };
    const renderHaSourceRows = ({ label = "HA-invoer", valueKey = "", validKey = "", value = "", forceVisible = false, effective = false }) => {
      if (!valueKey || !validKey || !hasEntity(valueKey) || !hasEntity(validKey)) {
        return [];
      }
      const valid = isInstallationMonitoringBinaryActive(validKey);
      if (!valid && !forceVisible && !effective) {
        return [];
      }
      const statusTitle = valid
        ? "Home Assistant geeft dit signaal geldig door. OpenQuatt mag deze HA-invoer gebruiken."
        : "Home Assistant geeft dit signaal niet geldig door. OpenQuatt gebruikt deze HA-invoer dan niet als bron.";
      return [renderSourceRow({
        label,
        key: valueKey,
        value: valid ? value : "—",
        status: valid ? "Beschikbaar" : "Niet geldig",
        statusTone: valid ? "valid" : "invalid",
        statusTitle,
        sourceKind: "ha",
        sourceState: valid ? "valid" : "invalid",
        effective,
      })];
    };
    const renderMqttSourceRows = ({ label = "MQTT", valueKey = "", validKey = "", value = "", topicKey = "", forceVisible = false, effective = false }) => {
      if (!valueKey || !validKey || !hasEntity(valueKey) || !hasEntity(validKey)) {
        return [];
      }
      if (!isMqttInputTopicEnabled(topicKey || mqttTopicKeyByValueKey[valueKey])) {
        return [];
      }
      const valid = isInstallationMonitoringBinaryActive(validKey);
      if (!valid && !forceVisible && !effective) {
        return [];
      }
      const statusTitle = valid
        ? "MQTT heeft een geldige, recente waarde ontvangen. OpenQuatt mag deze MQTT-invoer gebruiken."
        : "MQTT heeft nog geen geldige recente waarde ontvangen. OpenQuatt gebruikt deze MQTT-invoer dan niet als bron.";
      return [renderSourceRow({
        label,
        key: valueKey,
        value: valid ? value : "—",
        status: getMqttValidityLabel(validKey),
        statusTone: valid ? "valid" : "invalid",
        statusTitle,
        sourceKind: "mqtt",
        sourceState: valid ? "valid" : "invalid",
        effective,
      })];
    };
    const renderApiSourceRows = ({ label = "API-invoer", valueKey = "", validKey = "", ageKey = "", value = "", forceVisible = false, effective = false }) => {
      if (!valueKey || !validKey || !hasEntity(valueKey) || !hasEntity(validKey)) {
        return [];
      }
      const valid = isInstallationMonitoringBinaryActive(validKey);
      if (!valid && !forceVisible && !effective) {
        return [];
      }
      const age = ageKey && hasEntity(ageKey) ? getNumericSourceValue(ageKey) : NaN;
      const inactiveStatus = Number.isFinite(age) ? "Verouderd" : "Wacht op data";
      const statusTitle = valid
        ? "API-invoer heeft een geldige, recente waarde. OpenQuatt mag deze bron gebruiken."
        : Number.isFinite(age)
          ? "API-invoer heeft geen geldige recente waarde meer. OpenQuatt gebruikt deze bron dan niet."
          : "API-invoer heeft nog geen geldige waarde ontvangen. OpenQuatt gebruikt deze bron dan niet.";
      return [renderSourceRow({
        label,
        key: valueKey,
        value: valid ? value : "—",
        status: valid ? "Beschikbaar" : inactiveStatus,
        statusTone: valid ? "valid" : "invalid",
        statusTitle,
        sourceKind: "api",
        sourceState: valid ? "valid" : Number.isFinite(age) ? "stale" : "missing",
        effective,
      })];
    };
    const renderSourceSelect = (key, config = {}) => {
      if (!hasEntity(key)) {
        return { markup: "", warning: "" };
      }
      const entity = state.entities[key] || {};
      const current = String(getEntityValue(key) || "");
      const allOptions = getSelectEntityOptions(entity);
      const hiddenOptions = new Set(config.hiddenOptions || []);
      const currentHidden = current && hiddenOptions.has(current);
      const availableOptions = allOptions.filter((option) => !hiddenOptions.has(option) && isSourceAvailable(option, config));
      const currentUnavailable = current && !isSourceAvailable(current, config);
      const hideUnavailableCurrent = (
        currentHidden && currentUnavailable
      ) || (
        isMqttOption(current) && !isMqttInputTopicEnabled(getMqttTopicKey(config))
      ) || (
        isHaInputOption(current) && config.keepUnavailableCurrent !== true
      ) || (
        current === "CIC" && !cicAvailable
      ) || (
        current === "OT thermostat" && !otAvailable
      );
      const renderOptions = currentHidden && !hideUnavailableCurrent && !availableOptions.includes(current)
        ? [current, ...availableOptions]
        : currentUnavailable && !hideUnavailableCurrent && !availableOptions.includes(current)
        ? [current, ...availableOptions]
        : availableOptions;
      const optionMarkup = renderOptions.map((option) => {
        const displayLabel = formatSourceOptionLabel(option, config);
        return `<option value="${escapeHtml(option)}" ${option === current ? "selected" : ""}>${escapeHtml(displayLabel)}</option>`;
      }).join("");
      const unavailableCurrentPlaceholder = currentUnavailable && hideUnavailableCurrent
        ? `<option value="${escapeHtml(current)}" selected disabled>Kies een beschikbare bron</option>`
        : "";
      return {
        markup: `
          <label class="oq-settings-source-select">
            <span class="oq-settings-source-select-head">
              <span>${escapeHtml(config.label || "Bron")}</span>
              ${config.infoCopy ? renderSettingsInfoToggle(config.infoId || key, config.infoTitle || config.label || "Bron", config.infoCopy) : ""}
            </span>
            <select class="oq-helper-select" data-oq-field="${escapeHtml(key)}" ${state.loadingEntities ? "disabled" : ""}>
              ${unavailableCurrentPlaceholder}${optionMarkup}
            </select>
          </label>
        `,
        warning: currentHidden && currentUnavailable
          ? `Huidige legacybron niet beschikbaar: ${getUnavailableSourceReason(current, config)}; kies een nieuwe bron.`
          : currentHidden
          ? "Huidige bron is legacy; kies een nieuwe bron."
          : currentUnavailable ? `Huidige bron niet beschikbaar: ${getUnavailableSourceReason(current, config)}` : "",
      };
    };
    const buildSourceSignal = ({
      key,
      group,
      title,
      icon = "",
      select,
      secondarySelect = null,
      secondarySelects = null,
      summaryValue = "",
      summarySource = "",
      summaryInfo = "",
      measurementRows = [],
      measurementTitle = "Beschikbare metingen",
      warning = "",
      routeWarning = "",
    }) => {
      const mainSelect = select && select.when !== false
        ? renderSourceSelect(select.key, select)
        : { markup: "", warning: "" };
      const secondaryConfigs = Array.isArray(secondarySelects)
        ? secondarySelects
        : secondarySelect ? [secondarySelect] : [];
      const secondaries = secondaryConfigs
        .filter((config) => config && config.when !== false)
        .map((config) => renderSourceSelect(config.key, config))
        .filter((item) => item.markup);
      const secondaryMarkup = secondaries.map((item) => item.markup).join("");
      const secondaryWarning = secondaries.map((item) => item.warning).find(Boolean) || "";
      const controlsMarkup = `${mainSelect.markup}${secondaryMarkup}`;
      const current = select?.key ? String(getEntityValue(select.key) || "") : "";
      const mqttValidKey = mqttValidKeyByTopicKey[getMqttTopicKey(select || {})] || "";
      const selectedInputWarning = isApiInputOption(current) && select?.apiValidKey
        && (!hasEntity(select.apiValidKey) || !isInstallationMonitoringBinaryActive(select.apiValidKey))
          ? "De ingestelde API-invoer heeft nog geen geldige, recente waarde."
          : isMqttOption(current) && mqttValidKey
            && (!hasEntity(mqttValidKey) || !isInstallationMonitoringBinaryActive(mqttValidKey))
              ? "De ingestelde MQTT-invoer heeft nog geen geldige, recente waarde."
              : "";
      const warningCopy = mainSelect.warning || secondaryWarning || warning || selectedInputWarning || routeWarning;
      if (!controlsMarkup && !summaryValue && !summarySource && !measurementRows.some(Boolean)) {
        return "";
      }
      return {
        key,
        group,
        title,
        icon,
        fieldKey: select?.key || key,
        configuredSource: current ? formatSourceOptionLabel(current, select || {}) : "—",
        summaryValue: summaryValue || "—",
        summarySource: summarySource || "—",
        summaryInfo,
        controlsMarkup,
        warningCopy,
        measurementRows: measurementRows.filter(Boolean),
        measurementTitle,
      };
    };
    const currentWaterSupplySource = String(getEntityValue("waterSupplySource") || "");
    const currentLocalWaterSupplySource = String(getEntityValue("localWaterSupplyTempSource") || "");
    const currentFlowSource = String(getEntityValue("flowSource") || "");
    const currentQFlowSource = String(getEntityValue("qFlowSource") || "");
    const currentOutsideTempSource = String(getEntityValue("outsideTempSource") || "").trim();
    const waterSupplyCorrection = getWaterSupplyCorrectionView();
    const waterSupplyCalibrated = waterSupplyCorrection.calibrationActive;
    const localWaterSupplyWarning = currentWaterSupplySource === "Local" && currentLocalWaterSupplySource === "PT1000"
      && (isInstallationMonitoringBinaryActive("pt1000ReadProblem") || !hasUsableSourceValue("waterSupplyTempPt1000"))
        ? "De ingestelde lokale PT1000-bron levert geen geldige waarde; OpenQuatt gebruikt een fallback."
        : currentWaterSupplySource === "Local" && currentLocalWaterSupplySource === "DS18B20"
          && !hasUsableSourceValue("waterSupplyTempDs18b20")
            ? "De ingestelde lokale DS18B20-bron levert geen geldige waarde; OpenQuatt gebruikt een fallback."
            : "";
    const supplyInfo = waterSupplyCalibrated
      ? "Gekalibreerd; ruwe metingen hieronder."
      : waterSupplyCorrection.calibrationRequired
        ? "Ruwe waarde; kalibreer via Service."
        : "Ruwe waarde; niet gekalibreerd.";
    const heatingEnableSourceDisabled = String(getEntityValue("heatingEnableSource") || "").trim() === "Disabled";
    const heatingEnableSourceLabels = {
      Disabled: "Niet gebruiken",
      "API input": "API-invoer",
    };
    const heatingEnableSourceLabel = formattedSourceValue("heatingEnableSource", { optionLabels: heatingEnableSourceLabels });
    const heatingEnableEffectiveSource = formattedEffectivePermissionSourceValue("heatingEnableEffectiveSource");
    const coolingEnableSourceDisabled = String(getEntityValue("coolingEnableSource") || "").trim() === "Disabled";
    const coolingEnableSourceLabels = {
      Disabled: "Niet gebruiken / handmatig",
      CIC: "CIC (legacy)",
      "CIC or HA input": "CIC of HA-invoer (legacy)",
      "API input": "API-invoer",
    };
    const coolingEnableSourceLabel = formattedSourceValue("coolingEnableSource", { optionLabels: coolingEnableSourceLabels });
    const coolingEnableEffectiveSource = formattedEffectivePermissionSourceValue("coolingEnableEffectiveSource");
    const outsideTemperatureAutoInfo = mqttAvailable
      ? hasValidHaSource("outsideTempHa", "outsideTempHaValid")
        ? "Auto gebruikt de laagste geldige buitentemperatuurbron. Zijn buitenunit, HA-invoer, API-invoer en MQTT geldig, dan kiest OpenQuatt de laagste waarde. Is er maar een bron geldig, dan wordt die gebruikt."
        : "Auto gebruikt de laagste geldige buitentemperatuurbron."
      : hasValidHaSource("outsideTempHa", "outsideTempHaValid")
        ? "Auto gebruikt de laagste geldige buitentemperatuurbron van de buitenunit, HA-invoer en API-invoer. Is er maar een bron geldig, dan wordt die gebruikt."
        : "Auto gebruikt de laagste geldige buitentemperatuurbron.";
    const roomTemperatureUsedSource = firstAvailableSourceLabel(
      formattedTextSourceValue("roomTempEffectiveSource"),
      formattedSourceValue("roomTempSource"),
    );
    const roomSetpointUsedSource = firstAvailableSourceLabel(
      formattedTextSourceValue("roomSetpointEffectiveSource"),
      formattedSourceValue("roomSetpointSource"),
    );
    const waterSupplyUsedSource = getWaterSupplyUsedSource();
    const flowUsedSource = getFlowUsedSource();
    const outsideTemperatureUsedSource = getOutsideTempUsedSource();
    const heatingEnableUsedSource = heatingEnableSourceDisabled
      ? "—"
      : firstAvailableSourceLabel(heatingEnableEffectiveSource, heatingEnableSourceLabel);
    const coolingEnableUsedSource = firstAvailableSourceLabel(
      coolingEnableEffectiveSource,
      coolingEnableSourceDisabled ? "Handmatig" : coolingEnableSourceLabel,
    );
    const coolingDewPointUsedSource = getCoolingDewPointUsedSource();
    const externalHeatDemandConfiguredSource = formattedSourceValue("externalHeatDemandSource", {
      optionLabels: { Disabled: "Niet gebruiken", "API input": "API-invoer" },
    });
    const powerHouseDemandSource = String(getSettingsTextStatValue("powerHouseDemandSource", "") || "").trim().toLowerCase();
    const externalHeatDemandUsedSource = powerHouseDemandSource === "external"
      ? externalHeatDemandConfiguredSource
      : powerHouseDemandSource === "model" ? "Huismodel" : "—";
    const sourceSignals = [
      buildSourceSignal({
        key: "room-temperature",
        group: "room-outside",
        title: "Kamertemperatuur",
        icon: "thermometer",
        select: {
          key: "roomTempSource",
          label: "Bron",
          optionLabels: { "API input": "API-invoer" },
          haKeys: ["roomTempHa", "roomTempHaValid"],
          apiValueKey: "apiInputRoomTemperature",
          apiValidKey: "apiInputRoomTemperatureValid",
          mqttTopicKey: "room_temperature",
        },
        summaryValue: getSettingsStatValue("roomTemp"),
        summarySource: roomTemperatureUsedSource,
        routeWarning: invalidSourceValueWarning("roomTemp"),
        measurementRows: [
          cicAvailable ? renderSourceRow({ label: "CIC", key: "cicRoomTemp", sourceKind: "cic", sourceState: "available", effective: sourcesMatch(roomTemperatureUsedSource, "CIC") }) : "",
          otAvailable ? renderSourceRow({ label: "OpenTherm", key: "otRoomTemp", sourceKind: "ot", sourceState: "available", effective: sourcesMatch(roomTemperatureUsedSource, "OpenTherm") }) : "",
          ...renderHaSourceRows({
            valueKey: "roomTempHa",
            validKey: "roomTempHaValid",
            forceVisible: isConfiguredSource("roomTempSource", "HA input"),
            effective: sourcesMatch(roomTemperatureUsedSource, "HA input"),
          }),
          ...renderApiSourceRows({
            valueKey: "apiInputRoomTemperature",
            validKey: "apiInputRoomTemperatureValid",
            ageKey: "apiInputRoomTemperatureAge",
            forceVisible: isConfiguredSource("roomTempSource", "API input"),
            effective: sourcesMatch(roomTemperatureUsedSource, "API input"),
          }),
          ...renderMqttSourceRows({
            valueKey: "mqttRoomTemperature",
            validKey: "mqttRoomTemperatureValid",
            forceVisible: isConfiguredSource("roomTempSource", "MQTT"),
            effective: sourcesMatch(roomTemperatureUsedSource, "MQTT"),
          }),
        ],
      }),
      buildSourceSignal({
        key: "room-setpoint",
        group: "room-outside",
        title: "Kamer setpoint",
        icon: "target",
        select: {
          key: "roomSetpointSource",
          label: "Bron",
          optionLabels: { "API input": "API-invoer" },
          haKeys: ["roomSetpointHa", "roomSetpointHaValid"],
          apiValueKey: "apiInputRoomSetpoint",
          apiValidKey: "apiInputRoomSetpointValid",
          mqttTopicKey: "room_setpoint",
        },
        summaryValue: getSettingsStatValue("roomSetpoint"),
        summarySource: roomSetpointUsedSource,
        routeWarning: invalidSourceValueWarning("roomSetpoint"),
        measurementRows: [
          cicAvailable ? renderSourceRow({ label: "CIC", key: "cicRoomSetpoint", sourceKind: "cic", sourceState: "available", effective: sourcesMatch(roomSetpointUsedSource, "CIC") }) : "",
          otAvailable ? renderSourceRow({ label: "OpenTherm", key: "otRoomSetpoint", sourceKind: "ot", sourceState: "available", effective: sourcesMatch(roomSetpointUsedSource, "OpenTherm") }) : "",
          ...renderHaSourceRows({
            valueKey: "roomSetpointHa",
            validKey: "roomSetpointHaValid",
            forceVisible: isConfiguredSource("roomSetpointSource", "HA input"),
            effective: sourcesMatch(roomSetpointUsedSource, "HA input"),
          }),
          ...renderApiSourceRows({
            valueKey: "apiInputRoomSetpoint",
            validKey: "apiInputRoomSetpointValid",
            ageKey: "apiInputRoomSetpointAge",
            forceVisible: isConfiguredSource("roomSetpointSource", "API input"),
            effective: sourcesMatch(roomSetpointUsedSource, "API input"),
          }),
          ...renderMqttSourceRows({
            valueKey: "mqttRoomSetpoint",
            validKey: "mqttRoomSetpointValid",
            forceVisible: isConfiguredSource("roomSetpointSource", "MQTT"),
            effective: sourcesMatch(roomSetpointUsedSource, "MQTT"),
          }),
        ],
      }),
      buildSourceSignal({
        key: "water-supply",
        group: "water-circuit",
        title: "Aanvoertemperatuur",
        icon: "droplet",
        select: { key: "waterSupplySource", label: "Bron", haKeys: ["waterSupplyTempHa", "waterSupplyTempHaValid"] },
        secondarySelect: {
          key: "localWaterSupplyTempSource",
          label: "Lokale sensor",
          when: currentWaterSupplySource === "Local" && hasEntity("localWaterSupplyTempSource"),
        },
        summaryValue: getSettingsStatValue("supplyTemp"),
        summarySource: waterSupplyUsedSource,
        summaryInfo: renderSettingsInfoToggle(
          "supplyTemp-info",
          "Gebruikte waarde",
          supplyInfo,
          "i",
          `oq-settings-source-info oq-settings-source-info--${waterSupplyCalibrated ? "valid" : "error"} oq-settings-source-info--circle`,
        ),
        warning: localWaterSupplyWarning || (waterSupplyCorrection.calibrationRequired
          ? "De aanvoerbron of bronconfiguratie is gewijzigd. De oude correctie is uitgeschakeld; voer de temperatuurkalibratie opnieuw uit."
          : ""),
        routeWarning: invalidSourceValueWarning("supplyTemp"),
        measurementRows: [
          renderSourceRow({ label: "Lokale selectie", key: "waterSupplyTempEsp", sourceKind: "local", sourceState: "available", effective: sourcesMatch(waterSupplyUsedSource, "Local") }),
          renderSourceRow({ label: "PT1000", key: "waterSupplyTempPt1000", sourceKind: "pt1000", sourceState: "available", effective: sourcesMatch(waterSupplyUsedSource, "PT1000") }),
          renderSourceRow({ label: "DS18B20", key: "waterSupplyTempDs18b20", sourceKind: "ds18b20", sourceState: "available", effective: sourcesMatch(waterSupplyUsedSource, "DS18B20") }),
          cicAvailable ? renderSourceRow({ label: "CIC", key: "cicWaterSupplyTemp", sourceKind: "cic", sourceState: "available", effective: sourcesMatch(waterSupplyUsedSource, "CIC") }) : "",
          ...renderHaSourceRows({
            valueKey: "waterSupplyTempHa",
            validKey: "waterSupplyTempHaValid",
            forceVisible: isConfiguredSource("waterSupplySource", "HA input"),
            effective: sourcesMatch(waterSupplyUsedSource, "HA input"),
          }),
        ],
        measurementTitle: "Ruwe metingen",
      }),
      buildSourceSignal({
        key: "flow-source",
        group: "water-circuit",
        title: "Flow",
        icon: "waves",
        select: { key: "flowSource", label: "Bron", optionLabels: { "Outdoor unit": "Quatt-flow" }, when: cicAvailable || currentFlowSource === "CIC" },
        secondarySelects: [
          {
            key: "qFlowSource",
            label: "Flowpad",
            infoId: "qFlowSource-info",
            infoCopy: "Auto behoudt het bestaande gedrag: V1 gebruikt de lokale controller-flowmeter, V1.5 gebruikt de flow uit de buitenunit via Modbus. Kies Lokaal of Buitenunit om dit expliciet vast te zetten.",
            when: currentFlowSource === "Outdoor unit" && hasEntity("qFlowSource"),
          },
          {
            key: "outdoorUnitFlowMode",
            label: "Meterkeuze",
            infoId: "outdoorUnitFlowMode-info",
            infoCopy: "Kies welke buitenunit-flowmeting wordt gebruikt. Flowmeter HP1 en HP2 gebruiken direct die meter. Gecombineerde flow HP1/HP2 gebruikt normaal het gemiddelde, met een guard die bij sterk afwijkende meters de meest aannemelijke waarde kiest.",
            when: currentFlowSource === "Outdoor unit" && hasEntity("outdoorUnitFlowMode") && (!hasEntity("qFlowSource") || currentQFlowSource !== "Local"),
          },
        ],
        summaryValue: getSettingsStatValue("flowSelected"),
        summarySource: flowUsedSource,
        routeWarning: invalidSourceValueWarning("flowSelected"),
        measurementRows: [
          renderSourceRow({ label: "Controller-flowmeter", key: "controllerFlow", sourceKind: "local", sourceState: "available", effective: sourcesMatch(flowUsedSource, "Lokaal") }),
          renderSourceRow({ label: "Gecombineerd HP1/HP2", key: "flowLocal", sourceKind: "outdoor", sourceState: "available", effective: /gecombineerd/i.test(flowUsedSource) }),
          renderSourceRow({ label: "Flowmeter HP1", key: "hp1Flow", sourceKind: "hp1", sourceState: "available", effective: sourcesMatch(flowUsedSource, "HP1") && !sourcesMatch(flowUsedSource, "HP2") }),
          renderSourceRow({ label: "Flowmeter HP2", key: "hp2Flow", sourceKind: "hp2", sourceState: "available", effective: sourcesMatch(flowUsedSource, "HP2") && !sourcesMatch(flowUsedSource, "HP1") }),
          cicAvailable ? renderSourceRow({ label: "CIC", key: "cicFlowrate", sourceKind: "cic", sourceState: "available", effective: sourcesMatch(flowUsedSource, "CIC") }) : "",
        ],
      }),
      buildSourceSignal({
        key: "outside-temperature",
        group: "room-outside",
        title: "Buitentemperatuur",
        icon: "sun",
        warning: currentOutsideTempSource === "MQTT"
          ? "Na een (her)start is de MQTT-buitentemperatuur pas geldig na een nieuwe live publicatie. Tot die tijd ontbreekt de buitentemperatuur en kan OpenQuatt naar CM98 (antivriescirculatie) gaan. De wachttijd hangt af van het publicatie-interval. Overweeg daarom Auto; dan kan OpenQuatt tijdens het wachten een andere geldige buitentemperatuurbron gebruiken."
          : "",
        select: {
          key: "outsideTempSource",
          label: "Buiten bron",
          optionLabels: { "API input": "API-invoer" },
          haKeys: ["outsideTempHa", "outsideTempHaValid"],
          apiValueKey: "apiInputOutsideTemperature",
          apiValidKey: "apiInputOutsideTemperatureValid",
          mqttTopicKey: "outside_temperature",
          infoId: "outsideTempSource-auto-info",
          infoCopy: outsideTemperatureAutoInfo,
        },
        summaryValue: getSettingsStatValue("outsideTempSelected"),
        summarySource: outsideTemperatureUsedSource,
        routeWarning: invalidSourceValueWarning("outsideTempSelected"),
        measurementRows: [
          renderSourceRow({ label: "Buitenunit", key: "outsideTempLocalAggregated", sourceKind: "outdoor", sourceState: "available", effective: sourcesMatch(outsideTemperatureUsedSource, "Buitenunit") }),
          ...renderHaSourceRows({
            valueKey: "outsideTempHa",
            validKey: "outsideTempHaValid",
            forceVisible: isConfiguredSource("outsideTempSource", "HA input"),
            effective: sourcesMatch(outsideTemperatureUsedSource, "HA input"),
          }),
          ...renderApiSourceRows({
            valueKey: "apiInputOutsideTemperature",
            validKey: "apiInputOutsideTemperatureValid",
            ageKey: "apiInputOutsideTemperatureAge",
            forceVisible: isConfiguredSource("outsideTempSource", "API input"),
            effective: sourcesMatch(outsideTemperatureUsedSource, "API input"),
          }),
          ...renderMqttSourceRows({
            valueKey: "mqttOutsideTemperature",
            validKey: "mqttOutsideTemperatureValid",
            forceVisible: isConfiguredSource("outsideTempSource", "MQTT"),
            effective: sourcesMatch(outsideTemperatureUsedSource, "MQTT"),
          }),
        ],
      }),
      buildSourceSignal({
        key: "heating-enable",
        group: "heating",
        title: "Warmtetoestemming",
        icon: "flame",
        select: {
          key: "heatingEnableSource",
          label: "Bron",
          optionLabels: heatingEnableSourceLabels,
          infoId: "heatingEnableSource-info",
          infoCopy: "Niet gebruiken = geen externe gate; de strategie bepaalt zelf of warmte nodig is.",
          haKeys: ["heatingEnableHa", "heatingEnableHaValid"],
          apiValueKey: "apiInputHeatingEnable",
          apiValidKey: "apiInputHeatingEnableValid",
          mqttTopicKey: "heating_enable",
          keepUnavailableCurrent: true,
        },
        summaryValue: heatingEnableSourceDisabled
          ? "Niet gebruikt"
          : sourceStateText("heatingEnableSelected", "Toegestaan", "Geblokkeerd"),
        summarySource: heatingEnableUsedSource,
        routeWarning: heatingEnableSourceDisabled ? "" : invalidSourceValueWarning("heatingEnableSelected"),
        measurementRows: [
          otAvailable ? renderSourceRow({ label: "OpenTherm", value: sourceStateText("otThermostatChEnable", "Toegestaan", "Geblokkeerd"), sourceKind: "ot", sourceState: "available", effective: sourcesMatch(heatingEnableUsedSource, "OpenTherm") }) : "",
          cicAvailable ? renderSourceRow({ label: "CIC", value: sourceStateText("cicChEnabled", "Toegestaan", "Geblokkeerd"), sourceKind: "cic", sourceState: "available", effective: sourcesMatch(heatingEnableUsedSource, "CIC") }) : "",
          ...renderHaSourceRows({
            valueKey: "heatingEnableHa",
            validKey: "heatingEnableHaValid",
            value: sourceStateText("heatingEnableHa", "Toegestaan", "Geblokkeerd"),
            forceVisible: isConfiguredSource("heatingEnableSource", "HA input"),
            effective: sourcesMatch(heatingEnableUsedSource, "HA input"),
          }),
          ...renderApiSourceRows({
            valueKey: "apiInputHeatingEnable",
            validKey: "apiInputHeatingEnableValid",
            ageKey: "apiInputHeatingEnableAge",
            value: sourceStateText("apiInputHeatingEnable", "Toegestaan", "Geblokkeerd"),
            forceVisible: isConfiguredSource("heatingEnableSource", "API input"),
            effective: sourcesMatch(heatingEnableUsedSource, "API input"),
          }),
          ...renderMqttSourceRows({
            valueKey: "mqttHeatingEnable",
            validKey: "mqttHeatingEnableValid",
            value: sourceStateText("mqttHeatingEnable", "Toegestaan", "Geblokkeerd"),
            forceVisible: isConfiguredSource("heatingEnableSource", "MQTT"),
            effective: sourcesMatch(heatingEnableUsedSource, "MQTT"),
          }),
        ],
      }),
      buildSourceSignal({
        key: "cooling-enable",
        group: "cooling",
        title: "Koeltoestemming",
        icon: "snowflake",
        select: {
          key: "coolingEnableSource",
          label: "Bron",
          optionLabels: coolingEnableSourceLabels,
          hiddenOptions: ["CIC", "CIC or HA input"],
          haKeys: ["coolingEnableHa", "coolingEnableHaValid"],
          apiValueKey: "apiInputCoolingEnable",
          apiValidKey: "apiInputCoolingEnableValid",
          mqttTopicKey: "cooling_enable",
          keepUnavailableCurrent: true,
        },
        summaryValue: sourceStateText("coolingEnableSelected", "Toegestaan", "Geblokkeerd"),
        summarySource: coolingEnableUsedSource,
        routeWarning: invalidSourceValueWarning("coolingEnableSelected"),
        measurementRows: [
          renderSourceRow({ label: "Handmatig", value: sourceStateText("manualCoolingEnable", "Aan", "Uit"), sourceKind: "manual", sourceState: "available", effective: sourcesMatch(coolingEnableUsedSource, "Handmatig") }),
          otAvailable ? renderSourceRow({ label: "OpenTherm", value: sourceStateText("otThermostatCoolingEnable", "Toegestaan", "Geblokkeerd"), sourceKind: "ot", sourceState: "available", effective: sourcesMatch(coolingEnableUsedSource, "OpenTherm") }) : "",
          ...renderHaSourceRows({
            valueKey: "coolingEnableHa",
            validKey: "coolingEnableHaValid",
            value: sourceStateText("coolingEnableHa", "Toegestaan", "Geblokkeerd"),
            forceVisible: isConfiguredSource("coolingEnableSource", "HA input"),
            effective: sourcesMatch(coolingEnableUsedSource, "HA input"),
          }),
          ...renderApiSourceRows({
            valueKey: "apiInputCoolingEnable",
            validKey: "apiInputCoolingEnableValid",
            ageKey: "apiInputCoolingEnableAge",
            value: sourceStateText("apiInputCoolingEnable", "Toegestaan", "Geblokkeerd"),
            forceVisible: isConfiguredSource("coolingEnableSource", "API input"),
            effective: sourcesMatch(coolingEnableUsedSource, "API input"),
          }),
          ...renderMqttSourceRows({
            valueKey: "mqttCoolingEnable",
            validKey: "mqttCoolingEnableValid",
            value: sourceStateText("mqttCoolingEnable", "Toegestaan", "Geblokkeerd"),
            forceVisible: isConfiguredSource("coolingEnableSource", "MQTT"),
            effective: sourcesMatch(coolingEnableUsedSource, "MQTT"),
          }),
        ],
      }),
      buildSourceSignal({
        key: "cooling-dew-point",
        group: "cooling",
        title: "Koelingsdauwpunt",
        icon: "thermometer",
        select: {
          key: "coolingDewPointSource",
          label: "Bron",
          optionLabels: { "API input": "API-invoer" },
          haKeys: ["coolingDewPointHa", "coolingDewPointHaValid"],
          apiValueKey: "apiInputCoolingDewPoint",
          apiValidKey: "apiInputCoolingDewPointValid",
          mqttTopicKey: "cooling_dew_point",
          infoId: "coolingDewPointSource-info",
          infoCopy: mqttAvailable
            ? "Auto gebruikt de hoogste geldige waarde als Home Assistant, API-invoer en MQTT tegelijk geldig zijn. Kies Home Assistant, API input of MQTT om die bron expliciet te vereisen."
            : "Auto gebruikt een geldige Home Assistant-waarde wanneer die beschikbaar is. Kies Home Assistant om die bron expliciet te vereisen.",
        },
        summaryValue: getSettingsStatValue("coolingDewPointSelected"),
        summarySource: coolingDewPointUsedSource,
        routeWarning: invalidSourceValueWarning("coolingDewPointSelected"),
        measurementRows: [
          ...renderHaSourceRows({
            valueKey: "coolingDewPointHa",
            validKey: "coolingDewPointHaValid",
            forceVisible: isConfiguredSource("coolingDewPointSource", "HA input"),
            effective: sourcesMatch(coolingDewPointUsedSource, "HA input"),
          }),
          ...renderApiSourceRows({
            valueKey: "apiInputCoolingDewPoint",
            validKey: "apiInputCoolingDewPointValid",
            ageKey: "apiInputCoolingDewPointAge",
            forceVisible: isConfiguredSource("coolingDewPointSource", "API input"),
            effective: sourcesMatch(coolingDewPointUsedSource, "API input"),
          }),
          ...renderMqttSourceRows({
            valueKey: "mqttCoolingDewPoint",
            validKey: "mqttCoolingDewPointValid",
            forceVisible: isConfiguredSource("coolingDewPointSource", "MQTT"),
            effective: sourcesMatch(coolingDewPointUsedSource, "MQTT"),
          }),
        ],
      }),
      buildSourceSignal({
        key: "external-heat-demand",
        group: "heating",
        title: "Externe warmtevraag",
        icon: "zap",
        select: {
          key: "externalHeatDemandSource",
          label: "Bron",
          optionLabels: { Disabled: "Niet gebruiken", "API input": "API-invoer" },
          haKeys: ["externalHeatDemandHa", "externalHeatDemandHaValid"],
          apiValueKey: "apiInputExternalHeatDemand",
          apiValidKey: "apiInputExternalHeatDemandValid",
          infoCopy: "Vervangt alleen de vermogensschatting van het huismodel in Power House. Valt de bron weg of veroudert hij, dan rekent Power House weer met het huismodel.",
        },
        summaryValue: getSettingsStatValue("externalHeatDemandSelected"),
        summarySource: externalHeatDemandUsedSource,
        routeWarning: String(getEntityValue("externalHeatDemandSource") || "") === "Disabled"
          ? ""
          : invalidSourceValueWarning("externalHeatDemandSelected"),
        measurementRows: [
          ...renderHaSourceRows({
            valueKey: "externalHeatDemandHa",
            validKey: "externalHeatDemandHaValid",
            forceVisible: isConfiguredSource("externalHeatDemandSource", "HA input"),
            effective: sourcesMatch(externalHeatDemandUsedSource, "HA input"),
          }),
          ...renderApiSourceRows({
            valueKey: "apiInputExternalHeatDemand",
            validKey: "apiInputExternalHeatDemandValid",
            ageKey: "apiInputExternalHeatDemandAge",
            forceVisible: isConfiguredSource("externalHeatDemandSource", "API input"),
            effective: sourcesMatch(externalHeatDemandUsedSource, "API input"),
          }),
        ],
      }),
    ].filter(Boolean);

    if (!sourceSignals.length) {
      return "";
    }

    const sourceCategories = [
      {
        id: "room-outside",
        title: "Ruimte & buiten",
        icon: "home-cog",
        keys: ["room-temperature", "room-setpoint", "outside-temperature"],
      },
      {
        id: "water-circuit",
        title: "Watercircuit",
        icon: "droplet",
        keys: ["water-supply", "flow-source"],
      },
      {
        id: "heating",
        title: "Verwarmen",
        icon: "flame",
        keys: ["external-heat-demand", "heating-enable"],
      },
      {
        id: "cooling",
        title: "Koelen",
        icon: "snowflake",
        keys: ["cooling-enable", "cooling-dew-point"],
      },
    ];
    const signalByKey = new Map(sourceSignals.map((signal) => [signal.key, signal]));
    const visibleCategories = sourceCategories
      .map((category) => ({
        ...category,
        signals: category.keys.map((key) => signalByKey.get(key)).filter(Boolean),
      }))
      .filter((category) => category.signals.length);
    const requestedFocusKey = String(state.settingsSourceFocusKey || "").trim();
    const focusedSignal = signalByKey.get(requestedFocusKey) || visibleCategories[0]?.signals[0] || sourceSignals[0];
    if (state.settingsSourceFocusKey !== focusedSignal.key) {
      state.settingsSourceFocusKey = focusedSignal.key;
    }
    const focusedCategory = visibleCategories.find((category) => category.id === focusedSignal.group) || visibleCategories[0];
    const categoryMarkup = visibleCategories.map((category, categoryIndex) => {
      const count = category.signals.length;
      const categoryTitleId = `oq-settings-source-category-${category.id}`;
      const signalsMarkup = category.signals.map((signal) => {
        const active = signal.key === focusedSignal.key;
        return `
          <button
            class="oq-settings-source-signal${active ? " is-active" : ""}${signal.warningCopy ? " is-warning" : ""}"
            type="button"
            data-oq-action="select-settings-source"
            data-source-key="${escapeHtml(signal.key)}"
            data-oq-focus-key="settings-source-${escapeHtml(signal.key)}"
            aria-controls="oq-settings-source-inspector"
            ${active ? 'aria-current="true"' : ""}
          >
            <span class="oq-settings-source-signal-name">
              <span class="oq-settings-source-signal-icon">${renderOqIcon(signal.icon, "oq-settings-source-signal-icon-svg")}</span>
              <span class="oq-settings-source-signal-title">${escapeHtml(signal.title)}</span>
              ${signal.warningCopy ? `<span class="oq-settings-source-signal-warning" aria-label="Bronprobleem: ${escapeHtml(signal.warningCopy)}" title="Waarschuwing: bekijk de details">!</span>` : ""}
            </span>
            <span class="oq-settings-source-signal-summary">
              <strong>${escapeHtml(signal.summaryValue)}</strong>
              <small
                class="oq-settings-source-signal-source-path"
                aria-label="Ingesteld: ${escapeHtml(signal.configuredSource)}. Gebruikt: ${escapeHtml(signal.summarySource)}"
              >
                <span>${escapeHtml(signal.configuredSource)}</span>
                ${signal.configuredSource !== signal.summarySource ? `
                  <span class="oq-settings-source-signal-source-arrow" aria-hidden="true">→</span>
                  <span>${escapeHtml(signal.summarySource)}</span>
                ` : ""}
              </small>
            </span>
            <span class="oq-settings-source-signal-chevron" aria-hidden="true">›</span>
          </button>
        `;
      }).join("");
      return `
        <section class="oq-settings-source-category" data-source-category="${escapeHtml(category.id)}" aria-labelledby="${escapeHtml(categoryTitleId)}">
          <header class="oq-settings-source-category-head">
            <span class="oq-settings-source-category-icon">${renderOqIcon(category.icon, "oq-settings-source-category-icon-svg")}</span>
            <div class="oq-settings-source-category-copy">
              <h4 id="${escapeHtml(categoryTitleId)}">${escapeHtml(category.title)}</h4>
              <small class="oq-settings-source-category-count">${count} ${count === 1 ? "signaal" : "signalen"}</small>
            </div>
            <span class="oq-settings-source-category-index" aria-hidden="true">${String(categoryIndex + 1).padStart(2, "0")}</span>
          </header>
          ${signalsMarkup}
        </section>
      `;
    }).join("");
    const inspectorMarkup = `
      <article
        id="oq-settings-source-inspector"
        class="oq-settings-source-inspector"
        data-oq-source-inspector
        data-source-key="${escapeHtml(focusedSignal.key)}"
        data-oq-settings-field="${escapeHtml(focusedSignal.fieldKey)}"
      >
        <button class="oq-settings-source-inspector-back" type="button" data-oq-action="close-settings-source-detail" data-oq-focus-key="settings-source-detail-back">
          <span aria-hidden="true">←</span>
          Alle signalen
        </button>
        <p class="oq-settings-source-inspector-kicker">${escapeHtml(focusedCategory?.title || "Bronnen")}</p>
        <h4>${escapeHtml(focusedSignal.title)}</h4>
        ${focusedSignal.warningCopy ? `<p class="oq-settings-source-warning" data-oq-source-warning>${escapeHtml(focusedSignal.warningCopy)}</p>` : ""}
        <div class="oq-settings-source-inspector-summary">
          <div>
            <span>Ingesteld</span>
            <strong>${escapeHtml(focusedSignal.configuredSource)}</strong>
          </div>
          <div>
            <span>Gebruikt</span>
            ${focusedSignal.summaryInfo}
            <strong>${escapeHtml(focusedSignal.summaryValue)}</strong>
            <span>${escapeHtml(focusedSignal.summarySource)}</span>
          </div>
        </div>
        ${focusedSignal.controlsMarkup ? `
          <h5 class="oq-settings-source-inspector-section-title">Bronkeuze</h5>
          <div class="oq-settings-source-controls">${focusedSignal.controlsMarkup}</div>
        ` : ""}
        <h5 class="oq-settings-source-inspector-section-title">${escapeHtml(focusedSignal.measurementTitle)}</h5>
        ${focusedSignal.measurementRows.length
          ? `<div class="oq-settings-source-rows">${focusedSignal.measurementRows.join("")}</div>`
          : '<p class="oq-settings-source-empty">Nog geen relevante metingen beschikbaar.</p>'}
      </article>
    `;
    const sourceWorkspaceMarkup = `
      <div class="oq-settings-source-shell" data-oq-source-workspace>
        <div class="oq-settings-source-workspace${state.settingsSourceDetailOpen ? " is-detail-open" : ""}">
          <nav class="oq-settings-source-nav" aria-label="Signalen">
            ${categoryMarkup}
          </nav>
          ${inspectorMarkup}
        </div>
      </div>
    `;

    const heatingAdvice = hasEntity("heatingEnableSource") ? getHeatingEnableAdvice() : null;
    const heatingAdviceHeaderAction = hasEntity("heatingEnableSource") ? `<button class="oq-helper-button ${heatingAdvice && heatingAdvice.deviant ? "oq-helper-button--warning-soft" : "oq-helper-button--ghost"}" type="button" data-oq-action="open-heating-strategy-advice-modal">${heatingAdvice && heatingAdvice.deviant ? '<span class="oq-advice-warn-icon"><svg viewBox="0 0 20 18" aria-hidden="true"><path d="M10 1.6 L18.2 16.4 H1.8 Z"/><rect x="9.1" y="5.4" width="1.8" height="5.8" rx="0.9"/><circle cx="10" cy="13.6" r="1.1"/></svg></span> Advies per strategie' : "Advies per strategie"}</button>` : "";
    return renderSettingsSection(
      "Bronnen",
      "Sensorselectie",
      "Kies welke bron OpenQuatt gebruikt voor metingen en vraag-signalen. Uitgeschakelde integraties verdwijnen uit de keuzes.",
      sourceWorkspaceMarkup,
      "",
      "",
      heatingAdviceHeaderAction,
    );
  }

  export function renderSettingsMqttSection() {
    const sensorSummary = formatMqttSensorValiditySummary();
    const mqttEnabled = state.mqttStatus?.enabled === true;
    const sensorsPanel = mqttEnabled ? `
      <section class="oq-settings-mqtt-panel oq-settings-mqtt-panel--sensors oq-settings-mqtt-panel--compact">
        <div class="oq-settings-quickstart-status-row oq-settings-mqtt-status-row">
          <div>
            <p class="oq-settings-quickstart-status-label">MQTT sensoren</p>
            <strong class="oq-settings-quickstart-status-value">${escapeHtml(sensorSummary)}</strong>
          </div>
          <button
            class="oq-helper-button oq-helper-button--ghost"
            type="button"
            data-oq-action="open-mqtt-sensors-modal"
          >
            Details
          </button>
        </div>
      </section>
    ` : "";

    return renderSettingsSection(
      "Integratie",
      "MQTT inputbronnen",
      "Beheer de brokerverbinding voor externe MQTT-bronwaarden.",
      `
        <div class="oq-settings-mqtt-shell">
          <section class="oq-settings-mqtt-panel oq-settings-mqtt-panel--broker">
            <div class="oq-settings-field-head">
              <h3>MQTT brokerconfiguratie</h3>
            </div>
            <div class="oq-settings-quickstart-status-row oq-settings-mqtt-status-row">
              <div>
                <p class="oq-settings-quickstart-status-label">Huidige status</p>
                <strong class="oq-settings-quickstart-status-value">${escapeHtml(getMqttStatusLabel())}</strong>
                <p class="oq-settings-quickstart-status-copy">${escapeHtml(getMqttStatusDetail())}</p>
              </div>
              <button
                class="oq-helper-button oq-helper-button--ghost"
                type="button"
                data-oq-action="open-mqtt-modal"
              >
                Aanpassen
              </button>
            </div>
          </section>
          ${sensorsPanel}
        </div>
      `,
    );
  }
