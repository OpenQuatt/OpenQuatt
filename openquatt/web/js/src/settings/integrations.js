import { getEntityNumericValue, hasEntity } from "../core/app-shared.js";
import { renderOqIcon, SENSOR_SELECTION_KEYS } from "../core/config.js";
import { getInputDraftValue } from "../core/control-drafts.js";
import { getEntityValue } from "../core/entity-store.js";
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
      if (option === "HA input") {
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
      if (option === "HA input" && !hasHaSource(config)) {
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
    const renderSourceRow = ({ label, value = "", key = "", active = false, status = "", statusTone = "", statusTitle = "" }) => {
      const text = value || (key ? getSettingsStatValue(key) : "");
      if (!text && !status) {
        return "";
      }
      const safeStatusTone = String(statusTone || "").replace(/[^a-z0-9_-]/gi, "");
      const infoText = statusTitle || status;
      const statusMarkup = status
        ? renderSettingsInfoToggle(`${key}-info`, label, infoText, status, `oq-settings-source-info oq-settings-source-info--${safeStatusTone}${status === "i" ? " oq-settings-source-info--circle" : ""}`)
        : "";
      return `
        <div class="oq-settings-source-row${active ? " is-warning" : ""}${status ? " has-status" : ""}">
          <div class="oq-settings-source-row-label">${escapeHtml(label)}${statusMarkup}</div>
          <strong>${escapeHtml(text)}</strong>
        </div>
      `;
    };
    const renderHaSourceRows = ({ label = "HA-invoer", valueKey = "", validKey = "", value = "" }) => {
      if (!valueKey || !validKey || !hasEntity(valueKey) || !hasEntity(validKey)) {
        return [];
      }
      const valid = isInstallationMonitoringBinaryActive(validKey);
      const statusTitle = valid
        ? "Home Assistant geeft dit signaal geldig door. OpenQuatt mag deze HA-invoer gebruiken."
        : "Home Assistant geeft dit signaal niet geldig door. OpenQuatt gebruikt deze HA-invoer dan niet als bron.";
      return [renderSourceRow({
        label,
        key: valueKey,
        value,
        status: valid ? "Geldig" : "Ongeldig",
        statusTone: valid ? "valid" : "invalid",
        statusTitle,
      })];
    };
    const renderMqttSourceRows = ({ label = "MQTT", valueKey = "", validKey = "", value = "", topicKey = "" }) => {
      if (!valueKey || !validKey || !hasEntity(valueKey) || !hasEntity(validKey)) {
        return [];
      }
      if (!isMqttInputTopicEnabled(topicKey || mqttTopicKeyByValueKey[valueKey])) {
        return [];
      }
      const valid = isInstallationMonitoringBinaryActive(validKey);
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
      })];
    };
    const renderApiSourceRows = ({ label = "API-invoer", valueKey = "", validKey = "", value = "" }) => {
      if (!valueKey || !validKey || !hasEntity(valueKey) || !hasEntity(validKey)) {
        return [];
      }
      const valid = isInstallationMonitoringBinaryActive(validKey);
      const statusTitle = valid
        ? "API-invoer heeft een geldige, recente waarde. OpenQuatt mag deze bron gebruiken."
        : "API-invoer heeft nog geen geldige recente waarde. OpenQuatt gebruikt deze bron dan niet.";
      return [renderSourceRow({
        label,
        key: valueKey,
        value: valid ? value : "—",
        status: valid ? "Geldig" : "Ongeldig",
        statusTone: valid ? "valid" : "invalid",
        statusTitle,
      })];
    };
    const renderSourceGroup = ({ title, icon = "", content = "", rows = [], copy = "", className = "" }) => {
      const rowMarkup = rows.filter(Boolean).join("");
      if (!content && !rowMarkup && !copy) {
        return "";
      }
      return `
        <section class="oq-settings-source-group${className ? ` ${escapeHtml(className)}` : ""}">
          <h5>
            ${icon ? `<span class="oq-settings-source-group-icon">${renderOqIcon(icon, "oq-settings-source-group-icon-svg")}</span>` : ""}
            <span>${escapeHtml(title)}</span>
          </h5>
          ${content ? `<div class="oq-settings-source-group-content">${content}</div>` : ""}
          ${rowMarkup ? `<div class="oq-settings-source-rows">${rowMarkup}</div>` : ""}
          ${copy ? `<p class="oq-settings-source-group-copy">${escapeHtml(copy)}</p>` : ""}
        </section>
      `;
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
        isMqttOption(current) && !mqttAvailable
      ) || (
        current === "HA input" && config.keepUnavailableCurrent !== true
      );
      const renderOptions = currentHidden && !availableOptions.includes(current)
        ? [current, ...availableOptions]
        : currentUnavailable && !hideUnavailableCurrent && !availableOptions.includes(current)
        ? [current, ...availableOptions]
        : availableOptions;
      const optionMarkup = renderOptions.map((option) => {
        const displayLabel = formatSourceOptionLabel(option, config);
        return `<option value="${escapeHtml(option)}" ${option === current ? "selected" : ""}>${escapeHtml(displayLabel)}</option>`;
      }).join("");
      const unavailableCurrentPlaceholder = currentUnavailable && hideUnavailableCurrent
        ? '<option value="" selected disabled>Kies een beschikbare bron</option>'
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
        warning: currentHidden
          ? "Huidige bron is legacy; kies een nieuwe bron."
          : currentUnavailable ? `Huidige bron niet beschikbaar: ${getUnavailableSourceReason(current, config)}` : "",
      };
    };
    const renderSourceCard = ({
      key,
      title,
      icon = "",
      select,
      secondarySelect = null,
      secondarySelects = null,
      activeRows = [],
      measurementRows = [],
      rows = [],
      warning = "",
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
      const bodyRows = rows.filter(Boolean).join("");
      const controlsMarkup = `${mainSelect.markup}${secondaryMarkup}`;
      const warningCopy = mainSelect.warning || secondaryWarning || warning;
      const groupedMarkup = [
        renderSourceGroup({
          title: "Configuratie",
          icon: "settings",
          className: "oq-settings-source-group--config",
          content: controlsMarkup ? `
            <div class="oq-settings-source-controls">
              ${controlsMarkup}
            </div>
            ${warningCopy ? `<p class="oq-settings-source-warning">${escapeHtml(warningCopy)}</p>` : ""}
          ` : "",
        }),
        renderSourceGroup({ title: "Actief", icon: "target", rows: activeRows, className: "oq-settings-source-group--active" }),
        renderSourceGroup({ title: key === "water-supply" ? "Ruwe metingen" : "Metingen", icon: "activity", rows: measurementRows, className: "oq-settings-source-group--measurements" }),
      ].filter(Boolean).join("");
      if (!groupedMarkup && !controlsMarkup && !bodyRows) {
        return "";
      }
      return `
        <article class="oq-settings-source-card" data-oq-settings-field="${escapeHtml(key || select.key)}">
          <div class="oq-settings-source-card-head">
            ${icon ? `<span class="oq-settings-source-card-icon">${renderOqIcon(icon, "oq-settings-source-card-icon-svg")}</span>` : ""}
            <h4>${escapeHtml(title)}</h4>
          </div>
          ${groupedMarkup || `
            ${controlsMarkup ? `
              <div class="oq-settings-source-controls">
                ${controlsMarkup}
              </div>
            ` : ""}
            ${warningCopy ? `<p class="oq-settings-source-warning">${escapeHtml(warningCopy)}</p>` : ""}
            ${bodyRows ? `<div class="oq-settings-source-rows">${bodyRows}</div>` : ""}
          `}
        </article>
      `;
    };
    const currentWaterSupplySource = String(getEntityValue("waterSupplySource") || "");
    const currentFlowSource = String(getEntityValue("flowSource") || "");
    const currentQFlowSource = String(getEntityValue("qFlowSource") || "");
    const currentOutsideTempSource = String(getEntityValue("outsideTempSource") || "").trim();
    const waterSupplyCorrection = getWaterSupplyCorrectionView();
    const waterSupplyCalibrated = waterSupplyCorrection.calibrationActive;
    const supplyInfo = waterSupplyCalibrated
      ? "Gekalibreerd; ruwe metingen hieronder."
      : waterSupplyCorrection.calibrationRequired
        ? "Ruwe waarde; kalibreer via Service."
        : "Ruwe waarde; niet gekalibreerd.";
    const heatingEnableSourceDisabled = String(getEntityValue("heatingEnableSource") || "").trim() === "Disabled";
    const heatingEnableSourceLabel = formattedSourceValue("heatingEnableSource", { optionLabels: { Disabled: "Niet gebruiken" } });
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
    const sourceCards = [
      renderSourceCard({
        key: "room-temperature",
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
        activeRows: [
          renderSourceRow({ label: "Waarde", key: "roomTemp" }),
          renderSourceRow({ label: "Bron", value: formattedTextSourceValue("roomTempEffectiveSource") }),
        ],
        measurementRows: [
          cicAvailable ? renderSourceRow({ label: "CIC", key: "cicRoomTemp" }) : "",
          otAvailable ? renderSourceRow({ label: "OpenTherm", key: "otRoomTemp" }) : "",
          ...renderHaSourceRows({ valueKey: "roomTempHa", validKey: "roomTempHaValid" }),
          ...renderApiSourceRows({ valueKey: "apiInputRoomTemperature", validKey: "apiInputRoomTemperatureValid" }),
          ...renderMqttSourceRows({ valueKey: "mqttRoomTemperature", validKey: "mqttRoomTemperatureValid" }),
        ],
      }),
      renderSourceCard({
        key: "room-setpoint",
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
        activeRows: [
          renderSourceRow({ label: "Waarde", key: "roomSetpoint" }),
          renderSourceRow({ label: "Bron", value: formattedTextSourceValue("roomSetpointEffectiveSource") }),
        ],
        measurementRows: [
          cicAvailable ? renderSourceRow({ label: "CIC", key: "cicRoomSetpoint" }) : "",
          otAvailable ? renderSourceRow({ label: "OpenTherm", key: "otRoomSetpoint" }) : "",
          ...renderHaSourceRows({ valueKey: "roomSetpointHa", validKey: "roomSetpointHaValid" }),
          ...renderApiSourceRows({ valueKey: "apiInputRoomSetpoint", validKey: "apiInputRoomSetpointValid" }),
          ...renderMqttSourceRows({ valueKey: "mqttRoomSetpoint", validKey: "mqttRoomSetpointValid" }),
        ],
      }),
      renderSourceCard({
        key: "external-heat-demand",
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
        activeRows: [
          renderSourceRow({ label: "Waarde", key: "externalHeatDemandSelected" }),
          renderSourceRow({ label: "Power House gebruikt", value: formattedTextSourceValue("powerHouseDemandSource") }),
        ],
        measurementRows: [
          ...renderHaSourceRows({ valueKey: "externalHeatDemandHa", validKey: "externalHeatDemandHaValid" }),
          ...renderApiSourceRows({ valueKey: "apiInputExternalHeatDemand", validKey: "apiInputExternalHeatDemandValid" }),
        ],
      }),
      renderSourceCard({
        key: "water-supply",
        title: "Aanvoertemperatuur",
        icon: "droplet",
        select: { key: "waterSupplySource", label: "Bron", haKeys: ["waterSupplyTempHa", "waterSupplyTempHaValid"] },
        secondarySelect: {
          key: "localWaterSupplyTempSource",
          label: "Lokale sensor",
          when: currentWaterSupplySource === "Local" && hasEntity("localWaterSupplyTempSource"),
        },
        activeRows: [
          renderSourceRow({
            label: "Gebruikte waarde",
            key: "supplyTemp",
            status: "i",
            statusTone: waterSupplyCalibrated ? "valid" : "error",
            statusTitle: supplyInfo,
          }),
          renderSourceRow({ label: "Bron", value: getWaterSupplyUsedSource() }),
        ],
        warning: waterSupplyCorrection.calibrationRequired
          ? "De aanvoerbron of bronconfiguratie is gewijzigd. De oude correctie is uitgeschakeld; voer de temperatuurkalibratie opnieuw uit."
          : "",
        measurementRows: [
          renderSourceRow({ label: "Lokale selectie", key: "waterSupplyTempEsp" }),
          renderSourceRow({ label: "PT1000", key: "waterSupplyTempPt1000" }),
          renderSourceRow({ label: "DS18B20", key: "waterSupplyTempDs18b20" }),
          cicAvailable ? renderSourceRow({ label: "CIC", key: "cicWaterSupplyTemp" }) : "",
          ...renderHaSourceRows({ valueKey: "waterSupplyTempHa", validKey: "waterSupplyTempHaValid" }),
        ],
      }),
      renderSourceCard({
        key: "flow-source",
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
        activeRows: [
          renderSourceRow({ label: "OpenQuatt-flow", key: "flowSelected" }),
          renderSourceRow({ label: "Bron", value: getFlowUsedSource() }),
        ],
        measurementRows: [
          renderSourceRow({ label: "Controller-flowmeter", key: "controllerFlow" }),
          renderSourceRow({ label: "Gecombineerd HP1/HP2", key: "flowLocal" }),
          renderSourceRow({ label: "Flowmeter HP1", key: "hp1Flow" }),
          renderSourceRow({ label: "Flowmeter HP2", key: "hp2Flow" }),
          cicAvailable ? renderSourceRow({ label: "CIC", key: "cicFlowrate" }) : "",
        ],
      }),
      renderSourceCard({
        key: "outside-temperature",
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
        activeRows: [
          renderSourceRow({ label: "Waarde", key: "outsideTempSelected" }),
          renderSourceRow({ label: "Bron", value: getOutsideTempUsedSource() }),
        ],
        measurementRows: [
          renderSourceRow({ label: "Buitenunit", key: "outsideTempLocalAggregated" }),
          ...renderHaSourceRows({ valueKey: "outsideTempHa", validKey: "outsideTempHaValid" }),
          ...renderApiSourceRows({ valueKey: "apiInputOutsideTemperature", validKey: "apiInputOutsideTemperatureValid" }),
          ...renderMqttSourceRows({ valueKey: "mqttOutsideTemperature", validKey: "mqttOutsideTemperatureValid" }),
        ],
      }),
      renderSourceCard({
        key: "heating-enable",
        title: "Warmtetoestemming",
        icon: "flame",
        select: {
          key: "heatingEnableSource",
          label: "Bron",
          optionLabels: { Disabled: "Niet gebruiken", "API input": "API-invoer" },
          haKeys: ["heatingEnableHa", "heatingEnableHaValid"],
          apiValueKey: "apiInputHeatingEnable",
          apiValidKey: "apiInputHeatingEnableValid",
          mqttTopicKey: "heating_enable",
          keepUnavailableCurrent: true,
        },
        activeRows: [
          renderSourceRow({ label: "Toestemming", value: heatingEnableSourceDisabled ? "Niet gebruikt" : sourceStateText("heatingEnableSelected", "Toegestaan", "Geblokkeerd") }),
          !heatingEnableSourceDisabled ? renderSourceRow({ label: "Bron", value: heatingEnableSourceLabel }) : "",
        ],
        measurementRows: [
          otAvailable ? renderSourceRow({ label: "OpenTherm", value: sourceStateText("otThermostatChEnable", "Toegestaan", "Geblokkeerd") }) : "",
          cicAvailable ? renderSourceRow({ label: "CIC", value: sourceStateText("cicChEnabled", "Toegestaan", "Geblokkeerd") }) : "",
          ...renderHaSourceRows({
            valueKey: "heatingEnableHa",
            validKey: "heatingEnableHaValid",
            value: sourceStateText("heatingEnableHa", "Toegestaan", "Geblokkeerd"),
          }),
          ...renderApiSourceRows({
            valueKey: "apiInputHeatingEnable",
            validKey: "apiInputHeatingEnableValid",
            value: sourceStateText("apiInputHeatingEnable", "Toegestaan", "Geblokkeerd"),
          }),
          ...renderMqttSourceRows({
            valueKey: "mqttHeatingEnable",
            validKey: "mqttHeatingEnableValid",
            value: sourceStateText("mqttHeatingEnable", "Toegestaan", "Geblokkeerd"),
          }),
        ],
      }),
      renderSourceCard({
        key: "cooling-enable",
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
        activeRows: [
          renderSourceRow({ label: "Toestemming", value: sourceStateText("coolingEnableSelected", "Toegestaan", "Geblokkeerd") }),
          !coolingEnableSourceDisabled ? renderSourceRow({ label: "Bron", value: coolingEnableSourceLabel }) : "",
          coolingEnableEffectiveSource && coolingEnableEffectiveSource !== coolingEnableSourceLabel
            ? renderSourceRow({ label: "Via", value: coolingEnableEffectiveSource })
            : "",
        ],
        measurementRows: [
          renderSourceRow({ label: "Handmatig", value: sourceStateText("manualCoolingEnable", "Aan", "Uit") }),
          otAvailable ? renderSourceRow({ label: "OpenTherm", value: sourceStateText("otThermostatCoolingEnable", "Toegestaan", "Geblokkeerd") }) : "",
          ...renderHaSourceRows({
            valueKey: "coolingEnableHa",
            validKey: "coolingEnableHaValid",
            value: sourceStateText("coolingEnableHa", "Toegestaan", "Geblokkeerd"),
          }),
          ...renderApiSourceRows({
            valueKey: "apiInputCoolingEnable",
            validKey: "apiInputCoolingEnableValid",
            value: sourceStateText("apiInputCoolingEnable", "Toegestaan", "Geblokkeerd"),
          }),
          ...renderMqttSourceRows({
            valueKey: "mqttCoolingEnable",
            validKey: "mqttCoolingEnableValid",
            value: sourceStateText("mqttCoolingEnable", "Toegestaan", "Geblokkeerd"),
          }),
        ],
      }),
      renderSourceCard({
        key: "cooling-dew-point",
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
        activeRows: [
          renderSourceRow({ label: "Waarde", key: "coolingDewPointSelected" }),
          renderSourceRow({ label: "Bron", value: getCoolingDewPointUsedSource() }),
        ],
        measurementRows: [
          ...renderHaSourceRows({ valueKey: "coolingDewPointHa", validKey: "coolingDewPointHaValid" }),
          ...renderApiSourceRows({ valueKey: "apiInputCoolingDewPoint", validKey: "apiInputCoolingDewPointValid" }),
          ...renderMqttSourceRows({ valueKey: "mqttCoolingDewPoint", validKey: "mqttCoolingDewPointValid" }),
        ],
      }),
    ].filter(Boolean);

    if (!sourceCards.length) {
      return "";
    }

    return renderSettingsSection(
      "Bronnen",
      "Sensorselectie",
      "Kies welke bron OpenQuatt gebruikt voor metingen en vraag-signalen. Uitgeschakelde integraties verdwijnen uit de keuzes.",
      `<div class="oq-settings-source-grid">${sourceCards.join("")}</div>`,
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
