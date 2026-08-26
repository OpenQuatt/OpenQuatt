import { getEntityNumericValue, getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";
import { renderOqIcon } from "../core/config.js";
import { formatOpenQuattResumeDateTime, getEntityValue, getOpenQuattPauseDraftValue, hasOpenQuattResumeSchedule } from "../core/entity-store.js";
import { isDeviceReconnectRecovering } from "../core/device-reconnect.js";
import { setHeaderRenderControls } from "../core/header-render-controls.js";
import { renderModalShell } from "../core/modal-shell.js";
import { getEntitySignatureFragment } from "../core/render-signatures.js";
import { state } from "../core/state.js";
import { getDebugRecordingHubStatusLabel, renderDebugRecordingHeaderStatus, renderDebugRecordingModal } from "./debug-recording.js";
import { formatDeviceClock, formatUptimeFromMeta, getDeviceIpAddress, getInstallationLabel } from "./device-context.js";
import { getFirmwareUpdateEntity, getUpdateStatus, isFirmwareUpdateAvailable } from "./firmware-update.js";
import { renderMqttModal, renderMqttSensorsModal } from "./mqtt.js";
import { renderOduEepromDumpModal } from "./odu-eeprom-dump.js";
import { renderApiSecurityModal, renderLoginModal } from "./security-access.js";
import { getWebServerLogStatusLabel, renderWebServerLogsModal } from "./webserver-logs.js";
import { getControlModeOverrideLabel, renderSettingsServiceTaskModal } from "../settings/service.js";
import { renderSilentSettingsFields } from "../settings/silent.js";
import { renderSettingsBackupImportModal, renderSettingsBackupRestoreModal, renderSettingsHistoryStorageModal } from "../settings/storage.js";
import { renderHpWaterSensorOffsetsModal } from "../settings/water.js";
import { renderHeatingStrategyAdviceModal } from "./heating-strategy-advice.js";
import { formatNumericState } from "../core/formatting.js";
import { escapeHtml } from "../core/html.js";
import { render } from "../core/render-scheduler.js";

  export function getHeaderRenderSignature() {
    return [
      state.interfacePanelOpen ? "open" : "closed",
      state.nativeOpen ? "native" : "app",
      state.appView,
      state.complete ? "complete" : "incomplete",
      state.overviewTheme,
      state.hpVisualMode,
      getEntitySignatureFragment("installationTopology"),
      getEntitySignatureFragment("hardwareProfileText"),
      getEntitySignatureFragment("connectionText"),
      state.firmwareAdvancedOpen ? "firmware-advanced-open" : "firmware-advanced-closed",
      state.firmwareConnectionSwitchOpen ? "connection-open" : "connection-closed",
      state.firmwareTopologySwitchOpen ? "topology-open" : "topology-closed",
      state.updateManualUploadOpen ? "upload-open" : "upload-closed",
      state.updateTestFirmwareOpen ? "test-open" : "test-closed",
      state.updateTestFirmwareError,
      getEntitySignatureFragment("hpGeneration"),
      getEntitySignatureFragment("projectVersionText"),
      getEntitySignatureFragment("releaseChannelText"),
      getEntitySignatureFragment("controlModeOverride"),
      getConnectivityStatus(),
    ].join("|");
  }

  export function renderControlModeOverrideBanner() {
    if (!hasEntity("controlModeOverride")) {
      return "";
    }
    const value = String(getEntityValue("controlModeOverride") || "Auto");
    if (value === "Auto") {
      return "";
    }
    const busy = state.busyAction === "save-controlModeOverride";
    const feedbackMarkup = String(state.controlError || "").startsWith("CM Override")
      ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>`
      : "";
    return `
      <aside class="oq-control-mode-override-banner" role="status" aria-live="polite">
        <div>
          <span>Testmodus actief</span>
          <strong>${escapeHtml(getControlModeOverrideLabel(value))}</strong>
          <p>De normale moduskeuze is tijdelijk overruled. De controller keert uiterlijk 30 minuten na activering automatisch terug naar automatisch.</p>
          ${feedbackMarkup}
        </div>
        <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="clear-control-mode-override" ${busy ? "disabled" : ""}>
          ${busy ? "Bezig..." : "Terug naar automatisch"}
        </button>
      </aside>
    `;
  }

  export function getConnectivityStatus() {
    const lastEntityResponseAt = Math.max(Number(state.lastEntityResponseAt || 0), Number(state.lastEntitySyncAt || 0));
    const reconnectStartedAt = Number(state.deviceReconnectStartedAt || 0);
    if (state.entitySyncFailureCount > 0 && !state.deviceReconnectMode) {
      return "Bezig";
    }
    if (lastEntityResponseAt > 0 && (!state.deviceReconnectMode || lastEntityResponseAt >= reconnectStartedAt)) {
      return "Verbonden";
    }
    if (state.deviceReconnectMode) {
      if (isDeviceReconnectRecovering()) {
        return "Verbonden";
      }
      return state.deviceReconnectMode === "reconnect" ? "Offline" : "Bezig";
    }
    if (hasEntity("status") && !isEntityActive("status")) {
      return "Offline";
    }
    return "Bezig";
  }

  export function getDeviceVersionLabel() {
    const version = String(getEntityValue("projectVersionText") || "").trim();
    return version || "—";
  }

  export function getFirmwareVersionChipValue() {
    const version = getDeviceVersionLabel();
    if (version && version !== "—") {
      return version;
    }
    return getUpdateStatus();
  }

  export function getEspTemperatureLabel() {
    const entity = state.entities.espInternalTemp;
    if (!entity) {
      return "—";
    }
    const numeric = getEntityNumericValue("espInternalTemp");
    if (!Number.isNaN(numeric)) {
      return formatNumericState(numeric, 1, entity.uom || " °C");
    }
    return getEntityStateText("espInternalTemp");
  }

  export function getConnectivityModalRows() {
    const rows = [
      ["Netwerkstatus", getConnectivityStatus()],
      ["IP-adres", getDeviceIpAddress()],
    ];
    const ssid = String(getEntityValue("wifiSsid") || "").trim();
    if (ssid) {
      rows.push(["WiFi SSID", ssid]);
    }
    const signalEntity = state.entities.wifiSignal;
    if (signalEntity) {
      const signal = getEntityNumericValue("wifiSignal");
      if (!Number.isNaN(signal)) {
        rows.push(["WiFi signaal", formatNumericState(signal, 0, signalEntity.uom || " dBm")]);
      }
    }
    return rows;
  }

  export function getHeaderStatusAction(key) {
    if (key === "version") {
      return "open-update-modal";
    }
    if (key === "connectivity") {
      return "open-connectivity-modal";
    }
    if (key === "debugRecording") {
      return "open-debug-recording-modal";
    }
    if (key === "webserverLog") {
      return "open-webserver-log-modal";
    }
    if (key === "login") {
      return "open-login-modal";
    }
    return "";
  }

  export function getHeaderStatusItems() {
    return [
      ["installation", "Installatie", getInstallationLabel()],
      ["uptime", "Uptime", formatUptimeFromMeta()],
      ["connectivity", "Connectiviteit", getConnectivityStatus()],
      ["time", "Tijd", formatDeviceClock()],
      ["version", "Versie", getFirmwareVersionChipValue(), Boolean(getFirmwareUpdateEntity())],
      ["debugRecording", "Debugopname", getDebugRecordingHubStatusLabel(), true],
      ["webserverLog", "Logboek", getWebServerLogStatusLabel(), true],
    ];
  }

  export function hasFirmwareUpdateAttention() {
    return isFirmwareUpdateAvailable();
  }

  export function hasHeaderStatusBadge(key) {
    return key === "version" && hasFirmwareUpdateAttention();
  }

  export function renderHeaderStatusGrid() {
    const statusItems = getHeaderStatusItems();

    return `
      <div class="oq-helper-status-grid">
        ${statusItems.map(([key, label, value, interactive]) => {
          const action = getHeaderStatusAction(key);
          const isInteractive = Boolean(interactive || action);
          const hasBadge = hasHeaderStatusBadge(key);
          return `
          <${isInteractive ? "button" : "div"}
            class="oq-helper-status-item${isInteractive ? " oq-helper-status-item--button" : ""}${hasBadge ? " oq-helper-status-item--attention" : ""}"
            data-oq-header-status="${escapeHtml(key)}"
            ${isInteractive ? `type="button" data-oq-action="${escapeHtml(action)}"` : ""}
          >
            <span class="oq-helper-status-label">${escapeHtml(label)}</span>
            <strong class="oq-helper-status-value">${hasBadge ? `<span class="oq-helper-status-value-text">${escapeHtml(value)}</span><span class="oq-helper-status-badge" aria-label="Update beschikbaar" title="Update beschikbaar"></span>` : escapeHtml(value)}</strong>
          </${isInteractive ? "button" : "div"}>
        `;
        }).join("")}
      </div>
    `;
  }

  export function patchHeaderDom() {
    if (!state.root) {
      return false;
    }

    const statusGrid = state.root.querySelector(".oq-helper-status-grid");
    if (!statusGrid) {
      return Boolean(state.root.querySelector(".oq-helper-hub"));
    }

    const statusItems = getHeaderStatusItems();
    const renderedItems = statusGrid.querySelectorAll("[data-oq-header-status]");
    if (renderedItems.length !== statusItems.length) {
      statusGrid.outerHTML = renderHeaderStatusGrid();
      return true;
    }

    for (const [key, label, value, interactive] of statusItems) {
      const item = statusGrid.querySelector(`[data-oq-header-status="${key}"]`);
      if (!item) {
        statusGrid.outerHTML = renderHeaderStatusGrid();
        return true;
      }
      const action = getHeaderStatusAction(key);
      const isInteractive = Boolean(interactive || action);
      if (item.tagName.toLowerCase() !== (isInteractive ? "button" : "div")) {
        statusGrid.outerHTML = renderHeaderStatusGrid();
        return true;
      }

      const labelNode = item.querySelector(".oq-helper-status-label");
      const valueNode = item.querySelector(".oq-helper-status-value");
      if (!labelNode || !valueNode) {
        statusGrid.outerHTML = renderHeaderStatusGrid();
        return true;
      }

      if (labelNode.textContent !== label) {
        labelNode.textContent = label;
      }
      const hasBadge = hasHeaderStatusBadge(key);
      const desiredValueMarkup = hasBadge
        ? `<span class="oq-helper-status-value-text">${escapeHtml(value)}</span><span class="oq-helper-status-badge" aria-label="Update beschikbaar" title="Update beschikbaar"></span>`
        : escapeHtml(value);
      if (valueNode.innerHTML !== desiredValueMarkup) {
        valueNode.innerHTML = desiredValueMarkup;
      }
      if (isInteractive) {
        item.setAttribute("data-oq-action", action);
      } else {
        item.removeAttribute("data-oq-action");
      }
      item.classList.toggle("oq-helper-status-item--button", isInteractive);
      item.classList.toggle("oq-helper-status-item--attention", hasBadge);
    }

    return true;
  }

  export function renderHeaderDevControls() {
    if (!__OQ_PREVIEW__) {
      return "";
    }
    const controls = typeof window !== "undefined" ? window.__OQ_DEV_CONTROLS__ : null;
    if (!controls || typeof controls.render !== "function") {
      return "";
    }
    return controls.render();
  }

  export function renderDevPanel() {
    if (!__OQ_PREVIEW__) {
      return "";
    }
    const controlsMarkup = renderHeaderDevControls();
    if (!controlsMarkup) {
      return "";
    }

    if (!state.devPanelOpen) {
      return `
        <aside class="oq-helper-devdock oq-helper-devdock--collapsed" aria-label="Preview en test">
          <button
            class="oq-helper-devdock-toggle"
            type="button"
            data-oq-action="toggle-dev-panel"
            aria-expanded="false"
            aria-label="Open previewpaneel"
          >Preview</button>
        </aside>
      `;
    }

    return `
      <aside class="oq-helper-devdock" aria-label="Preview en test">
        <div class="oq-helper-devdock-head">
          <div>
            <p class="oq-helper-devdock-kicker">Preview en test</p>
            <h2 class="oq-helper-devdock-title">Mockbediening</h2>
          </div>
          <button
            class="oq-helper-devdock-toggle oq-helper-devdock-toggle--close"
            type="button"
            data-oq-action="toggle-dev-panel"
            aria-expanded="true"
            aria-label="Sluit previewpaneel"
          >×</button>
        </div>
        ${controlsMarkup}
      </aside>
    `;
  }

  export function renderHeaderStatus() {
    const surface = state.nativeOpen ? "native" : "app";
    const hasUpdateAttention = hasFirmwareUpdateAttention();
    if (!state.interfacePanelOpen) {
      const debugRecordingStatus = renderDebugRecordingHeaderStatus();
      return `
        <aside class="oq-helper-hub oq-helper-hub--collapsed" aria-label="Weergave en systeem">
          <div class="oq-helper-hub-head-actions">
            ${debugRecordingStatus}
            <button
              class="oq-helper-hub-toggle${hasUpdateAttention ? " oq-helper-hub-toggle--attention" : ""}"
              type="button"
              data-oq-action="toggle-interface-panel"
              aria-expanded="false"
              aria-label="Open interfacepaneel"
              title="Open interfacepaneel"
            >${renderOqIcon("more-horizontal", "oq-helper-hub-toggle-icon")}${hasUpdateAttention ? '<span class="oq-helper-hub-toggle-dot" aria-hidden="true"></span>' : ""}</button>
          </div>
        </aside>
      `;
    }

    return `
      <aside class="oq-helper-hub" aria-label="Weergave en systeem">
        <div class="oq-helper-hub-head">
          <h2 class="oq-helper-hub-title">Weergave en systeem</h2>
          <div class="oq-helper-hub-head-actions">
            <button
              class="oq-helper-hub-toggle oq-helper-hub-toggle--close"
              type="button"
              data-oq-action="toggle-interface-panel"
              aria-expanded="true"
              aria-label="Sluit interfacepaneel"
              title="Sluit interfacepaneel"
            >×</button>
          </div>
        </div>
        <div class="oq-helper-hub-block">
          <p class="oq-helper-hub-kicker">Weergave</p>
          <div class="oq-helper-hub-switches">
            <button class="oq-helper-hub-chip${surface === "app" ? " is-active" : ""}" type="button" data-oq-action="select-surface" data-surface="app">OpenQuatt-app</button>
            <button class="oq-helper-hub-chip${surface === "native" ? " is-active" : ""}" type="button" data-oq-action="select-surface" data-surface="native">ESPHome fallback</button>
          </div>
        </div>
        <div class="oq-helper-hub-block">
          <p class="oq-helper-hub-kicker">Uiterlijk en overzicht</p>
          <div class="oq-helper-hub-actions">
            <button class="oq-helper-button oq-helper-button--ghost oq-helper-hub-action" type="button" data-oq-action="toggle-overview-theme">
              ${state.overviewTheme === "light" ? "Donkere modus" : "Lichte modus"}
            </button>
          </div>
        </div>
        <div class="oq-helper-hub-block">
          <p class="oq-helper-hub-kicker">Systeem</p>
          ${renderHeaderStatusGrid()}
          <div class="oq-helper-hub-actions oq-helper-hub-actions--single">
            <button class="oq-helper-hub-action oq-helper-hub-action--warning" type="button" data-oq-action="open-restart-confirm">
              Herstart OpenQuatt
            </button>
          </div>
        </div>
      </aside>
    `;
  }

  export function renderNativeSurfaceShell() {
    const surface = state.nativeOpen ? "native" : "app";
    const statusCopy = state.nativeFrontendLoading
      ? "ESPHome fallback wordt geladen. Daarna blijft alleen de native webinterface actief."
      : "De OpenQuatt-app is tijdelijk uitgeschakeld, zodat de ESPHome fallback zelfstandig en zonder extra interfacebelasting kan draaien.";
    const errorMarkup = state.controlError
      ? `<p class="oq-native-surface-note oq-native-surface-note--error">${escapeHtml(state.controlError)}</p>`
      : "";

    return `
      <div class="oq-helper-shell oq-native-surface-shell">
        <div class="oq-helper-card oq-native-surface-card">
          <div class="oq-native-surface-head">
            <div class="oq-native-surface-copy">
              <p class="oq-helper-kicker">Weergave</p>
              <h1>ESPHome fallback actief</h1>
              <p>${escapeHtml(statusCopy)}</p>
            </div>
            <div class="oq-native-surface-controls">
              <div class="oq-helper-hub-switches">
                <button class="oq-helper-hub-chip${surface === "app" ? " is-active" : ""}" type="button" data-oq-action="select-surface" data-surface="app">OpenQuatt-app</button>
                <button class="oq-helper-hub-chip${surface === "native" ? " is-active" : ""}" type="button" data-oq-action="select-surface" data-surface="native">ESPHome fallback</button>
              </div>
            </div>
          </div>
          <p class="oq-native-surface-note">Schakel terug naar OpenQuatt-app om tuning, live overzicht en instellingen weer te activeren.</p>
          ${errorMarkup}
        </div>
      </div>
    `;
  }

  setHeaderRenderControls({
    getSignature: getHeaderRenderSignature,
    patch: patchHeaderDom,
  });

  export function renderSystemModal() {
    if (state.systemModal === "login") {
      return renderLoginModal();
    }

    if (state.systemModal === "api-security") {
      return renderApiSecurityModal();
    }

    if (state.systemModal === "mqtt") {
      return renderMqttModal();
    }

    if (state.systemModal === "mqtt-sensors") {
      return renderMqttSensorsModal();
    }

    if (state.systemModal === "connectivity") {
      const rows = getConnectivityModalRows();
      return renderModalShell({
        modalId: "system",
        titleId: "oq-system-modal-title",
        kicker: "Systeem",
        title: "Connectiviteit",
        closeAction: "close-system-modal",
        closeLabel: "Sluit systeem-popup",
        bodyMarkup: `
          <p class="oq-helper-modal-copy">Status en details van de actieve netwerkverbinding van OpenQuatt.</p>
          <div class="oq-helper-modal-grid">
            ${rows.map(([label, value]) => `
              <div class="oq-helper-modal-row">
                <span class="oq-helper-modal-label">${escapeHtml(label)}</span>
                <strong class="oq-helper-modal-value">${escapeHtml(value)}</strong>
              </div>
            `).join("")}
          </div>
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-system-modal">Gereed</button>
          </div>
        `,
      });
    }

    if (state.systemModal === "settings-backup-restore") {
      return renderSettingsBackupRestoreModal();
    }

    if (state.systemModal === "settings-backup-import") {
      return renderSettingsBackupImportModal();
    }

    if (state.systemModal === "history-storage") {
      return renderSettingsHistoryStorageModal();
    }

    if (state.systemModal === "water-sensor-corrections") {
      return renderHpWaterSensorOffsetsModal();
    }

    if (state.systemModal === "odu-eeprom-dump") {
      return renderOduEepromDumpModal();
    }

    if (String(state.systemModal || "").startsWith("service-task-")) {
      return renderSettingsServiceTaskModal();
    }

    if (state.systemModal === "settings-backup-success") {
      const notice = state.controlNotice || "Backup hersteld.";
      const result = state.settingsBackupRestoreResult || { applied: [], skipped: [], unknown: [], mqttIncluded: false };
      const resultItems = [...result.skipped, ...result.unknown];
      const resultDetails = resultItems.length ? `
        <details class="oq-settings-backup-result-details" open>
          <summary>
            <span>
              <strong>Niet toegepast</strong>
              <em>${escapeHtml(`${result.skipped.length} overgeslagen · ${result.unknown.length} onbekend`)}</em>
            </span>
          </summary>
          <div class="oq-settings-backup-result-list">
            ${resultItems.map((item) => `
              <div class="oq-settings-backup-result-item oq-settings-backup-result-item--${escapeHtml(item.severity || "warning")}">
                <div>
                  <strong>${escapeHtml(item.label || item.key)}</strong>
                  <code>${escapeHtml(`${item.section || "Onbekend"} · ${item.key}`)}</code>
                </div>
                <div>
                  <strong>${escapeHtml(item.reason || "Niet toegepast")}</strong>
                  ${item.detail ? `<span>${escapeHtml(item.detail)}</span>` : ""}
                </div>
              </div>
            `).join("")}
          </div>
        </details>
      ` : "";
      return renderModalShell({
        modalId: "system",
        titleId: "oq-backup-success-modal-title",
        kicker: "Beheer",
        title: "Backup hersteld",
        closeAction: "close-system-modal",
        closeLabel: "Sluit bevestiging",
        className: "oq-helper-modal--wide oq-helper-modal--scrollable",
        bodyMarkup: `
          <p class="oq-helper-modal-copy">${escapeHtml(notice)}</p>
          <div class="oq-settings-backup-result-summary">
            <div><span>Toegepast</span><strong>${escapeHtml(String(result.applied.length))}</strong></div>
            <div><span>Niet toegepast</span><strong>${escapeHtml(String(result.skipped.length))}</strong></div>
            <div><span>Onbekend</span><strong>${escapeHtml(String(result.unknown.length))}</strong></div>
          </div>
          ${resultDetails}
          ${result.mqttIncluded ? "" : `<p class="oq-settings-action-note oq-settings-action-note--warning">Deze backup bevatte geen MQTT-configuratie. Bestaande MQTT-instellingen en MQTT-afhankelijke bronselecties zijn behouden.</p>`}
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-system-modal">Gereed</button>
          </div>
        `,
      });
    }

    if (state.systemModal === "control-mode-override-confirm") {
      const option = String(state.pendingControlModeOverride || "");
      const busy = state.busyAction === "save-controlModeOverride";
      return renderModalShell({
        modalId: "system",
        titleId: "oq-control-mode-override-modal-title",
        kicker: "Service · tijdelijke testmodus",
        title: `${getControlModeOverrideLabel(option)} activeren?`,
        closeAction: "close-system-modal",
        closeLabel: "Sluit testmodus-popup",
        bodyMarkup: `
          <p class="oq-helper-modal-copy">Deze keuze omzeilt tijdelijk de normale regelmodus. Gebruik dit alleen voor een gerichte test en houd de installatie tijdens de test in de gaten.</p>
          <p class="oq-settings-action-note oq-settings-action-note--warning">De override stopt automatisch na maximaal 30 minuten. Je kunt hem eerder beëindigen via de waarschuwing bovenaan de web-app.</p>
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${busy ? "disabled" : ""}>Annuleren</button>
            <button class="oq-helper-button oq-helper-button--warning" type="button" data-oq-action="confirm-control-mode-override" ${busy ? "disabled" : ""}>${busy ? "Activeren..." : "Tijdelijk activeren"}</button>
          </div>
        `,
      });
    }

    if (state.systemModal === "runtime-reset-confirm") {
      const duo = hasEntity("resetRuntimeCountersHp1Hp2");
      const key = duo ? "resetRuntimeCountersHp1Hp2" : "resetRuntimeCountersHp1";
      const busy = state.busyAction === key;
      return renderModalShell({
        modalId: "system",
        titleId: "oq-runtime-reset-modal-title",
        kicker: "Onderhoud",
        title: `${duo ? "Beide draaitijdbalansen" : "Draaitijdbalans"} resetten?`,
        closeAction: "close-system-modal",
        closeLabel: "Sluit draaitijd-resetpopup",
        bodyMarkup: `
          <p class="oq-helper-modal-copy">De in OpenQuatt bijgehouden compressorlooptijd wordt op nul gezet${duo ? " voor beide warmtepompen" : ""}. Gebruik dit alleen na vervanging of wanneer de runtimebalans bewust opnieuw moet beginnen.</p>
          <p class="oq-settings-action-note oq-settings-action-note--warning">Dit wijzigt geen fysieke teller in de warmtepomp zelf. De nieuwe waarden kunnen binnen ongeveer één minuut zichtbaar worden.</p>
          ${state.controlError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>` : ""}
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${busy ? "disabled" : ""}>Annuleren</button>
            <button class="oq-helper-button oq-helper-button--warning" type="button" data-oq-action="confirm-runtime-reset" ${busy ? "disabled" : ""}>${busy ? "Resetten..." : "Tellers resetten"}</button>
          </div>
        `,
      });
    }

    if (state.systemModal === "energy-counter-reset-confirm") {
      const busy = state.busyAction === "resetCumulativeEnergyCounters";
      return renderModalShell({
        modalId: "system",
        titleId: "oq-energy-counter-reset-modal-title",
        kicker: "Onderhoud",
        title: "Cumulatieve energietellers resetten?",
        closeAction: "close-system-modal",
        closeLabel: "Sluit energieteller-resetpopup",
        bodyMarkup: `
          <p class="oq-helper-modal-copy">De cumulatieve elektriciteits-, warmte- en koelenergiemeters van OpenQuatt beginnen opnieuw bij nul.</p>
          <p class="oq-settings-action-note oq-settings-action-note--warning">Eerder opgebouwde totalen blijven niet beschikbaar in deze tellers. Externe historie in Home Assistant wordt hiermee niet verwijderd.</p>
          ${state.controlError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>` : ""}
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${busy ? "disabled" : ""}>Annuleren</button>
            <button class="oq-helper-button oq-helper-button--warning" type="button" data-oq-action="confirm-energy-counter-reset" ${busy ? "disabled" : ""}>${busy ? "Resetten..." : "Energietellers resetten"}</button>
          </div>
        `,
      });
    }

    if (state.systemModal === "restart-confirm") {
      const busy = state.busyAction === "restartAction";
      return renderModalShell({
        modalId: "system",
        titleId: "oq-restart-modal-title",
        kicker: "Systeem",
        title: "OpenQuatt herstarten?",
        closeAction: "close-system-modal",
        closeLabel: "Sluit herstart-popup",
        bodyMarkup: `
          <p class="oq-helper-modal-copy">De webinterface en regeling zijn tijdens de herstart kort niet bereikbaar. Daarna komt OpenQuatt vanzelf terug.</p>
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${busy ? "disabled" : ""}>Annuleren</button>
            <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="confirm-restart" ${busy ? "disabled" : ""}>${busy ? "Herstarten..." : "Herstarten"}</button>
          </div>
        `,
      });
    }

    if (state.systemModal === "silent-settings") {
      return renderModalShell({
        modalId: "system",
        titleId: "oq-silent-settings-modal-title",
        kicker: "Stille uren",
        title: "Stille uren instellen",
        modalClass: "oq-helper-modal--wide",
        closeAction: "close-system-modal",
        closeLabel: "Sluit stille-uren-popup",
        bodyMarkup: `
          <p class="oq-helper-modal-copy">Kies wanneer het systeem stiller moet werken, en hoe ver het dan nog mag opschalen. Wijzigingen worden direct toegepast.</p>
          <div class="oq-helper-modal-body">
            ${renderSilentSettingsFields()}
          </div>
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-system-modal">Gereed</button>
          </div>
        `,
      });
    }

    if (state.systemModal === "webserver-logs") {
      return renderWebServerLogsModal();
    }

    if (state.systemModal === "debug-recording") {
      return renderDebugRecordingModal();
    }

    if (state.systemModal === "heating-strategy-advice") {
      return renderHeatingStrategyAdviceModal();
    }

    if (state.systemModal === "openquatt-pause") {
      const enabled = isEntityActive("openquattEnabled");
      const busy = state.busyAction === "openquatt-regulation";
      const hasResumeEntity = hasEntity("openquattResumeAt");
      const resumeEntityPending = state.loadingEntities || state.entitySyncInFlight;
      const resumeEntityReady = hasResumeEntity || !resumeEntityPending;
      const resumeScheduled = hasOpenQuattResumeSchedule();
      const resumeLabel = formatOpenQuattResumeDateTime(getEntityValue("openquattResumeAt"));
      const draftValue = getOpenQuattPauseDraftValue();
      return renderModalShell({
        modalId: "system",
        titleId: "oq-openquatt-pause-modal-title",
        kicker: "Bediening",
        title: "Openquatt regeling",
        modalClass: "oq-helper-modal--wide",
        closeAction: "close-system-modal",
        closeLabel: "Sluit regeling-popup",
        bodyMarkup: `
          <p class="oq-helper-modal-copy">${enabled
              ? "Kies hoe lang de regeling uit moet blijven. Verwarmen en koelen stoppen dan, maar beveiligingen (inclusief vorstbeveiliging) blijven actief."
              : "De regeling staat nu tijdelijk uit. Je kunt meteen weer inschakelen of een nieuw hervatmoment plannen."
          }</p>
          ${resumeScheduled
            ? `<div class="oq-helper-modal-success oq-helper-modal-success--compact">
                <strong>Hervat nu automatisch</strong>
                <span>${escapeHtml(resumeLabel)}</span>
              </div>`
            : ""
          }
          ${!resumeEntityReady
            ? `<p class="oq-helper-modal-note" aria-live="polite">Hervatopties laden...</p>`
            : hasResumeEntity
            ? `
              <div class="oq-helper-modal-presets">
                <button class="oq-helper-button" type="button" data-oq-action="apply-openquatt-preset" data-pause-preset="2h" ${busy ? "disabled" : ""}>2 uur</button>
                <button class="oq-helper-button" type="button" data-oq-action="apply-openquatt-preset" data-pause-preset="8h" ${busy ? "disabled" : ""}>8 uur</button>
                <button class="oq-helper-button" type="button" data-oq-action="apply-openquatt-preset" data-pause-preset="tomorrow-morning" ${busy ? "disabled" : ""}>Tot morgenochtend</button>
              </div>
              <div class="oq-helper-modal-channel oq-helper-modal-channel--datetime">
                <span class="oq-helper-modal-label">Hervatten op</span>
                <div class="oq-helper-modal-inline">
                  <label class="oq-settings-control oq-settings-control--datetime">
                    <input
                      class="oq-helper-input"
                      type="datetime-local"
                      step="60"
                      lang="nl-NL"
                      data-oq-field="openquattPauseDraft"
                      data-oq-pause-draft="resume"
                      value="${escapeHtml(draftValue)}"
                      ${busy ? "disabled" : ""}
                    >
                    <span class="oq-settings-time-icon" aria-hidden="true">
                      <svg viewBox="0 0 20 20" focusable="false">
                        <rect x="3.2" y="4.2" width="13.6" height="12.6" rx="2.4" fill="none" stroke="currentColor" stroke-width="1.5" />
                        <path d="M6.2 2.9V5.4M13.8 2.9V5.4M3.8 8.1H16.2M10 10.3V13.1L12.3 14.4" fill="none" stroke="currentColor" stroke-width="1.5" stroke-linecap="round" stroke-linejoin="round" />
                      </svg>
                    </span>
                  </label>
                  <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="apply-openquatt-custom-pause" ${busy ? "disabled" : ""}>Plan moment</button>
                </div>
              </div>
            `
            : `<p class="oq-helper-modal-note">Automatisch hervatten is nog niet beschikbaar op deze firmware. Je kunt de regeling wel zonder eindtijd uitschakelen.</p>`
          }
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${busy ? "disabled" : ""}>Annuleren</button>
            ${!enabled
              ? `<button class="oq-helper-button" type="button" data-oq-action="enable-openquatt-now" ${busy ? "disabled" : ""}>Nu inschakelen</button>`
              : ""
            }
            <button class="oq-helper-button" type="button" data-oq-action="apply-openquatt-indefinite" ${busy ? "disabled" : ""}>${enabled ? "Zonder eindtijd uitschakelen" : "Zonder eindtijd"}</button>
          </div>
        `,
      });
    }

    return "";
  }
