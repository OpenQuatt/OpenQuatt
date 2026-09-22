import { getEntityNumericValue, hasEntity, isEntityActive } from "../core/app-shared.js";
import { renderOqIcon } from "../core/config.js";
import { state } from "../core/state.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";
import { formatNumber, t } from "../i18n/index.js";

  export function getMqttStatusLabel() {
    const status = state.mqttStatus;
    if (!status) {
      return t("mqtt.statusLoading");
    }
    if (status.enabled && status.connected) {
      return t("mqtt.statusConnected");
    }
    if (status.enabled) {
      return t("mqtt.statusEnabled");
    }
    if (status.broker) {
      return t("mqtt.statusOff");
    }
    return t("mqtt.statusUnset");
  }

  export function getMqttStatusDetail() {
    const status = state.mqttStatus;
    if (!status) {
      return t("mqtt.detailLoading");
    }
    const broker = String(status.broker || "").trim();
    const port = Number(status.port || 1883);
    const endpoint = broker ? `${broker}:${port}` : t("mqtt.detailNoBroker");
    if (status.enabled && status.connected) {
      return t("mqtt.detailConnected", { endpoint });
    }
    if (status.enabled) {
      return broker
        ? t("mqtt.detailNotConfirmed", { endpoint })
        : t("mqtt.detailNoBrokerSet");
    }
    if (broker) {
      return t("mqtt.detailStoredOff", { endpoint });
    }
    return t("mqtt.detailOffNoBroker");
  }

  export function renderMqttNumericValue(key, decimals = 2) {
    const value = getEntityNumericValue(key);
    if (!Number.isFinite(value)) {
      return `<span class="oq-settings-mqtt-sensor-value-missing">${escapeHtml(t("mqtt.noReading"))}</span>`;
    }
    return `
      <span class="oq-settings-mqtt-sensor-value-number">${escapeHtml(formatNumber(value, { minimumFractionDigits: decimals, maximumFractionDigits: decimals }))}</span>
      <span class="oq-settings-mqtt-sensor-value-unit">°C</span>
    `;
  }

  export function renderMqttBooleanValue(sensor) {
    if (!hasEntity(sensor.valueKey) || !isEntityActive(sensor.validKey)) {
      return `<span class="oq-settings-mqtt-sensor-value-missing">${escapeHtml(t("mqtt.noReading"))}</span>`;
    }
    const activeLabel = sensor.activeLabel || t("settingsIntegrations.diagAllowed");
    const inactiveLabel = sensor.inactiveLabel || t("settingsIntegrations.diagBlocked");
    return `<span class="oq-settings-mqtt-sensor-value-boolean">${escapeHtml(isEntityActive(sensor.valueKey) ? activeLabel : inactiveLabel)}</span>`;
  }

  export function renderMqttSensorValue(sensor) {
    if (sensor.kind === "binary") {
      return renderMqttBooleanValue(sensor);
    }
    return renderMqttNumericValue(sensor.valueKey);
  }

  export function formatMqttAge(key) {
    const age = getEntityNumericValue(key);
    if (!Number.isFinite(age)) {
      return "—";
    }
    if (age < 60) {
      return `${Math.round(age)} s`;
    }
    if (age < 3600) {
      return `${Math.round(age / 60)} min`;
    }
    return `${Math.round(age / 3600)} u`;
  }

  export function getMqttValidityLabel(validKey) {
    if (!hasEntity(validKey)) {
      return t("mqtt.validityNoStatus");
    }
    return isEntityActive(validKey) ? t("mqtt.validityValid") : t("mqtt.validityStale");
  }

  export function getMqttInputTopic(key) {
    const topics = state.mqttStatus?.input_topics;
    if (topics && typeof topics === "object") {
      const value = String(topics[key] || "").trim();
      if (value) {
        return value;
      }
    }
    if (key === "cooling_dew_point") {
      return String(state.mqttStatus?.dew_point_topic || "").trim();
    }
    return "";
  }

  export function isMqttInputEnabled(key) {
    const inputEnabled = state.mqttStatus?.input_enabled;
    if (inputEnabled && typeof inputEnabled === "object" && Object.prototype.hasOwnProperty.call(inputEnabled, key)) {
      return inputEnabled[key] !== false;
    }
    return true;
  }

  export function isMqttInputRetained(key) {
    const inputRetained = state.mqttStatus?.input_retained;
    return Boolean(inputRetained && typeof inputRetained === "object" && inputRetained[key]);
  }

  export function isMqttInputAcceptRetained(key) {
    const inputAcceptRetained = state.mqttStatus?.input_accept_retained;
    return Boolean(
      inputAcceptRetained &&
      typeof inputAcceptRetained === "object" &&
      inputAcceptRetained[key]
    );
  }

  export function getMqttInputSensors() {
    return [
      {
        topicKey: "cooling_dew_point",
        label: t("mqtt.sensorDewPoint"),
        valueKey: "mqttCoolingDewPoint",
        ageKey: "mqttCoolingDewPointAge",
        validKey: "mqttCoolingDewPointValid",
        staleCopy: t("mqtt.stale15min"),
        payloadInfoTitle: t("mqtt.payloadTempTitle"),
        payloadInfo: t("mqtt.payloadDew"),
      },
      {
        topicKey: "outside_temperature",
        label: t("mqtt.sensorOutside"),
        valueKey: "mqttOutsideTemperature",
        ageKey: "mqttOutsideTemperatureAge",
        validKey: "mqttOutsideTemperatureValid",
        staleCopy: t("mqtt.stale30min"),
        payloadInfoTitle: t("mqtt.payloadTempTitle"),
        payloadInfo: t("mqtt.payloadOutside"),
      },
      {
        topicKey: "room_temperature",
        label: t("mqtt.sensorRoom"),
        valueKey: "mqttRoomTemperature",
        ageKey: "mqttRoomTemperatureAge",
        validKey: "mqttRoomTemperatureValid",
        staleCopy: t("mqtt.stale10min"),
        payloadInfoTitle: t("mqtt.payloadTempTitle"),
        payloadInfo: t("mqtt.payloadRoom"),
      },
      {
        topicKey: "room_setpoint",
        label: t("mqtt.sensorRoomSetpoint"),
        valueKey: "mqttRoomSetpoint",
        ageKey: "mqttRoomSetpointAge",
        validKey: "mqttRoomSetpointValid",
        staleCopy: t("mqtt.staleNew"),
        stateful: true,
        payloadInfoTitle: t("mqtt.payloadTempTitle"),
        payloadInfo: t("mqtt.payloadRoomSetpoint"),
      },
      {
        topicKey: "heating_supply_target",
        label: t("mqtt.sensorSupplyTarget"),
        valueKey: "mqttHeatingSupplyTarget",
        ageKey: "mqttHeatingSupplyTargetAge",
        validKey: "mqttHeatingSupplyTargetValid",
        staleCopy: t("mqtt.stale15min"),
        payloadInfoTitle: t("mqtt.payloadTempTitle"),
        payloadInfo: t("mqtt.payloadSupply"),
      },
      {
        topicKey: "heating_enable",
        label: t("mqtt.sensorHeatEnable"),
        valueKey: "mqttHeatingEnable",
        ageKey: "mqttHeatingEnableAge",
        validKey: "mqttHeatingEnableValid",
        staleCopy: t("mqtt.staleNew"),
        kind: "binary",
        stateful: true,
        payloadInfoTitle: t("mqtt.payloadBoolTitle"),
        payloadInfo: t("mqtt.payloadHeatBool"),
      },
      {
        topicKey: "cooling_enable",
        label: t("mqtt.sensorCoolEnable"),
        valueKey: "mqttCoolingEnable",
        ageKey: "mqttCoolingEnableAge",
        validKey: "mqttCoolingEnableValid",
        staleCopy: t("mqtt.staleNew"),
        kind: "binary",
        stateful: true,
        payloadInfoTitle: t("mqtt.payloadBoolTitle"),
        payloadInfo: t("mqtt.payloadCoolBool"),
      },
    ];
  }

  export function formatMqttSensorValiditySummary(sensors = getMqttInputSensors()) {
    if (!sensors.length) {
      return t("mqtt.summaryNone");
    }
    const enabledSensors = sensors.filter((sensor) => isMqttInputEnabled(sensor.topicKey));
    const disabledCount = sensors.length - enabledSensors.length;
    if (!enabledSensors.length) {
      return disabledCount === 1
        ? t("mqtt.summaryDisabledOne", { count: formatNumber(disabledCount, { maximumFractionDigits: 0 }) })
        : t("mqtt.summaryDisabledOther", { count: formatNumber(disabledCount, { maximumFractionDigits: 0 }) });
    }
    const validCount = enabledSensors.filter((sensor) => isEntityActive(sensor.validKey)).length;
    const validityText = validCount === enabledSensors.length
      ? validCount === 1
        ? t("mqtt.summaryValidOne", { count: formatNumber(validCount, { maximumFractionDigits: 0 }) })
        : t("mqtt.summaryValidOther", { count: formatNumber(validCount, { maximumFractionDigits: 0 }) })
      : t("mqtt.summaryValidPart", { valid: formatNumber(validCount, { maximumFractionDigits: 0 }), total: formatNumber(enabledSensors.length, { maximumFractionDigits: 0 }) });
    return disabledCount ? `${validityText} · ${t("mqtt.summaryDisabledShort", { count: formatNumber(disabledCount, { maximumFractionDigits: 0 }) })}` : validityText;
  }

  export function renderMqttModal() {
    const status = state.mqttStatus || {};
    const enabled = Boolean(state.mqttDraftEnabled);
    const clearPassword = Boolean(state.mqttDraftClearPassword);
    const passwordPlaceholder = status.password_set
      ? t("mqtt.passKeep")
      : t("mqtt.passOptional");
    const noticeMarkup = state.mqttNotice
      ? `<div class="oq-helper-modal-success oq-helper-modal-success--compact" aria-live="polite"><strong>${escapeHtml(t("mqtt.modalStatus"))}</strong><span>${escapeHtml(state.mqttNotice)}</span></div>`
      : "";
    const errorMarkup = state.mqttError
      ? `<div class="oq-helper-modal-note oq-helper-modal-note--error" aria-live="assertive">${escapeHtml(state.mqttError)}</div>`
      : "";

    return renderModalShell({
      id: "system",
      titleId: "oq-mqtt-modal-title",
      kicker: t("mqtt.modalKicker"),
      title: t("mqtt.modalTitle"),
      copy: t("mqtt.modalCopy"),
      closeAction: "close-system-modal",
      closeLabel: t("mqtt.modalClose"),
      body: `
          ${noticeMarkup}
          ${errorMarkup}
          <div class="oq-settings-mqtt-form-grid">
            <label class="oq-settings-mqtt-toggle">
              <input
                type="checkbox"
                data-oq-mqtt-field="enabled"
                ${enabled ? "checked" : ""}
                ${state.mqttBusy ? "disabled" : ""}
              >
              <span>${escapeHtml(t("mqtt.enableLabel"))}</span>
            </label>
            <label class="oq-helper-modal-auth-field oq-settings-mqtt-field">
              <span>${escapeHtml(t("mqtt.brokerLabel"))}</span>
              <input
                class="oq-helper-input"
                type="text"
                data-oq-mqtt-field="broker"
                value="${escapeHtml(state.mqttDraftBroker)}"
                placeholder="mqtt.local"
                autocomplete="off"
                ${state.mqttBusy ? "disabled" : ""}
              >
            </label>
            <label class="oq-helper-modal-auth-field oq-settings-mqtt-field oq-settings-mqtt-field--port">
              <span>${escapeHtml(t("mqtt.portLabel"))}</span>
              <input
                class="oq-helper-input"
                type="number"
                min="1"
                max="65535"
                step="1"
                inputmode="numeric"
                data-oq-mqtt-field="port"
                value="${escapeHtml(state.mqttDraftPort)}"
                ${state.mqttBusy ? "disabled" : ""}
              >
            </label>
            <label class="oq-helper-modal-auth-field oq-settings-mqtt-field">
              <span>${escapeHtml(t("mqtt.userLabel"))}</span>
              <input
                class="oq-helper-input"
                type="text"
                data-oq-mqtt-field="username"
                value="${escapeHtml(state.mqttDraftUsername)}"
                autocomplete="username"
                ${state.mqttBusy ? "disabled" : ""}
              >
            </label>
            <label class="oq-helper-modal-auth-field oq-settings-mqtt-field">
              <span>${escapeHtml(t("mqtt.passLabel"))}</span>
              <input
                class="oq-helper-input"
                type="password"
                data-oq-mqtt-field="password"
                value="${escapeHtml(state.mqttDraftPassword)}"
                placeholder="${escapeHtml(passwordPlaceholder)}"
                autocomplete="current-password"
                ${state.mqttBusy || clearPassword ? "disabled" : ""}
              >
            </label>
            ${status.password_set ? `
              <label class="oq-settings-mqtt-toggle">
                <input
                  type="checkbox"
                  data-oq-mqtt-field="clear-password"
                  ${clearPassword ? "checked" : ""}
                  ${state.mqttBusy ? "disabled" : ""}
                >
                <span>${escapeHtml(t("mqtt.clearPass"))}</span>
              </label>
            ` : ""}
          </div>`,
      actions: `
        <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${state.mqttBusy ? "disabled" : ""}>${escapeHtml(t("header.done"))}</button>
        <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="save-mqtt-config" ${state.mqttBusy || !status.csrf_token ? "disabled" : ""}>${state.mqttBusy ? escapeHtml(t("mqtt.saving")) : escapeHtml(t("common.save"))}</button>
      `,
    });
  }

  export function renderMqttSensorsModal() {
    const sensors = getMqttInputSensors();
    const nonRetainedTimeoutMinutes = Math.max(
      1,
      Math.round(Number(state.mqttStatus?.non_retained_stateful_timeout_s || 1800) / 60)
    );
    const expandedTopicKey = sensors.some((sensor) => sensor.topicKey === state.mqttExpandedTopicKey)
      ? state.mqttExpandedTopicKey
      : "";
    const sensorValiditySummary = formatMqttSensorValiditySummary(sensors);
    const sensorMarkup = sensors.map((sensor) => {
      const topic = getMqttInputTopic(sensor.topicKey);
      const topicDisplay = topic || t("mqtt.topicLoading");
      const age = formatMqttAge(sensor.ageKey);
      const inputEnabled = isMqttInputEnabled(sensor.topicKey);
      const valid = isEntityActive(sensor.validKey);
      const retained = inputEnabled && valid && isMqttInputRetained(sensor.topicKey);
      const acceptRetained = sensor.stateful && isMqttInputAcceptRetained(sensor.topicKey);
      const copied = state.mqttCopiedTopicKey === sensor.topicKey;
      const expanded = expandedTopicKey === sensor.topicKey;
      const busy = state.mqttInputToggleBusyKey === sensor.topicKey ||
        state.mqttRetainedToggleBusyKey === sensor.topicKey;
      const statusTone = inputEnabled ? (valid ? "valid" : "invalid") : "disabled";
      const statusLabel = inputEnabled ? (valid ? t("mqtt.statusValidShort") : t("mqtt.statusInvalidShort")) : t("mqtt.statusOffShort");
      const validityTitle = inputEnabled ? getMqttValidityLabel(sensor.validKey) : t("mqtt.statusDisabledLong");
      const ageText = age === "—" ? t("mqtt.ageUnknown") : t("mqtt.ageAgo", { age });
      const statusTitle = inputEnabled
        ? valid
          ? sensor.stateful
            ? acceptRetained
              ? t("mqtt.statefulRetained", { age: ageText })
              : t("mqtt.statefulLive", { age: ageText, minutes: formatNumber(nonRetainedTimeoutMinutes, { maximumFractionDigits: 0 }) })
            : t("mqtt.plainStale", { age: ageText, stale: sensor.staleCopy })
          : age === "—"
            ? t("mqtt.noValid")
            : t("mqtt.staleAge", { age })
        : t("mqtt.topicUnused");
      const toggleTitle = inputEnabled ? t("mqtt.toggleOff") : t("mqtt.toggleOn");
      const retainedTitle = t("mqtt.retainedInfo");
      const retainedBehavior = acceptRetained
        ? t("mqtt.retainedKept")
        : t("mqtt.retainedLiveOnly", { minutes: formatNumber(nonRetainedTimeoutMinutes, { maximumFractionDigits: 0 }) });
      const payloadInfo = sensor.stateful
        ? `${sensor.payloadInfo} ${acceptRetained
          ? t("mqtt.retainedAccept")
          : t("mqtt.retainedReject", { minutes: formatNumber(nonRetainedTimeoutMinutes, { maximumFractionDigits: 0 }) })}`
        : sensor.payloadInfo;
      return `
        <article class="oq-settings-mqtt-sensor-row${expanded ? " is-open" : ""}${inputEnabled ? "" : " is-disabled"}">
          <div
            class="oq-settings-mqtt-sensor-summary"
            data-oq-action="toggle-mqtt-sensor-topic"
            data-oq-mqtt-topic-key="${escapeHtml(sensor.topicKey)}"
            aria-expanded="${expanded ? "true" : "false"}"
          >
            <span class="oq-settings-mqtt-sensor-name">${escapeHtml(sensor.label)}</span>
            <span class="oq-settings-mqtt-sensor-value">
              ${inputEnabled ? renderMqttSensorValue(sensor) : '<span class="oq-settings-mqtt-sensor-value-missing">—</span>'}
            </span>
            <span class="oq-settings-mqtt-sensor-status-cell">
              <em
                class="oq-settings-mqtt-sensor-status oq-settings-mqtt-sensor-status--${statusTone}"
                title="${escapeHtml(statusTitle)}"
                aria-label="${escapeHtml(validityTitle)}: ${escapeHtml(statusTitle)}"
              >${escapeHtml(statusLabel)}</em>
              ${retained ? `<span class="oq-settings-mqtt-sensor-retained" title="${escapeHtml(retainedTitle)}" aria-label="${escapeHtml(retainedTitle)}">R</span>` : ""}
            </span>
            <button
              class="oq-settings-toggle-switch oq-settings-mqtt-sensor-inline-toggle${inputEnabled ? " is-on" : ""}"
              type="button"
              data-oq-action="toggle-mqtt-input"
              data-oq-mqtt-topic-key="${escapeHtml(sensor.topicKey)}"
              aria-pressed="${inputEnabled ? "true" : "false"}"
              aria-label="${escapeHtml(`${sensor.label}: ${toggleTitle}`)}"
              title="${escapeHtml(toggleTitle)}"
              ${busy || !state.mqttStatus?.csrf_token ? "disabled" : ""}
            >
              <span class="oq-settings-toggle-switch-track"><span class="oq-settings-toggle-switch-knob"></span></span>
            </button>
            <span class="oq-settings-mqtt-sensor-chevron" aria-hidden="true"></span>
          </div>
          ${expanded ? `
            <div class="oq-settings-mqtt-sensor-topic">
              ${sensor.stateful ? `
                <div class="oq-settings-mqtt-retained-setting">
                  <span class="oq-settings-mqtt-retained-setting-copy">
                    <strong>${escapeHtml(t("mqtt.retainedUse"))}</strong>
                    <small>${escapeHtml(retainedBehavior)}</small>
                  </span>
                  <button
                    class="oq-settings-toggle-switch oq-settings-mqtt-retained-toggle${acceptRetained ? " is-on" : ""}"
                    type="button"
                    data-oq-action="toggle-mqtt-retained"
                    data-oq-mqtt-topic-key="${escapeHtml(sensor.topicKey)}"
                    aria-pressed="${acceptRetained ? "true" : "false"}"
                    aria-label="${escapeHtml(t("mqtt.retainedUseAria", { label: sensor.label, action: acceptRetained ? t("mqtt.retainedActionOff") : t("mqtt.retainedActionOn") }))}"
                    title="${escapeHtml(acceptRetained ? t("mqtt.retainedIgnore") : t("mqtt.retainedUse"))}"
                    ${busy || !state.mqttStatus?.csrf_token ? "disabled" : ""}
                  >
                    <span class="oq-settings-toggle-switch-track"><span class="oq-settings-toggle-switch-knob"></span></span>
                  </button>
                </div>
              ` : ""}
              <div class="oq-settings-mqtt-sensor-topic-head">
                <span class="oq-settings-mqtt-sensor-topic-label">${escapeHtml(t("mqtt.subTopic"))}</span>
              </div>
              <div class="oq-settings-mqtt-topic-row">
                <div class="oq-settings-mqtt-topic-field${copied ? " is-copied" : ""}">
                  <code>${escapeHtml(topicDisplay)}</code>
                  <button
                    class="oq-settings-mqtt-topic-copy"
                    type="button"
                    data-oq-action="copy-mqtt-topic"
                    data-oq-mqtt-topic-key="${escapeHtml(sensor.topicKey)}"
                    aria-label="${escapeHtml(copied ? t("mqtt.copiedAria", { label: sensor.label }) : t("mqtt.copyAria", { label: sensor.label }))}"
                    title="${escapeHtml(copied ? t("mqtt.copiedTitle") : t("mqtt.copyTitle"))}"
                    ${!topic ? "disabled" : ""}
                  >
                    ${renderOqIcon(copied ? "clipboard-check" : "clipboard", "oq-settings-mqtt-topic-copy-icon")}
                  </button>
                </div>
                <details class="oq-settings-mqtt-topic-info">
                  <summary aria-label="${escapeHtml(t("mqtt.payloadInfoAria", { label: sensor.label }))}">i</summary>
                  <div class="oq-settings-mqtt-topic-info-popover">
                    <strong>${escapeHtml(sensor.payloadInfoTitle || t("mqtt.payloadFallback"))}</strong>
                    <p>${escapeHtml(payloadInfo || "")}</p>
                  </div>
                </details>
              </div>
            </div>
          ` : ""}
        </article>
      `;
    }).join("");
    const errorMarkup = state.mqttError
      ? `<div class="oq-helper-modal-note oq-helper-modal-note--error" aria-live="assertive">${escapeHtml(state.mqttError)}</div>`
      : "";

    return renderModalShell({
      id: "system",
      titleId: "oq-mqtt-sensors-modal-title",
      kicker: t("mqtt.modalKicker"),
      title: t("mqtt.sensorsTitle"),
      className: "oq-helper-modal--mqtt-sensors",
      headerMarkup: `<div class="oq-settings-mqtt-modal-head">
            <span class="oq-settings-mqtt-modal-icon">${renderMqttLogoIcon("oq-settings-mqtt-modal-logo")}</span>
            <div>
              <p class="oq-helper-modal-kicker">${escapeHtml(t("mqtt.modalKicker"))}</p>
              <h2 class="oq-helper-modal-title" id="oq-mqtt-sensors-modal-title">${escapeHtml(t("mqtt.sensorsTitle"))}</h2>
            </div>
            <button class="oq-helper-modal-close" type="button" data-oq-action="close-system-modal" aria-label="${escapeHtml(t("mqtt.sensorsClose"))}">×</button>
          </div>`,
      body: `
          ${errorMarkup}
          <div class="oq-settings-mqtt-sensor-table">
            <div class="oq-settings-mqtt-sensor-table-head" aria-hidden="true">
              <span>${escapeHtml(t("mqtt.tableSensor"))}</span>
              <span>${escapeHtml(t("mqtt.tableValue"))}</span>
              <span>${escapeHtml(t("mqtt.tableStatus"))}</span>
              <span></span>
              <span></span>
            </div>
            ${sensorMarkup}
          </div>
          <div class="oq-settings-mqtt-sensor-footer">
            <span>${escapeHtml(sensorValiditySummary)}</span>
            <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-system-modal">${escapeHtml(t("header.done"))}</button>
          </div>`,
    });
  }

  export function renderMqttLogoIcon(className = "") {
    const classAttr = className ? ` class="${escapeHtml(className)}"` : "";
    return `
      <svg${classAttr} viewBox="0 0 320 320" aria-hidden="true" focusable="false" xmlns="http://www.w3.org/2000/svg">
        <path d="M7.1,180.6v117.1c0,8.4,6.8,15.3,15.3,15.3H142C141,239.8,80.9,180.7,7.1,180.6z"/>
        <path d="M7.1,84.1v49.8c99,0.9,179.4,80.7,180.4,179.1h51.7C238.2,186.6,134.5,84.2,7.1,84.1z"/>
        <path d="M312.9,297.6V193.5C278.1,107.2,207.3,38.9,119,7.1H22.4c-8.4,0-15.3,6.8-15.3,15.3v15c152.6,0.9,276.6,124,277.6,275.6h13C306.1,312.9,312.9,306.1,312.9,297.6z"/>
        <path d="M272.6,49.8c14.5,14.4,28.6,31.7,40.4,47.8V22.4c0-8.4-6.8-15.3-15.3-15.3h-77.3C238.4,19.7,256.6,33.9,272.6,49.8z"/>
      </svg>
    `;
  }
