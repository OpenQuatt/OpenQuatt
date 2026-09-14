import { getEntityNumericValue, hasEntity, isEntityActive } from "../core/app-shared.js";
import { renderOqIcon } from "../core/config.js";
import { state } from "../core/state.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";

  export function getMqttStatusLabel() {
    const status = state.mqttStatus;
    if (!status) {
      return "Laden...";
    }
    if (status.enabled && status.connected) {
      return "Verbonden";
    }
    if (status.enabled) {
      return "Ingeschakeld";
    }
    if (status.broker) {
      return "Uit";
    }
    return "Niet ingesteld";
  }

  export function getMqttStatusDetail() {
    const status = state.mqttStatus;
    if (!status) {
      return "MQTT-status wordt geladen.";
    }
    const broker = String(status.broker || "").trim();
    const port = Number(status.port || 1883);
    const endpoint = broker ? `${broker}:${port}` : "geen broker";
    if (status.enabled && status.connected) {
      return `Verbonden met ${endpoint}.`;
    }
    if (status.enabled) {
      return broker
        ? `MQTT staat aan; verbinding met ${endpoint} is nog niet bevestigd.`
        : "MQTT staat aan, maar er is nog geen broker ingesteld.";
    }
    if (broker) {
      return `Broker ${endpoint} is opgeslagen, maar MQTT inputbronnen staan uit.`;
    }
    return "MQTT inputbronnen staan uit. Stel een broker in om externe bronwaarden te ontvangen.";
  }

  export function renderMqttNumericValue(key, decimals = 2) {
    const value = getEntityNumericValue(key);
    if (!Number.isFinite(value)) {
      return '<span class="oq-settings-mqtt-sensor-value-missing">Geen meting</span>';
    }
    return `
      <span class="oq-settings-mqtt-sensor-value-number">${escapeHtml(value.toFixed(decimals))}</span>
      <span class="oq-settings-mqtt-sensor-value-unit">°C</span>
    `;
  }

  export function renderMqttBooleanValue(sensor) {
    if (!hasEntity(sensor.valueKey) || !isEntityActive(sensor.validKey)) {
      return '<span class="oq-settings-mqtt-sensor-value-missing">Geen meting</span>';
    }
    const activeLabel = sensor.activeLabel || "Toegestaan";
    const inactiveLabel = sensor.inactiveLabel || "Geblokkeerd";
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
      return "Nog geen status";
    }
    return isEntityActive(validKey) ? "Geldig" : "Ontbreekt of verouderd";
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
        label: "Dauwpunt",
        valueKey: "mqttCoolingDewPoint",
        ageKey: "mqttCoolingDewPointAge",
        validKey: "mqttCoolingDewPointValid",
        staleCopy: "15 minuten",
        payloadInfoTitle: "Temperatuurpayload",
        payloadInfo: 'Publiceer live een temperatuur in °C. Voorbeelden: 16.2, 16,2, 16.2 °C of {"value":16.2}. Geldig bereik: -20..35 °C. Retained berichten worden niet gebruikt voor regeling.',
      },
      {
        topicKey: "outside_temperature",
        label: "Buitentemperatuur",
        valueKey: "mqttOutsideTemperature",
        ageKey: "mqttOutsideTemperatureAge",
        validKey: "mqttOutsideTemperatureValid",
        staleCopy: "30 minuten",
        payloadInfoTitle: "Temperatuurpayload",
        payloadInfo: 'Publiceer live een temperatuur in °C. Voorbeelden: 15.0, 15,0, 15.0 °C of {"value":15.0}. Geldig bereik: -40..60 °C. Retained berichten worden niet gebruikt voor regeling.',
      },
      {
        topicKey: "room_temperature",
        label: "Kamertemperatuur",
        valueKey: "mqttRoomTemperature",
        ageKey: "mqttRoomTemperatureAge",
        validKey: "mqttRoomTemperatureValid",
        staleCopy: "10 minuten",
        payloadInfoTitle: "Temperatuurpayload",
        payloadInfo: 'Publiceer live een temperatuur in °C. Voorbeelden: 21.1, 21,1, 21.1 °C of {"value":21.1}. Geldig bereik: 0..50 °C. Retained berichten worden niet gebruikt voor regeling.',
      },
      {
        topicKey: "room_setpoint",
        label: "Kamer setpoint",
        valueKey: "mqttRoomSetpoint",
        ageKey: "mqttRoomSetpointAge",
        validKey: "mqttRoomSetpointValid",
        staleCopy: "nieuw bericht",
        stateful: true,
        payloadInfoTitle: "Temperatuurpayload",
        payloadInfo: 'Publiceer een setpoint in °C. Voorbeelden: 21.0, 21,0, 21.0 °C of {"value":21.0}. Geldig bereik: 5..35 °C.',
      },
      {
        topicKey: "heating_supply_target",
        label: "Aanvoertarget",
        valueKey: "mqttHeatingSupplyTarget",
        ageKey: "mqttHeatingSupplyTargetAge",
        validKey: "mqttHeatingSupplyTargetValid",
        staleCopy: "15 minuten",
        payloadInfoTitle: "Temperatuurpayload",
        payloadInfo: 'Publiceer live een aanvoertemperatuur in °C. Voorbeelden: 42.0, 42,0, 42.0 °C of {"value":42.0}. Geldig bereik: 20..70 °C. Retained berichten worden niet gebruikt voor regeling.',
      },
      {
        topicKey: "heating_enable",
        label: "Warmtetoestemming",
        valueKey: "mqttHeatingEnable",
        ageKey: "mqttHeatingEnableAge",
        validKey: "mqttHeatingEnableValid",
        staleCopy: "nieuw bericht",
        kind: "binary",
        stateful: true,
        payloadInfoTitle: "Booleanpayload",
        payloadInfo: 'Publiceer warmtetoestemming als boolean. Geaccepteerd: true/false, 1/0, on/off, yes/no of {"value":true}.',
      },
      {
        topicKey: "cooling_enable",
        label: "Koeltoestemming",
        valueKey: "mqttCoolingEnable",
        ageKey: "mqttCoolingEnableAge",
        validKey: "mqttCoolingEnableValid",
        staleCopy: "nieuw bericht",
        kind: "binary",
        stateful: true,
        payloadInfoTitle: "Booleanpayload",
        payloadInfo: 'Publiceer koeltoestemming als boolean. Geaccepteerd: true/false, 1/0, on/off, yes/no of {"value":true}.',
      },
    ];
  }

  export function formatMqttSensorValiditySummary(sensors = getMqttInputSensors()) {
    if (!sensors.length) {
      return "Geen sensoren";
    }
    const enabledSensors = sensors.filter((sensor) => isMqttInputEnabled(sensor.topicKey));
    const disabledCount = sensors.length - enabledSensors.length;
    if (!enabledSensors.length) {
      return `${disabledCount} ${disabledCount === 1 ? "topic" : "topics"} uitgeschakeld`;
    }
    const validCount = enabledSensors.filter((sensor) => isEntityActive(sensor.validKey)).length;
    const validityText = validCount === enabledSensors.length
      ? `${validCount} ${validCount === 1 ? "sensor" : "sensoren"} geldig`
      : `${validCount} van ${enabledSensors.length} sensoren geldig`;
    return disabledCount ? `${validityText} · ${disabledCount} uit` : validityText;
  }

  export function renderMqttModal() {
    const status = state.mqttStatus || {};
    const enabled = Boolean(state.mqttDraftEnabled);
    const clearPassword = Boolean(state.mqttDraftClearPassword);
    const passwordPlaceholder = status.password_set
      ? "Leeg laten om huidig wachtwoord te behouden"
      : "Optioneel";
    const noticeMarkup = state.mqttNotice
      ? `<div class="oq-helper-modal-success oq-helper-modal-success--compact" aria-live="polite"><strong>Status</strong><span>${escapeHtml(state.mqttNotice)}</span></div>`
      : "";
    const errorMarkup = state.mqttError
      ? `<div class="oq-helper-modal-note oq-helper-modal-note--error" aria-live="assertive">${escapeHtml(state.mqttError)}</div>`
      : "";

    return renderModalShell({
      id: "system",
      titleId: "oq-mqtt-modal-title",
      kicker: "Integratie",
      title: "MQTT brokerconfiguratie",
      copy: "Stel de broker in waarop OpenQuatt MQTT-inputs beluistert.",
      closeAction: "close-system-modal",
      closeLabel: "Sluit MQTT brokerconfiguratie",
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
              <span>MQTT inputbronnen inschakelen</span>
            </label>
            <label class="oq-helper-modal-auth-field oq-settings-mqtt-field">
              <span>Broker</span>
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
              <span>Poort</span>
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
              <span>Gebruikersnaam</span>
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
              <span>Wachtwoord</span>
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
                <span>Opgeslagen wachtwoord wissen</span>
              </label>
            ` : ""}
          </div>`,
      actions: `
        <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${state.mqttBusy ? "disabled" : ""}>Gereed</button>
        <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="save-mqtt-config" ${state.mqttBusy || !status.csrf_token ? "disabled" : ""}>${state.mqttBusy ? "Opslaan..." : "Opslaan"}</button>
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
      const topicDisplay = topic || "Wordt geladen...";
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
      const statusLabel = inputEnabled ? (valid ? "geldig" : "ongeldig") : "uit";
      const validityTitle = inputEnabled ? getMqttValidityLabel(sensor.validKey) : "Uitgeschakeld";
      const statusTitle = inputEnabled
        ? valid
          ? sensor.stateful
            ? acceptRetained
              ? `Laatste MQTT-publicatie ${age === "—" ? "onbekend" : `${age} geleden`}. De waarde blijft geldig tot een nieuwe payload, uitschakelen of herstart.`
              : `Laatste live MQTT-publicatie ${age === "—" ? "onbekend" : `${age} geleden`}. De waarde blijft maximaal ${nonRetainedTimeoutMinutes} minuten geldig en vervalt bij een MQTT-disconnect.`
            : `Laatste MQTT-publicatie ${age === "—" ? "onbekend" : `${age} geleden`}. Zonder nieuwe MQTT-publicatie wordt de waarde na ${sensor.staleCopy} ongeldig.`
          : age === "—"
            ? "Nog geen geldige MQTT-publicatie ontvangen."
            : `Laatste MQTT-publicatie ${age} geleden; de waarde is niet meer geldig.`
        : "Dit topic wordt niet gebruikt. OpenQuatt subscribed er niet op.";
      const toggleTitle = inputEnabled ? "Topic uitschakelen" : "Topic gebruiken";
      const retainedTitle = "Retained MQTT-waarde: ontvangen bij verbinden met de broker.";
      const retainedBehavior = acceptRetained
        ? "Brokerwaarde wordt na reconnect of herstart opnieuw gebruikt."
        : `Alleen live waarden; maximaal ${nonRetainedTimeoutMinutes} minuten geldig en direct ongeldig bij disconnect.`;
      const payloadInfo = sensor.stateful
        ? `${sensor.payloadInfo} ${acceptRetained
            ? "Retained berichten worden geaccepteerd."
            : `Retained berichten worden genegeerd; live waarden verlopen na ${nonRetainedTimeoutMinutes} minuten.`}`
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
                    <strong>Retained waarde gebruiken</strong>
                    <small>${escapeHtml(retainedBehavior)}</small>
                  </span>
                  <button
                    class="oq-settings-toggle-switch oq-settings-mqtt-retained-toggle${acceptRetained ? " is-on" : ""}"
                    type="button"
                    data-oq-action="toggle-mqtt-retained"
                    data-oq-mqtt-topic-key="${escapeHtml(sensor.topicKey)}"
                    aria-pressed="${acceptRetained ? "true" : "false"}"
                    aria-label="${escapeHtml(`${sensor.label}: retained waarde ${acceptRetained ? "uitschakelen" : "gebruiken"}`)}"
                    title="${acceptRetained ? "Retained waarde negeren" : "Retained waarde gebruiken"}"
                    ${busy || !state.mqttStatus?.csrf_token ? "disabled" : ""}
                  >
                    <span class="oq-settings-toggle-switch-track"><span class="oq-settings-toggle-switch-knob"></span></span>
                  </button>
                </div>
              ` : ""}
              <div class="oq-settings-mqtt-sensor-topic-head">
                <span class="oq-settings-mqtt-sensor-topic-label">Subscribe-topic</span>
              </div>
              <div class="oq-settings-mqtt-topic-row">
                <div class="oq-settings-mqtt-topic-field${copied ? " is-copied" : ""}">
                  <code>${escapeHtml(topicDisplay)}</code>
                  <button
                    class="oq-settings-mqtt-topic-copy"
                    type="button"
                    data-oq-action="copy-mqtt-topic"
                    data-oq-mqtt-topic-key="${escapeHtml(sensor.topicKey)}"
                    aria-label="${escapeHtml(copied ? `MQTT-topic voor ${sensor.label} gekopieerd` : `Kopieer MQTT-topic voor ${sensor.label}`)}"
                    title="${copied ? "Gekopieerd" : "Kopieer topic"}"
                    ${!topic ? "disabled" : ""}
                  >
                    ${renderOqIcon(copied ? "clipboard-check" : "clipboard", "oq-settings-mqtt-topic-copy-icon")}
                  </button>
                </div>
                <details class="oq-settings-mqtt-topic-info">
                  <summary aria-label="${escapeHtml(`Payloadinformatie voor ${sensor.label}`)}">i</summary>
                  <div class="oq-settings-mqtt-topic-info-popover">
                    <strong>${escapeHtml(sensor.payloadInfoTitle || "Payload")}</strong>
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
      kicker: "Integratie",
      title: "MQTT sensoren",
      className: "oq-helper-modal--mqtt-sensors",
      headerMarkup: `<div class="oq-settings-mqtt-modal-head">
            <span class="oq-settings-mqtt-modal-icon">${renderMqttLogoIcon("oq-settings-mqtt-modal-logo")}</span>
            <div>
              <p class="oq-helper-modal-kicker">Integratie</p>
              <h2 class="oq-helper-modal-title" id="oq-mqtt-sensors-modal-title">MQTT sensoren</h2>
            </div>
            <button class="oq-helper-modal-close" type="button" data-oq-action="close-system-modal" aria-label="Sluit MQTT sensoren">×</button>
          </div>`,
      body: `
          ${errorMarkup}
          <div class="oq-settings-mqtt-sensor-table">
            <div class="oq-settings-mqtt-sensor-table-head" aria-hidden="true">
              <span>Sensor</span>
              <span>Waarde</span>
              <span>Status</span>
              <span></span>
              <span></span>
            </div>
            ${sensorMarkup}
          </div>
          <div class="oq-settings-mqtt-sensor-footer">
            <span>${escapeHtml(sensorValiditySummary)}</span>
            <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-system-modal">Gereed</button>
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
