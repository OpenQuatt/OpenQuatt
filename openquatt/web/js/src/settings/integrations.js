import { getEntityNumericValue, hasEntity } from "../core/app-shared.js";
import { renderOqIcon, SENSOR_SELECTION_KEYS } from "../core/config.js";
import { getInputDraftValue } from "../core/control-drafts.js";
import { getEntityValue, getNumberMeta } from "../core/entity-store.js";
import { renderNumberInputControl } from "../core/number-controls.js";
import { getHeatingEnableAdvice } from "../core/heating-strategy-matrix.js";
import { isInstallationMonitoringBinaryActive, isInstallationMonitoringIntegrationEnabled } from "../core/installation-monitoring.js";
import { state } from "../core/state.js";
import { formatMqttSensorValiditySummary, getMqttStatusDetail, getMqttStatusLabel, getMqttValidityLabel } from "../features/mqtt.js";
import { formatSettingsOptionLabel, getSelectEntityOptions, getSettingsStatValue, getSettingsTextStatValue, renderSettingsInfoToggle, renderSettingsIntegrationSwitchCard, renderSettingsSection } from "./controls.js";
import { getWaterSupplyCorrectionView } from "./water.js";
import { escapeHtml } from "../core/html.js";
import { formatNumber, optionLabel, t } from "../i18n/index.js";

  export function renderSettingsOpenThermCicSection() {
    const hasOpenThermConfig = hasEntity("otEnabled");
    const hasCicConfig = hasEntity("cicPollingEnabled") || hasEntity("cicFeedUrl");
    const hasCicCompatibilityConfig = hasEntity("cicCompatibilityMode");
    const hasStatus = hasEntity("otLinkProblem") || hasEntity("otbLinkAvailable") || hasEntity("otbConnectionState") || hasEntity("boilerCommandValid") || hasEntity("cicDataStale") || hasEntity("cicJsonFeedOk");
    if (!hasOpenThermConfig && !hasCicConfig && !hasCicCompatibilityConfig && !hasStatus) {
      return "";
    }

    const cicPollingEnabled = isInstallationMonitoringIntegrationEnabled("cicPollingEnabled");
    const otEnabled = isInstallationMonitoringIntegrationEnabled("otEnabled");
    const boilerConnection = String(getEntityValue("boilerConnection") || "R1");
    const otbSelected = boilerConnection === "OpenTherm";
    const otbConnectionState = String(getEntityValue("otbConnectionState") || "");
    const otbConnectionProblem = otbSelected && ["ot_no_response", "ot_link_lost"].includes(otbConnectionState);
    const renderDiagnosticItem = ({ label, value, active = false }) => `
      <div class="oq-settings-integration-diagnostic-item${active ? " is-warning" : ""}">
        <dt>${escapeHtml(label)}</dt>
        <dd>${escapeHtml(value)}</dd>
      </div>
    `;

    const renderBinaryDiagnosticItem = (key, label, activeLabel = null, inactiveLabel = null, options = {}) => {
      if (!hasEntity(key)) {
        return "";
      }
      const active = isInstallationMonitoringBinaryActive(key);
      return renderDiagnosticItem({
        label,
        value: active ? (activeLabel ?? t("settingsIntegrations.srcActive")) : (inactiveLabel ?? t("settingsIntegrations.srcNormal")),
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
      <details class="oq-settings-cic-address"${state.cicAddressOpen ? " open" : ""}>
        <summary data-oq-action="toggle-cic-address">${escapeHtml(t("settingsIntegrations.cicAddressToggle"))}</summary>
      <article class="oq-settings-integration-card oq-settings-integration-card--wide oq-settings-cic-feed-url" data-oq-settings-field="cicFeedUrl">
        <div class="oq-settings-integration-card-head">
          <h4>${escapeHtml(t("settingsIntegrations.cicAddressTitle"))}</h4>
        </div>
        <label class="oq-settings-control oq-settings-control--text">
          <input
            class="oq-helper-input oq-settings-integration-url-input"
            type="url"
            aria-label="${escapeHtml(t("settingsIntegrations.cicAddressTitle"))}"
            data-oq-field="cicFeedUrl"
            value="${escapeHtml(String(getInputDraftValue("cicFeedUrl") || ""))}"
            placeholder="${escapeHtml(t("settingsIntegrations.cicAddressPlaceholder"))}"
            autocomplete="off"
            spellcheck="false"
            ${state.loadingEntities ? "disabled" : ""}
          >
        </label>
        <p>${escapeHtml(t("settingsIntegrations.cicAddressNote"))}</p>
      </article>
      </details>
    ` : "";

    const otDiagnosticPanel = renderDiagnosticGroup(t("settingsIntegrations.diagGroupOt"), [
      hasEntity("otLinkProblem") ? renderDiagnosticItem({
        label: t("settingsIntegrations.diagOtLink"),
        value: !otEnabled
          ? t("settingsIntegrations.diagOff")
          : isInstallationMonitoringBinaryActive("otLinkProblem") ? t("settingsIntegrations.diagProblem") : t("settingsIntegrations.diagOk"),
        active: otEnabled && isInstallationMonitoringBinaryActive("otLinkProblem"),
      }) : "",
      renderBinaryDiagnosticItem("otThermostatStatusValid", t("settingsIntegrations.diagOtStatus"), t("settingsIntegrations.diagYes"), t("settingsIntegrations.diagNo")),
      renderBinaryDiagnosticItem("otThermostatChEnable", t("settingsIntegrations.diagOtCh"), t("settingsIntegrations.diagActive"), t("settingsIntegrations.diagNormal")),
      renderBinaryDiagnosticItem("otThermostatDhwEnable", t("settingsIntegrations.diagOtDhw"), t("settingsIntegrations.diagAllowed"), t("settingsIntegrations.diagBlocked")),
      renderBinaryDiagnosticItem("otThermostatCoolingEnable", t("settingsIntegrations.diagOtCool"), t("settingsIntegrations.diagActive"), t("settingsIntegrations.diagNormal")),
      renderValueDiagnosticItem("otControlSetpoint", t("settingsIntegrations.diagOtControl")),
      renderValueDiagnosticItem("otRoomSetpoint", t("settingsIntegrations.diagOtRoomSetpoint"), { fallbackKey: "roomSetpoint" }),
      renderValueDiagnosticItem("otRoomTemp", t("settingsIntegrations.diagOtRoomTemp"), { fallbackKey: "roomTemp" }),
    ]);

    const boilerDiagnosticPanel = renderDiagnosticGroup(t("settingsIntegrations.diagGroupBoiler"), [
      hasEntity("boilerConnection") ? renderDiagnosticItem({
        label: t("settingsIntegrations.diagBoilerConnection"),
        value: otbSelected ? t("settingsInstallation.boilerConnectionOt") : t("settingsInstallation.boilerConnectionR1"),
      }) : "",
      renderBinaryDiagnosticItem("boilerCommandValid", t("settingsIntegrations.diagBoilerCmdValid"), t("settingsIntegrations.diagYes"), t("settingsIntegrations.diagNo"), { warningWhenInactive: true }),
      renderBinaryDiagnosticItem("boilerCommandActive", t("settingsIntegrations.diagBoilerCmdActive"), t("settingsIntegrations.diagActive"), t("settingsIntegrations.diagOffValue")),
      renderValueDiagnosticItem("boilerCommandSource", t("settingsIntegrations.diagBoilerCmdSource")),
      renderValueDiagnosticItem("boilerCommandTargetTemperature", t("settingsIntegrations.diagBoilerTarget")),
      renderValueDiagnosticItem("boilerCommandRequestedPower", t("settingsIntegrations.diagBoilerPower")),
      renderValueDiagnosticItem("boilerCommandAge", t("settingsIntegrations.diagBoilerAge")),
      renderValueDiagnosticItem("boilerBlockReason", t("settingsIntegrations.diagBoilerBlock")),
      renderValueDiagnosticItem("boilerStartThermalGuard", t("settingsIntegrations.diagBoilerWarmDecision")),
      renderValueDiagnosticItem("boilerStartThermalSafeCeiling", t("settingsIntegrations.diagBoilerWarmCeiling")),
    ]);

    const otbDiagnosticRows = [
      otbSelected && hasEntity("otbConnectionState") ? renderDiagnosticItem({
        label: t("settingsIntegrations.diagOtbPhysical"),
        value: otbConnectionState === "ot_verified"
          ? t("settingsIntegrations.diagOtbVerified")
          : otbConnectionState === "ot_no_response"
            ? t("settingsIntegrations.diagOtbNoResponse")
            : otbConnectionState === "ot_link_lost" ? t("settingsIntegrations.diagOtbLost") : t("settingsIntegrations.diagOtbChecking"),
        active: otbConnectionProblem,
      }) : "",
      hasEntity("otbLinkAvailable") ? renderDiagnosticItem({
        label: t("settingsIntegrations.diagOtbLink"),
        value: !otbSelected
          ? t("settingsIntegrations.diagOtbNotSelected")
          : isInstallationMonitoringBinaryActive("otbLinkAvailable") ? t("settingsIntegrations.diagOk") : t("header.connNotConnected"),
        active: otbSelected && !isInstallationMonitoringBinaryActive("otbLinkAvailable"),
      }) : "",
    ];
    if (otbSelected) {
      otbDiagnosticRows.push(
        renderBinaryDiagnosticItem("otbChCommand", t("settingsIntegrations.diagOtbChCmd"), t("settingsIntegrations.diagActive"), t("settingsIntegrations.diagOffValue")),
        renderValueDiagnosticItem("otbControlSetpointCommand", t("settingsIntegrations.diagOtbTSet")),
        renderBinaryDiagnosticItem("otbChActive", t("settingsIntegrations.diagOtbChActive"), t("settingsIntegrations.diagActive"), t("settingsIntegrations.diagOffValue")),
        renderBinaryDiagnosticItem("otbFlameOn", t("settingsIntegrations.diagOtbFlame"), t("settingsIntegrations.diagOn"), t("settingsIntegrations.diagOffValue")),
        renderBinaryDiagnosticItem("otbDhwActive", t("settingsIntegrations.diagOtbDhw"), t("settingsIntegrations.diagActive"), t("settingsIntegrations.diagOffValue")),
        renderValueDiagnosticItem("otbRelativeModulation", t("settingsIntegrations.diagOtbMod")),
        renderValueDiagnosticItem("otbChPressure", t("settingsIntegrations.diagOtbPressure")),
        renderValueDiagnosticItem("otbBoilerWaterTemp", t("settingsIntegrations.diagOtbBoilerTemp")),
        renderValueDiagnosticItem("otbReturnWaterTemp", t("settingsIntegrations.diagOtbReturnTemp")),
        renderValueDiagnosticItem("otbDhwTemp", t("settingsIntegrations.diagOtbDhwTemp")),
        renderBinaryDiagnosticItem("otbFaultIndication", t("settingsIntegrations.diagOtbFault"), t("settingsIntegrations.diagActive"), t("settingsIntegrations.diagNone"), { warningWhenActive: true }),
        renderBinaryDiagnosticItem("otbDiagnosticIndication", t("settingsIntegrations.diagOtbDiag"), t("settingsIntegrations.diagActive"), t("settingsIntegrations.diagNone"), { warningWhenActive: true }),
        renderBinaryDiagnosticItem("otbServiceRequest", t("settingsIntegrations.diagOtbService"), t("settingsIntegrations.diagYes"), t("settingsIntegrations.diagNo"), { warningWhenActive: true }),
        renderBinaryDiagnosticItem("otbLowWaterPressure", t("settingsIntegrations.diagOtbLowPressure"), t("settingsIntegrations.diagYes"), t("settingsIntegrations.diagNo"), { warningWhenActive: true }),
        renderBinaryDiagnosticItem("otbFlameFault", t("settingsIntegrations.diagOtbFlameFault"), t("settingsIntegrations.diagYes"), t("settingsIntegrations.diagNo"), { warningWhenActive: true }),
        renderBinaryDiagnosticItem("otbAirPressureFault", t("settingsIntegrations.diagOtbAirFault"), t("settingsIntegrations.diagYes"), t("settingsIntegrations.diagNo"), { warningWhenActive: true }),
        renderBinaryDiagnosticItem("otbWaterOverTemp", t("settingsIntegrations.diagOtbOverTemp"), t("settingsIntegrations.diagYes"), t("settingsIntegrations.diagNo"), { warningWhenActive: true }),
        renderValueDiagnosticItem("otbOemFaultCode", t("settingsIntegrations.diagOtbOemFault")),
        renderValueDiagnosticItem("otbOemDiagnosticCode", t("settingsIntegrations.diagOtbOemDiag")),
        renderValueDiagnosticItem("otbLastResponseAge", t("settingsIntegrations.diagOtbLastAge")),
        renderValueDiagnosticItem("otbLastResponseId", t("settingsIntegrations.diagOtbLastId")),
        renderValueDiagnosticItem("otbResponseCount", t("settingsIntegrations.diagOtbResponses")),
      );
    }
    const otbDiagnosticPanel = renderDiagnosticGroup(t("settingsIntegrations.diagGroupOtb"), otbDiagnosticRows);

    const cicDiagnosticPanel = renderDiagnosticGroup(t("settingsIntegrations.diagGroupCic"), [
      hasEntity("cicJsonFeedOk") ? renderDiagnosticItem({
        label: t("settingsIntegrations.diagCicJson"),
        value: !cicPollingEnabled
          ? t("settingsIntegrations.diagCicPollingOff")
          : isInstallationMonitoringBinaryActive("cicJsonFeedOk") ? t("settingsIntegrations.diagOk") : t("settingsIntegrations.diagProblem"),
        active: cicPollingEnabled && !isInstallationMonitoringBinaryActive("cicJsonFeedOk"),
      }) : "",
      hasEntity("cicDataStale") ? renderDiagnosticItem({
        label: t("settingsIntegrations.diagCicData"),
        value: !cicPollingEnabled
          ? t("settingsIntegrations.diagCicPollingOff")
          : isInstallationMonitoringBinaryActive("cicDataStale") ? t("settingsIntegrations.diagCicStale") : t("settingsIntegrations.diagCicCurrent"),
        active: cicPollingEnabled && isInstallationMonitoringBinaryActive("cicDataStale"),
      }) : "",
      renderBinaryDiagnosticItem("cicChEnabled", t("settingsIntegrations.diagCicCh"), t("settingsIntegrations.diagActive"), t("settingsIntegrations.diagNormal")),
      renderBinaryDiagnosticItem("cicCoolingEnabled", t("settingsIntegrations.diagCicCooling"), t("settingsIntegrations.diagActive"), t("settingsIntegrations.diagNormal")),
      renderValueDiagnosticItem("cicBoilerWaterPressure", t("settingsIntegrations.diagOtbPressure")),
      renderValueDiagnosticItem("cicControlSetpoint", t("settingsIntegrations.diagOtControl")),
      renderValueDiagnosticItem("cicRoomSetpoint", t("settingsIntegrations.diagOtRoomSetpoint")),
      renderValueDiagnosticItem("cicRoomTemp", t("settingsIntegrations.diagOtRoomTemp")),
      renderValueDiagnosticItem("cicFlowrate", t("settingsIntegrations.diagCicFlow")),
      renderValueDiagnosticItem("cicLastSuccessAge", t("settingsIntegrations.diagCicLastSuccess")),
    ]);

    const diagnosticsPanel = otDiagnosticPanel || boilerDiagnosticPanel || otbDiagnosticPanel || cicDiagnosticPanel ? `
      <details class="oq-settings-integration-diagnostics"${state.integrationDiagnosticsOpen ? " open" : ""}>
        <summary data-oq-action="toggle-integration-diagnostics">
          <strong>${escapeHtml(t("settingsIntegrations.diagTitle"))}</strong>
          <span>${escapeHtml(t("settingsIntegrations.diagSub"))}</span>
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
      t("settingsIntegrations.groupIntegrations"),
      t("settingsIntegrations.otTitle"),
      t("settingsIntegrations.otCopy"),
      `
        <div class="oq-settings-cic-connections">
          ${renderSettingsIntegrationSwitchCard("otEnabled", t("settingsIntegrations.otSwitchTitle"), t("settingsIntegrations.otSwitchCopy"), t("settingsIntegrations.otSwitchNote"))}
          ${hasCicConfig ? `
            <div class="oq-settings-cic-input">
              ${renderSettingsIntegrationSwitchCard("cicPollingEnabled", t("settingsIntegrations.cicSwitchTitle"), t("settingsIntegrations.cicSwitchCopy"), t("settingsIntegrations.cicSwitchNote"))}
              ${urlField}
            </div>
          ` : ""}
          ${renderSettingsIntegrationSwitchCard("cicCompatibilityMode", t("settingsIntegrations.cicCompatTitle"), t("settingsIntegrations.cicCompatCopy"), t("settingsIntegrations.cicCompatNote"))}
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
      mqttHeatingSupplyTarget: "heating_supply_target",
      mqttHeatingEnable: "heating_enable",
      mqttCoolingEnable: "cooling_enable",
    };
    const mqttValidKeyByTopicKey = {
      cooling_dew_point: "mqttCoolingDewPointValid",
      outside_temperature: "mqttOutsideTemperatureValid",
      room_temperature: "mqttRoomTemperatureValid",
      room_setpoint: "mqttRoomSetpointValid",
      heating_supply_target: "mqttHeatingSupplyTargetValid",
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
        return t("settingsIntegrations.mqttOff");
      }
      return isMqttInputTopicEnabled(topicKey) ? "" : t("settingsIntegrations.mqttTopicOff");
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
        return t("settingsIntegrations.unavCicOff");
      }
      if (option === "OT thermostat" && !otAvailable) {
        return t("settingsIntegrations.unavOtOff");
      }
      if (isHaInputOption(option) && !hasHaSource(config)) {
        return t("settingsIntegrations.unavHaInvalid");
      }
      if (option === "CIC or HA input" && !cicAvailable && !hasHaSource(config)) {
        return t("settingsIntegrations.unavCicHaMissing");
      }
      if (isApiInputOption(option) && !hasApiInputSource(config)) {
        return t("settingsIntegrations.unavApiMissing");
      }
      if (isMqttOption(option)) {
        return getMqttUnavailableSourceReason(getMqttTopicKey(config));
      }
      if (option === "Flowmeter HP2" && !hasEntity("hp2Flow")) {
        return t("settingsIntegrations.unavHp2Flow");
      }
      if (option === "Local aggregate HP1/HP2" && !hasEntity("flowLocal") && !hasEntity("hp2Flow")) {
        return t("settingsIntegrations.unavLocalFlow");
      }
      return "";
    };
    const sourceStateText = (key, activeLabel = null, inactiveLabel = null) => {
      if (!hasEntity(key)) {
        return "";
      }
      return isInstallationMonitoringBinaryActive(key)
        ? (activeLabel ?? t("settingsIntegrations.srcActive"))
        : (inactiveLabel ?? t("settingsIntegrations.srcNormal"));
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
      : t("settingsIntegrations.srcInvalidValue");
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
            return qSource === "Auto" ? t("settingsIntegrations.flowLocalAuto") : optionLabel("Local");
          }
          return firstAvailableSourceLabel(formattedSourceValue("outdoorUnitFlowMode"), qSource === "Auto" ? t("settingsIntegrations.flowOutdoorAuto") : optionLabel("Outdoor unit"));
        }
        return firstAvailableSourceLabel(formattedSourceValue("outdoorUnitFlowMode"), t("settingsIntegrations.flowQuatt"));
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
        unitValid ? { label: optionLabel("Outdoor unit"), value: unitTemp } : null,
        haValid ? { label: optionLabel("HA input"), value: haTemp } : null,
        apiValid ? { label: optionLabel("API input"), value: apiTemp } : null,
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
        return isValidNumericSource("coolingDewPointHa", "coolingDewPointHaValid") ? optionLabel("HA input") : t("settingsIntegrations.dewHaMissing");
      }
      if (source === "API input") {
        return isValidNumericSource("apiInputCoolingDewPoint", "apiInputCoolingDewPointValid") ? optionLabel("API input") : t("settingsIntegrations.dewApiMissing");
      }
      if (source === "MQTT") {
        const mqttUnavailableReason = getMqttUnavailableSourceReason("cooling_dew_point");
        if (mqttUnavailableReason) {
          return mqttUnavailableReason;
        }
        return isValidNumericSource("mqttCoolingDewPoint", "mqttCoolingDewPointValid") ? "MQTT" : t("settingsIntegrations.dewMqttMissing");
      }

      const haValid = isValidNumericSource("coolingDewPointHa", "coolingDewPointHaValid");
      const apiValid = isValidNumericSource("apiInputCoolingDewPoint", "apiInputCoolingDewPointValid");
      const mqttValid = isMqttInputTopicEnabled("cooling_dew_point") &&
        isValidNumericSource("mqttCoolingDewPoint", "mqttCoolingDewPointValid");
      const candidates = [
        haValid ? { label: optionLabel("HA input"), value: getNumericSourceValue("coolingDewPointHa") } : null,
        apiValid ? { label: optionLabel("API input"), value: getNumericSourceValue("apiInputCoolingDewPoint") } : null,
        mqttValid ? { label: "MQTT", value: getNumericSourceValue("mqttCoolingDewPoint") } : null,
      ].filter((candidate) => candidate && Number.isFinite(candidate.value));
      if (candidates.length) {
        return candidates.reduce((best, item) => (item.value > best.value ? item : best), candidates[0]).label;
      }
      return source ? formatSettingsOptionLabel(source) : "Auto";
    };
    const sourceKindPatterns = [
      ["ha", /\b(?:ha|home assistant)\b/],
      ["api", /\bapi\b/],
      ["mqtt", /\bmqtt\b/],
      ["cic", /\bcic\b/],
      ["ot", /\b(?:opentherm|ot thermostat|ot-thermostaat)\b/],
      ["outdoor", /\b(?:outdoor unit|buitenunit|quatt-flow)\b/],
      ["local", /\b(?:local|lokaal|controller)\b/],
      ["pt1000", /\bpt1000\b/],
      ["ds18b20", /\bds18b20\b/],
      ["manual", /\b(?:manual|handmatig)\b/],
      ["hp1", /\bhp1\b/],
      ["hp2", /\bhp2\b/],
      ["disabled", /\b(?:disabled|niet gebruiken|none)\b|^—$|^$/],
    ];
    const sourceKinds = (value = "") => {
      const text = String(value || "").trim().toLowerCase();
      const kinds = new Set(sourceKindPatterns.filter(([, pattern]) => pattern.test(text)).map(([kind]) => kind));
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
          class="oq-settings-source-row${effective ? " is-effective" : ""}"
          ${safeSourceKind ? `data-source-kind="${escapeHtml(safeSourceKind)}"` : ""}
          ${safeSourceState ? `data-source-state="${escapeHtml(safeSourceState)}"` : ""}
          ${effective ? 'data-source-effective="true"' : ""}
        >
          <div class="oq-settings-source-row-label">${escapeHtml(label)}${statusMarkup}</div>
          <strong>${escapeHtml(text)}</strong>
        </div>
      `;
    };
    const inputSourceCopy = {
      ha: {
        label: optionLabel("HA input"),
        valid: [t("settingsIntegrations.haValid"), t("settingsIntegrations.haValidCopy")],
        invalid: [t("settingsIntegrations.haInvalid"), t("settingsIntegrations.haInvalidCopy")],
      },
      mqtt: {
        label: "MQTT",
        valid: [t("settingsIntegrations.mqttValid"), t("settingsIntegrations.mqttValidCopy")],
        invalid: [t("settingsIntegrations.mqttInvalid"), t("settingsIntegrations.mqttInvalidCopy")],
      },
      api: {
        label: optionLabel("API input"),
        valid: [t("settingsIntegrations.apiValid"), t("settingsIntegrations.apiValidCopy")],
        stale: [t("settingsIntegrations.apiStale"), t("settingsIntegrations.apiStaleCopy")],
        missing: [t("settingsIntegrations.apiMissing"), t("settingsIntegrations.apiMissingCopy")],
      },
    };
    const renderInputSourceRows = ({ kind, label = "", valueKey = "", validKey = "", ageKey = "", value = "", topicKey = "", forceVisible = false, effective = false }) => {
      if (!valueKey || !validKey || !hasEntity(valueKey) || !hasEntity(validKey)) {
        return [];
      }
      if (kind === "mqtt" && !isMqttInputTopicEnabled(topicKey || mqttTopicKeyByValueKey[valueKey])) {
        return [];
      }
      const valid = isInstallationMonitoringBinaryActive(validKey);
      if (!valid && !forceVisible && !effective) {
        return [];
      }
      const age = ageKey && hasEntity(ageKey) ? getNumericSourceValue(ageKey) : NaN;
      const sourceState = valid ? "valid" : kind === "api" ? (Number.isFinite(age) ? "stale" : "missing") : "invalid";
      const [defaultStatus, statusTitle] = inputSourceCopy[kind][sourceState];
      return [renderSourceRow({
        label: label || inputSourceCopy[kind].label,
        key: valueKey,
        value: valid ? value : "—",
        status: kind === "mqtt" ? getMqttValidityLabel(validKey) : defaultStatus,
        statusTone: valid ? "valid" : "invalid",
        statusTitle,
        sourceKind: kind,
        sourceState,
        effective,
      })];
    };
    const inputOptionByKind = { ha: "HA input", api: "API input", mqtt: "MQTT" };
    const renderExternalSourceRows = (selectKey, effectiveSource, sources, formatValue = null) => (
      Object.entries(sources).flatMap(([kind, keys]) => {
        const [valueKey, validKey, ageKey = "", topicKey = ""] = keys;
        const option = inputOptionByKind[kind];
        return renderInputSourceRows({
          kind,
          valueKey,
          validKey,
          ageKey,
          topicKey,
          value: formatValue ? formatValue(valueKey) : "",
          forceVisible: isConfiguredSource(selectKey, option),
          effective: sourcesMatch(effectiveSource, option),
        });
      })
    );
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
      const keepCurrent = (currentHidden || currentUnavailable) && !hideUnavailableCurrent && !availableOptions.includes(current);
      const renderOptions = keepCurrent ? [current, ...availableOptions] : availableOptions;
      const optionMarkup = renderOptions.map((option) => {
        const displayLabel = formatSourceOptionLabel(option, config);
        return `<option value="${escapeHtml(option)}" ${option === current ? "selected" : ""}>${escapeHtml(displayLabel)}</option>`;
      }).join("");
      const unavailableCurrentPlaceholder = currentUnavailable && hideUnavailableCurrent
        ? `<option value="${escapeHtml(current)}" selected disabled>${escapeHtml(t("settingsIntegrations.chooseSourcePlaceholder"))}</option>`
        : "";
      return {
        markup: `
          <label class="oq-settings-source-select">
            <span class="oq-settings-source-select-head">
              <span>${escapeHtml(config.label || t("settingsIntegrations.defaultSourceLabel"))}</span>
              ${config.infoCopy ? renderSettingsInfoToggle(config.infoId || key, config.infoTitle || config.label || t("settingsIntegrations.defaultSourceLabel"), config.infoCopy) : ""}
            </span>
            <select class="oq-helper-select" data-oq-field="${escapeHtml(key)}" ${state.loadingEntities ? "disabled" : ""}>
              ${unavailableCurrentPlaceholder}${optionMarkup}
            </select>
          </label>
        `,
        warning: currentHidden && currentUnavailable
          ? t("settingsIntegrations.legacyHidden", { reason: getUnavailableSourceReason(current, config) })
          : currentHidden
          ? t("settingsIntegrations.legacyCurrent")
          : currentUnavailable ? t("settingsIntegrations.currentUnavailable", { reason: getUnavailableSourceReason(current, config) }) : "",
      };
    };
    const renderSourceNumber = (key, config = {}) => {
      if (!hasEntity(key)) return "";
      const meta = getNumberMeta(key);
      return `
        <label class="oq-settings-source-select">
          <span class="oq-settings-source-select-head">
            <span>${escapeHtml(config.label || t("settingsIntegrations.defaultValueLabel"))}</span>
            ${config.infoCopy ? renderSettingsInfoToggle(config.infoId || key, config.infoTitle || config.label || t("settingsIntegrations.defaultValueLabel"), config.infoCopy) : ""}
          </span>
          ${renderNumberInputControl({
            key,
            value: getInputDraftValue(key),
            meta,
            controlClass: "oq-helper-control oq-helper-control--suffix",
            unitMarkup: meta.uom ? `<span class="oq-helper-unit-chip">${escapeHtml(meta.uom)}</span>` : "",
          })}
        </label>
      `;
    };
    const buildExternalSourceSelect = (stem, externalStem, mqttTopicKey = "", extra = {}) => ({
      key: `${stem}Source`,
      label: t("settingsIntegrations.selSource"),
      haKeys: [`${stem}Ha`, `${stem}HaValid`],
      apiValueKey: `apiInput${externalStem}`,
      apiValidKey: `apiInput${externalStem}Valid`,
      mqttTopicKey,
      ...extra,
    });
    const buildExternalSourceKeys = (stem, externalStem, includeMqtt = true) => ({
      ha: [`${stem}Ha`, `${stem}HaValid`],
      api: [`apiInput${externalStem}`, `apiInput${externalStem}Valid`, `apiInput${externalStem}Age`],
      ...(includeMqtt ? { mqtt: [`mqtt${externalStem}`, `mqtt${externalStem}Valid`] } : {}),
    });
    const buildSourceSignal = ({
      key,
      group,
      title,
      icon = "",
      select,
      secondarySelects = [],
      secondaryNumbers = [],
      summaryValue = "",
      summarySource = "",
      summaryInfo = "",
      measurementRows = [],
      measurementTitle = null,
      warning = "",
      routeWarning = "",
    }) => {
      const mainSelect = select && select.when !== false
        ? renderSourceSelect(select.key, select)
        : { markup: "", warning: "" };
      const secondaries = secondarySelects
        .filter((config) => config && config.when !== false)
        .map((config) => renderSourceSelect(config.key, config))
        .filter((item) => item.markup);
      const secondaryMarkup = secondaries.map((item) => item.markup).join("");
      const secondaryWarning = secondaries.map((item) => item.warning).find(Boolean) || "";
      const secondaryNumberMarkup = secondaryNumbers
        .filter((config) => config && config.when !== false)
        .map((config) => renderSourceNumber(config.key, config))
        .join("");
      const controlsMarkup = `${mainSelect.markup}${secondaryMarkup}${secondaryNumberMarkup}`;
      const current = select?.key ? String(getEntityValue(select.key) || "") : "";
      const mqttValidKey = mqttValidKeyByTopicKey[getMqttTopicKey(select || {})] || "";
      const selectedInputWarning = isApiInputOption(current) && select?.apiValidKey
        && (!hasEntity(select.apiValidKey) || !isInstallationMonitoringBinaryActive(select.apiValidKey))
          ? t("settingsIntegrations.apiNoValue")
          : isMqttOption(current) && mqttValidKey
            && (!hasEntity(mqttValidKey) || !isInstallationMonitoringBinaryActive(mqttValidKey))
              ? t("settingsIntegrations.mqttNoValue")
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
        measurementTitle: measurementTitle ?? t("settingsIntegrations.measureTitle"),
      };
    };
    const currentWaterSupplySource = String(getEntityValue("waterSupplySource") || "");
    const currentLocalWaterSupplySource = String(getEntityValue("localWaterSupplyTempSource") || "");
    const currentFlowSource = String(getEntityValue("flowSource") || "");
    const currentQFlowSource = String(getEntityValue("qFlowSource") || "");
    const currentControllerFlowMeter = String(getEntityValue("controllerFlowMeter") || "");
    const currentOutsideTempSource = String(getEntityValue("outsideTempSource") || "").trim();
    const waterSupplyCorrection = getWaterSupplyCorrectionView();
    const waterSupplyCalibrated = waterSupplyCorrection.calibrationActive;
    const localWaterSupplyWarning = currentWaterSupplySource === "Local" && currentLocalWaterSupplySource === "PT1000"
      && (isInstallationMonitoringBinaryActive("pt1000ReadProblem") || !hasUsableSourceValue("waterSupplyTempPt1000"))
        ? t("settingsIntegrations.pt1000Warn")
        : currentWaterSupplySource === "Local" && currentLocalWaterSupplySource === "DS18B20"
          && !hasUsableSourceValue("waterSupplyTempDs18b20")
            ? t("settingsIntegrations.dsWarn")
            : "";
    const supplyInfo = waterSupplyCalibrated
      ? t("settingsIntegrations.supplyCalibrated")
      : waterSupplyCorrection.calibrationRequired
        ? t("settingsIntegrations.supplyRawCalibrate")
        : t("settingsIntegrations.supplyRawUncal");
    const heatingEnableSourceDisabled = String(getEntityValue("heatingEnableSource") || "").trim() === "Disabled";
    const heatingEnableSourceLabel = formattedSourceValue("heatingEnableSource");
    const heatingEnableEffectiveSource = formattedEffectivePermissionSourceValue("heatingEnableEffectiveSource");
    const coolingEnableSourceDisabled = String(getEntityValue("coolingEnableSource") || "").trim() === "Disabled";
    const coolingEnableSourceLabels = {
      Disabled: t("settingsIntegrations.optCoolDisabledLegacy"),
      CIC: t("settingsIntegrations.optCoolCicLegacy"),
      "CIC or HA input": t("settingsIntegrations.optCoolCicHaLegacy"),
    };
    const coolingEnableSourceLabel = formattedSourceValue("coolingEnableSource", { optionLabels: coolingEnableSourceLabels });
    const coolingEnableEffectiveSource = formattedEffectivePermissionSourceValue("coolingEnableEffectiveSource");
    const outsideTemperatureAutoInfo = mqttAvailable
      ? hasValidHaSource("outsideTempHa", "outsideTempHaValid")
        ? t("settingsIntegrations.autoOutsideFull")
        : t("settingsIntegrations.autoOutsideShort")
      : hasValidHaSource("outsideTempHa", "outsideTempHaValid")
        ? t("settingsIntegrations.autoOutsideNoMqtt")
        : t("settingsIntegrations.autoOutsideShort");
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
      coolingEnableSourceDisabled ? t("settingsIntegrations.coolManual") : coolingEnableSourceLabel,
    );
    const coolingDewPointUsedSource = getCoolingDewPointUsedSource();
    const externalHeatDemandConfiguredSource = formattedSourceValue("externalHeatDemandSource");
    const powerHouseDemandSource = String(getSettingsTextStatValue("powerHouseDemandSource", "") || "").trim().toLowerCase();
    const externalHeatDemandUsedSource = powerHouseDemandSource === "external"
      ? externalHeatDemandConfiguredSource
      : powerHouseDemandSource === "model" ? t("settingsIntegrations.homeModel") : "—";
    const heatingSupplyTargetConfiguredSource = formattedSourceValue("heatingSupplyTargetSource", {
      optionLabels: { "Heating curve": t("settingsIntegrations.optHeatingCurve"), "OT thermostat": t("settingsIntegrations.optOtThermostat") },
    });
    const heatingSupplyTargetActiveSource = String(getSettingsTextStatValue("heatingSupplyTargetActiveSource", "") || "").trim().toLowerCase();
    const heatingSupplyTargetIsExternal = heatingSupplyTargetActiveSource === "external";
    const heatingSupplyTargetUsedSource = heatingSupplyTargetIsExternal
      ? heatingSupplyTargetConfiguredSource
      : heatingSupplyTargetActiveSource === "curve" ? t("settingsIntegrations.optHeatingCurve") : "—";
    const heatingSupplyTargetSummaryValue = heatingSupplyTargetIsExternal
      ? getSettingsStatValue("heatingSupplyTargetSelected")
      : getSettingsStatValue("curveSupplyTarget");
    const buildRoomSignal = ({ key, title, icon, stem, externalStem, mqttTopic, usedSource }) => {
      const entityStem = `${stem[0].toUpperCase()}${stem.slice(1)}`;
      return buildSourceSignal({
        key,
        group: "room-outside",
        title,
        icon,
        select: buildExternalSourceSelect(stem, externalStem, mqttTopic),
        summaryValue: getSettingsStatValue(stem),
        summarySource: usedSource,
        routeWarning: invalidSourceValueWarning(stem),
        measurementRows: [
          cicAvailable ? renderSourceRow({ label: "CIC", key: `cic${entityStem}`, sourceKind: "cic", sourceState: "available", effective: sourcesMatch(usedSource, "CIC") }) : "",
          otAvailable ? renderSourceRow({ label: "OpenTherm", key: `ot${entityStem}`, sourceKind: "ot", sourceState: "available", effective: sourcesMatch(usedSource, "OpenTherm") }) : "",
          ...renderExternalSourceRows(`${stem}Source`, usedSource, buildExternalSourceKeys(stem, externalStem)),
        ],
      });
    };
    const buildPermissionSignal = ({ mode, title, icon, usedSource, optionLabels, selectExtra = {}, summaryValue, routeWarning, measurementRows }) => {
      const stem = `${mode}Enable`;
      const externalStem = `${mode[0].toUpperCase()}${mode.slice(1)}Enable`;
      return buildSourceSignal({
        key: `${mode}-enable`,
        group: mode,
        title,
        icon,
        select: buildExternalSourceSelect(stem, externalStem, `${mode}_enable`, {
          optionLabels,
          keepUnavailableCurrent: true,
          ...selectExtra,
        }),
        summaryValue,
        summarySource: usedSource,
        routeWarning,
        measurementRows: [
          ...measurementRows,
          ...renderExternalSourceRows(`${stem}Source`, usedSource, buildExternalSourceKeys(stem, externalStem), (key) => sourceStateText(key, t("settingsIntegrations.diagAllowed"), t("settingsIntegrations.diagBlocked"))),
        ],
      });
    };
    const sourceSignals = [
      buildRoomSignal({
        key: "room-temperature",
        title: t("settingsIntegrations.sigRoomTemp"),
        icon: "thermometer",
        stem: "roomTemp",
        externalStem: "RoomTemperature",
        mqttTopic: "room_temperature",
        usedSource: roomTemperatureUsedSource,
      }),
      buildRoomSignal({
        key: "room-setpoint",
        title: t("settingsIntegrations.sigRoomSetpoint"),
        icon: "target",
        stem: "roomSetpoint",
        externalStem: "RoomSetpoint",
        mqttTopic: "room_setpoint",
        usedSource: roomSetpointUsedSource,
      }),
      buildSourceSignal({
        key: "water-supply",
        group: "water-circuit",
        title: t("settingsIntegrations.sigSupply"),
        icon: "droplet",
        select: { key: "waterSupplySource", label: t("settingsIntegrations.selSource"), haKeys: ["waterSupplyTempHa", "waterSupplyTempHaValid"] },
        secondarySelects: [{
          key: "localWaterSupplyTempSource",
          label: t("settingsIntegrations.selLocalSensor"),
          when: currentWaterSupplySource === "Local" && hasEntity("localWaterSupplyTempSource"),
        }],
        summaryValue: getSettingsStatValue("supplyTemp"),
        summarySource: waterSupplyUsedSource,
        summaryInfo: renderSettingsInfoToggle(
          "supplyTemp-info",
          t("settingsIntegrations.usedFallback"),
          supplyInfo,
          "i",
          `oq-settings-source-info oq-settings-source-info--${waterSupplyCalibrated ? "valid" : "error"} oq-settings-source-info--circle`,
        ),
        warning: localWaterSupplyWarning || (waterSupplyCorrection.calibrationRequired
          ? t("settingsIntegrations.supplyChangedWarn")
          : ""),
        routeWarning: invalidSourceValueWarning("supplyTemp"),
        measurementRows: [
          renderSourceRow({ label: t("settingsIntegrations.measLocal"), key: "waterSupplyTempEsp", sourceKind: "local", sourceState: "available", effective: sourcesMatch(waterSupplyUsedSource, "Local") }),
          renderSourceRow({ label: "PT1000", key: "waterSupplyTempPt1000", sourceKind: "pt1000", sourceState: "available", effective: sourcesMatch(waterSupplyUsedSource, "PT1000") }),
          renderSourceRow({ label: "DS18B20", key: "waterSupplyTempDs18b20", sourceKind: "ds18b20", sourceState: "available", effective: sourcesMatch(waterSupplyUsedSource, "DS18B20") }),
          cicAvailable ? renderSourceRow({ label: "CIC", key: "cicWaterSupplyTemp", sourceKind: "cic", sourceState: "available", effective: sourcesMatch(waterSupplyUsedSource, "CIC") }) : "",
          ...renderExternalSourceRows("waterSupplySource", waterSupplyUsedSource, {
            ha: ["waterSupplyTempHa", "waterSupplyTempHaValid"],
          }),
        ],
        measurementTitle: t("settingsIntegrations.measureRaw"),
      }),
      buildSourceSignal({
        key: "flow-source",
        group: "water-circuit",
        title: t("settingsIntegrations.sigFlow"),
        icon: "waves",
        select: { key: "flowSource", label: t("settingsIntegrations.selSource"), optionLabels: { "Outdoor unit": t("settingsIntegrations.flowQuatt") }, when: cicAvailable || currentFlowSource === "CIC" },
        secondarySelects: [
          {
            key: "qFlowSource",
            label: t("settingsIntegrations.selFlowPath"),
            infoId: "qFlowSource-info",
            infoCopy: t("settingsIntegrations.selFlowPathInfo"),
            when: currentFlowSource === "Outdoor unit" && hasEntity("qFlowSource"),
          },
          {
            key: "controllerFlowMeter",
            label: t("settingsIntegrations.selLocalMeter"),
            infoId: "controllerFlowMeter-info",
            infoCopy: t("settingsIntegrations.selLocalMeterInfo"),
            optionLabels: { "Huba Control": t("settingsIntegrations.selHubaControl") },
            when: currentFlowSource === "Outdoor unit" && hasEntity("controllerFlowMeter") && currentQFlowSource !== "Outdoor unit",
          },
          {
            key: "outdoorUnitFlowMode",
            label: t("settingsIntegrations.selMeterChoice"),
            infoId: "outdoorUnitFlowMode-info",
            infoCopy: t("settingsIntegrations.selMeterChoiceInfo"),
            when: currentFlowSource === "Outdoor unit" && hasEntity("outdoorUnitFlowMode") && (!hasEntity("qFlowSource") || currentQFlowSource !== "Local"),
          },
        ],
        secondaryNumbers: [{
          key: "customFlowMeterPulsesPerLiter",
          label: t("settingsIntegrations.selPulses"),
          infoId: "customFlowMeterPulsesPerLiter-info",
          infoCopy: t("settingsIntegrations.selPulsesInfo"),
          when: currentFlowSource === "Outdoor unit" && currentQFlowSource !== "Outdoor unit" && currentControllerFlowMeter === "Custom",
        }],
        summaryValue: getSettingsStatValue("flowSelected"),
        summarySource: flowUsedSource,
        routeWarning: invalidSourceValueWarning("flowSelected"),
        measurementRows: [
          renderSourceRow({ label: t("settingsIntegrations.measController"), key: "controllerFlow", sourceKind: "local", sourceState: "available", effective: sourcesMatch(flowUsedSource, optionLabel("Local")) }),
          renderSourceRow({ label: t("settingsIntegrations.measCombined"), key: "flowLocal", sourceKind: "outdoor", sourceState: "available", effective: /gecombineerd|combined/i.test(flowUsedSource) }),
          renderSourceRow({ label: t("settingsIntegrations.measHp1"), key: "hp1Flow", sourceKind: "hp1", sourceState: "available", effective: sourcesMatch(flowUsedSource, "HP1") && !sourcesMatch(flowUsedSource, "HP2") }),
          renderSourceRow({ label: t("settingsIntegrations.measHp2"), key: "hp2Flow", sourceKind: "hp2", sourceState: "available", effective: sourcesMatch(flowUsedSource, "HP2") && !sourcesMatch(flowUsedSource, "HP1") }),
          cicAvailable ? renderSourceRow({ label: "CIC", key: "cicFlowrate", sourceKind: "cic", sourceState: "available", effective: sourcesMatch(flowUsedSource, "CIC") }) : "",
        ],
      }),
      buildSourceSignal({
        key: "outside-temperature",
        group: "room-outside",
        title: t("settingsIntegrations.sigOutside"),
        icon: "sun",
        warning: currentOutsideTempSource === "MQTT"
          ? t("settingsIntegrations.mqttOutsideWarn")
          : "",
        select: buildExternalSourceSelect("outsideTemp", "OutsideTemperature", "outside_temperature", {
          label: t("settingsIntegrations.selOutside"),
          infoId: "outsideTempSource-auto-info",
          infoCopy: outsideTemperatureAutoInfo,
        }),
        summaryValue: getSettingsStatValue("outsideTempSelected"),
        summarySource: outsideTemperatureUsedSource,
        routeWarning: invalidSourceValueWarning("outsideTempSelected"),
        measurementRows: [
          renderSourceRow({ label: optionLabel("Outdoor unit"), key: "outsideTempLocalAggregated", sourceKind: "outdoor", sourceState: "available", effective: sourcesMatch(outsideTemperatureUsedSource, optionLabel("Outdoor unit")) }),
          ...renderExternalSourceRows("outsideTempSource", outsideTemperatureUsedSource, buildExternalSourceKeys("outsideTemp", "OutsideTemperature")),
        ],
      }),
      buildPermissionSignal({
        mode: "heating",
        title: t("settingsIntegrations.sigHeatPerm"),
        icon: "flame",
        usedSource: heatingEnableUsedSource,
        selectExtra: {
          infoId: "heatingEnableSource-info",
          infoCopy: t("settingsIntegrations.permInfoHeat"),
        },
        summaryValue: heatingEnableSourceDisabled
          ? t("settingsIntegrations.sumNotUsed")
          : sourceStateText("heatingEnableSelected", t("settingsIntegrations.diagAllowed"), t("settingsIntegrations.diagBlocked")),
        routeWarning: heatingEnableSourceDisabled ? "" : invalidSourceValueWarning("heatingEnableSelected"),
        measurementRows: [
          otAvailable ? renderSourceRow({ label: "OpenTherm", value: sourceStateText("otThermostatChEnable", t("settingsIntegrations.diagAllowed"), t("settingsIntegrations.diagBlocked")), sourceKind: "ot", sourceState: "available", effective: sourcesMatch(heatingEnableUsedSource, "OpenTherm") }) : "",
          cicAvailable ? renderSourceRow({ label: "CIC", value: sourceStateText("cicChEnabled", t("settingsIntegrations.diagAllowed"), t("settingsIntegrations.diagBlocked")), sourceKind: "cic", sourceState: "available", effective: sourcesMatch(heatingEnableUsedSource, "CIC") }) : "",
        ],
      }),
      buildPermissionSignal({
        mode: "cooling",
        title: t("settingsIntegrations.sigCoolPerm"),
        icon: "snowflake",
        usedSource: coolingEnableUsedSource,
        optionLabels: coolingEnableSourceLabels,
        selectExtra: {
          hiddenOptions: ["CIC", "CIC or HA input"],
        },
        summaryValue: sourceStateText("coolingEnableSelected", t("settingsIntegrations.diagAllowed"), t("settingsIntegrations.diagBlocked")),
        routeWarning: invalidSourceValueWarning("coolingEnableSelected"),
        measurementRows: [
          renderSourceRow({ label: t("settingsIntegrations.coolManual"), value: sourceStateText("manualCoolingEnable", t("settingsIntegrations.diagOn"), t("settingsIntegrations.diagOffValue")), sourceKind: "manual", sourceState: "available", effective: sourcesMatch(coolingEnableUsedSource, t("settingsIntegrations.coolManual")) }),
          otAvailable ? renderSourceRow({ label: "OpenTherm", value: sourceStateText("otThermostatCoolingEnable", t("settingsIntegrations.diagAllowed"), t("settingsIntegrations.diagBlocked")), sourceKind: "ot", sourceState: "available", effective: sourcesMatch(coolingEnableUsedSource, "OpenTherm") }) : "",
        ],
      }),
      buildSourceSignal({
        key: "cooling-dew-point",
        group: "cooling",
        title: t("settingsIntegrations.sigDewPoint"),
        icon: "thermometer",
        select: buildExternalSourceSelect("coolingDewPoint", "CoolingDewPoint", "cooling_dew_point", {
          infoId: "coolingDewPointSource-info",
          infoCopy: mqttAvailable
            ? t("settingsIntegrations.dewInfoFull")
            : t("settingsIntegrations.dewInfoHa"),
        }),
        summaryValue: getSettingsStatValue("coolingDewPointSelected"),
        summarySource: coolingDewPointUsedSource,
        routeWarning: invalidSourceValueWarning("coolingDewPointSelected"),
        measurementRows: [
          ...renderExternalSourceRows("coolingDewPointSource", coolingDewPointUsedSource, buildExternalSourceKeys("coolingDewPoint", "CoolingDewPoint")),
        ],
      }),
      buildSourceSignal({
        key: "external-heat-demand",
        group: "heating",
        title: t("settingsIntegrations.sigExtDemand"),
        icon: "zap",
        select: buildExternalSourceSelect("externalHeatDemand", "ExternalHeatDemand", "", {
          infoCopy: t("settingsIntegrations.heatDemandExternalInfo"),
        }),
        summaryValue: externalHeatDemandConfiguredSource === t("settingsIntegrations.optDisabled")
          ? t("settingsIntegrations.sumNotUsed")
          : getSettingsStatValue("externalHeatDemandSelected"),
        summarySource: externalHeatDemandUsedSource,
        routeWarning: String(getEntityValue("externalHeatDemandSource") || "") === "Disabled"
          ? ""
          : invalidSourceValueWarning("externalHeatDemandSelected"),
        measurementRows: [
          ...renderExternalSourceRows("externalHeatDemandSource", externalHeatDemandUsedSource, buildExternalSourceKeys("externalHeatDemand", "ExternalHeatDemand", false)),
        ],
      }),
      buildSourceSignal({
        key: "heating-supply-target",
        group: "heating",
        title: t("settingsIntegrations.sigSupplyTarget"),
        icon: "target",
        select: buildExternalSourceSelect("heatingSupplyTarget", "HeatingSupplyTarget", "heating_supply_target", {
          optionLabels: { "Heating curve": t("settingsIntegrations.optHeatingCurve"), "OT thermostat": t("settingsIntegrations.optOtThermostat") },
          infoCopy: t("settingsIntegrations.supplyTargetInfo"),
        }),
        summaryValue: heatingSupplyTargetSummaryValue,
        summarySource: heatingSupplyTargetUsedSource,
        routeWarning: heatingSupplyTargetIsExternal
          ? invalidSourceValueWarning("heatingSupplyTargetSelected")
          : invalidSourceValueWarning("curveSupplyTarget"),
        measurementRows: [
          renderSourceRow({ label: t("settingsIntegrations.optHeatingCurve"), key: "curveSupplyTarget", sourceKind: "local", sourceState: "available", effective: !heatingSupplyTargetIsExternal }),
          otAvailable ? renderSourceRow({ label: "OpenTherm", key: "otControlSetpoint", sourceKind: "ot", sourceState: "available", effective: sourcesMatch(heatingSupplyTargetUsedSource, "OpenTherm") }) : "",
          ...renderExternalSourceRows("heatingSupplyTargetSource", heatingSupplyTargetUsedSource, buildExternalSourceKeys("heatingSupplyTarget", "HeatingSupplyTarget")),
        ],
      }),
    ].filter(Boolean);

    if (!sourceSignals.length) {
      return "";
    }

    const sourceCategories = [
      { id: "room-outside", title: t("settingsIntegrations.catRoomOutside"), icon: "home-cog", keys: ["room-temperature", "room-setpoint", "outside-temperature"] },
      { id: "water-circuit", title: t("settingsIntegrations.catWater"), icon: "droplet", keys: ["water-supply", "flow-source"] },
      { id: "heating", title: t("settingsIntegrations.catHeating"), icon: "flame", keys: ["external-heat-demand", "heating-supply-target", "heating-enable"] },
      { id: "cooling", title: t("settingsIntegrations.catCooling"), icon: "snowflake", keys: ["cooling-enable", "cooling-dew-point"] },
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
    const categoryMarkup = visibleCategories.map((category) => {
      const count = category.signals.length;
      const categoryTitleId = `oq-settings-source-category-${category.id}`;
      const signalsMarkup = category.signals.map((signal) => {
        const active = signal.key === focusedSignal.key;
        const showUsedSource = signal.summarySource !== "—"
          || !sourcesMatch(signal.configuredSource, t("settingsIntegrations.optDisabled"));
        const sourcePathLabel = showUsedSource
          ? t("settingsIntegrations.sourcePath", { set: signal.configuredSource, used: signal.summarySource })
          : t("settingsIntegrations.sourcePathSet", { set: signal.configuredSource });
        return `
          <button
            class="oq-settings-source-signal${signal.warningCopy ? " is-warning" : ""}"
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
              ${signal.warningCopy ? `<span class="oq-settings-source-signal-warning" aria-label="${escapeHtml(t("settingsIntegrations.sourceProblem", { warning: signal.warningCopy }))}" title="${escapeHtml(t("settingsIntegrations.warnDetails"))}">!</span>` : ""}
            </span>
            <span class="oq-settings-source-signal-summary">
              <strong>${escapeHtml(signal.summaryValue)}</strong>
              <small
                class="oq-settings-source-signal-source-path"
                aria-label="${escapeHtml(sourcePathLabel)}"
              >
                <span>${escapeHtml(signal.configuredSource)}</span>
                ${showUsedSource && signal.configuredSource !== signal.summarySource ? `
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
              <small class="oq-settings-source-category-count">${formatNumber(count, { maximumFractionDigits: 0 })} ${count === 1 ? t("settingsIntegrations.sigSingular") : t("settingsIntegrations.sigPlural")}</small>
            </div>
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
          ${escapeHtml(t("settingsIntegrations.allSignals"))}
        </button>
        <p class="oq-settings-source-inspector-kicker">${escapeHtml(focusedCategory?.title || t("settingsIntegrations.kickerFallback"))}</p>
        <h4>${escapeHtml(focusedSignal.title)}</h4>
        ${focusedSignal.warningCopy ? `<p class="oq-settings-source-warning" data-oq-source-warning>${escapeHtml(focusedSignal.warningCopy)}</p>` : ""}
        <div class="oq-settings-source-inspector-summary">
          <div>
            <span>${escapeHtml(t("settingsIntegrations.setLabel"))}</span>
            <strong>${escapeHtml(focusedSignal.configuredSource)}</strong>
          </div>
          <div>
            <span>${escapeHtml(t("settingsIntegrations.usedLabel"))}</span>
            ${focusedSignal.summaryInfo}
            <strong>${escapeHtml(focusedSignal.summaryValue)}</strong>
            <span>${escapeHtml(focusedSignal.summarySource)}</span>
          </div>
        </div>
        ${focusedSignal.controlsMarkup ? `
          <h5 class="oq-settings-source-inspector-section-title">${escapeHtml(t("settingsIntegrations.choiceTitle"))}</h5>
          <div class="oq-settings-source-controls">${focusedSignal.controlsMarkup}</div>
        ` : ""}
        <h5 class="oq-settings-source-inspector-section-title">${escapeHtml(focusedSignal.measurementTitle)}</h5>
        ${focusedSignal.measurementRows.length
          ? `<div class="oq-settings-source-rows">${focusedSignal.measurementRows.join("")}</div>`
          : `<p class="oq-settings-source-empty">${escapeHtml(t("settingsIntegrations.emptyMeasures"))}</p>`}
      </article>
    `;
    const sourceWorkspaceMarkup = `
      <div class="oq-settings-source-shell" data-oq-source-workspace>
        <div class="oq-settings-source-workspace${state.settingsSourceDetailOpen ? " is-detail-open" : ""}">
          <nav class="oq-settings-source-nav" aria-label="${escapeHtml(t("settingsIntegrations.signalsNav"))}">
            ${categoryMarkup}
          </nav>
          ${inspectorMarkup}
        </div>
      </div>
    `;

    const heatingAdvice = hasEntity("heatingEnableSource") ? getHeatingEnableAdvice() : null;
    const heatingAdviceHeaderAction = hasEntity("heatingEnableSource") ? `<button class="oq-helper-button ${heatingAdvice && heatingAdvice.deviant ? "oq-helper-button--warning-soft" : "oq-helper-button--ghost"}" type="button" data-oq-action="open-heating-strategy-advice-modal">${heatingAdvice && heatingAdvice.deviant ? '<span class="oq-advice-warn-icon"><svg viewBox="0 0 20 18" aria-hidden="true"><path d="M10 1.6 L18.2 16.4 H1.8 Z"/><rect x="9.1" y="5.4" width="1.8" height="5.8" rx="0.9"/><circle cx="10" cy="13.6" r="1.1"/></svg></span> ' : ""}${escapeHtml(t("settingsIntegrations.heatAdviceShort"))}</button>` : "";
    return renderSettingsSection(
      t("settingsIntegrations.sectionSources"),
      t("settingsIntegrations.sectionSensors"),
      t("settingsIntegrations.sectionSensorsCopy"),
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
            <p class="oq-settings-quickstart-status-label">${escapeHtml(t("settingsIntegrations.mqttSensors"))}</p>
            <strong class="oq-settings-quickstart-status-value">${escapeHtml(sensorSummary)}</strong>
          </div>
          <button
            class="oq-helper-button oq-helper-button--ghost"
            type="button"
            data-oq-action="open-mqtt-sensors-modal"
          >
            ${escapeHtml(t("settingsIntegrations.mqttDetails"))}
          </button>
        </div>
      </section>
    ` : "";

    return renderSettingsSection(
      t("settingsIntegrations.mqttGroup"),
      t("settingsIntegrations.mqttTitle"),
      t("settingsIntegrations.mqttCopy"),
      `
        <div class="oq-settings-mqtt-shell">
          <section class="oq-settings-mqtt-panel oq-settings-mqtt-panel--broker">
            <div class="oq-settings-field-head">
              <h3>${escapeHtml(t("settingsIntegrations.mqttBrokerTitle"))}</h3>
            </div>
            <div class="oq-settings-quickstart-status-row oq-settings-mqtt-status-row">
              <div>
                <p class="oq-settings-quickstart-status-label">${escapeHtml(t("settingsIntegrations.mqttCurrentStatus"))}</p>
                <strong class="oq-settings-quickstart-status-value">${escapeHtml(getMqttStatusLabel())}</strong>
                <p class="oq-settings-quickstart-status-copy">${escapeHtml(getMqttStatusDetail())}</p>
              </div>
              <button
                class="oq-helper-button oq-helper-button--ghost"
                type="button"
                data-oq-action="open-mqtt-modal"
              >
                ${escapeHtml(t("settingsIntegrations.mqttAdjust"))}
              </button>
            </div>
          </section>
          ${sensorsPanel}
        </div>
      `,
    );
  }
