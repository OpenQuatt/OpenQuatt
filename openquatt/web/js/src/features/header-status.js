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
import { renderOduRuntimeFrequencyModal } from "./odu-runtime-frequency.js";
import { renderOduSettingsModal } from "./odu-settings.js";
import { renderApiSecurityModal, renderLoginModal } from "./security-access.js";
import { getWebServerLogStatusLabel, renderWebServerLogsModal } from "./webserver-logs.js";
import { getControlModeOverrideLabel, renderSettingsServiceTaskModal } from "../settings/service.js";
import { renderCoolingScheduleSettingsFields } from "../settings/cooling.js";
import { renderSilentSettingsFields } from "../settings/silent.js";
import { renderSettingsBackupImportModal, renderSettingsBackupRestoreModal, renderSettingsHistoryStorageModal } from "../settings/storage.js";
import { renderHpWaterSensorOffsetsModal } from "../settings/water.js";
import { renderSettingsSelectField } from "../settings/controls.js";
import { formatDutchAmps } from "../settings/electrical-limit.js";
import { renderHeatingStrategyAdviceModal } from "./heating-strategy-advice.js";
import { formatNumericState } from "../core/formatting.js";
import { escapeHtml } from "../core/html.js";
import { getIntlLocale, getLocale, t } from "../i18n/index.js";
import { render } from "../core/render-scheduler.js";

  export function getHeaderRenderSignature() {
    return [
      state.interfacePanelOpen ? "open" : "closed",
      getLocale(),
      state.nativeOpen ? "native" : "app",
      state.appView,
      state.complete ? "complete" : "incomplete",
      state.overviewTheme,
      state.hpVisualMode,
      getEntitySignatureFragment("installationTopology"),
      getEntitySignatureFragment("hardwareProfileText"),
      getEntitySignatureFragment("connectionText"),
      getEntitySignatureFragment("preferredConnection"),
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
          <span>${escapeHtml(t("header.testModeActive"))}</span>
          <strong>${escapeHtml(getControlModeOverrideLabel(value))}</strong>
          <p>${escapeHtml(t("header.overrideCopy"))}</p>
          ${feedbackMarkup}
        </div>
        <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="clear-control-mode-override" ${busy ? "disabled" : ""}>
          ${busy ? escapeHtml(t("common.busy")) : escapeHtml(t("header.backToAuto"))}
        </button>
      </aside>
    `;
  }

  export function getConnectivityStatus() {
    const lastEntityResponseAt = Math.max(Number(state.lastEntityResponseAt || 0), Number(state.lastEntitySyncAt || 0));
    const reconnectStartedAt = Number(state.deviceReconnectStartedAt || 0);
    if (state.entitySyncFailureCount > 0 && !state.deviceReconnectMode) {
      return t("header.statusBusy");
    }
    if (lastEntityResponseAt > 0 && (!state.deviceReconnectMode || lastEntityResponseAt >= reconnectStartedAt)) {
      return t("header.statusConnected");
    }
    if (state.deviceReconnectMode) {
      if (isDeviceReconnectRecovering()) {
        return t("header.statusConnected");
      }
      return state.deviceReconnectMode === "reconnect" ? t("header.statusOffline") : t("header.statusBusy");
    }
    if (hasEntity("status") && !isEntityActive("status")) {
      return t("header.statusOffline");
    }
    return t("header.statusBusy");
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
      [t("header.connNetworkStatus"), getConnectivityStatus()],
    ];
    const hasActiveConnection = hasEntity("connectionText");
    const activeConnection = getEntityStateText("connectionText", t("header.connNotConnected")).replace("Not connected", t("header.connNotConnected"));
    if (hasActiveConnection) {
      rows.push([t("header.connActiveConnection"), activeConnection]);
    }
    rows.push([t("header.connIpAddress"), getDeviceIpAddress()]);
    const showWifiDetails = !hasActiveConnection || activeConnection === "WiFi";
    const ssid = String(getEntityValue("wifiSsid") || "").trim();
    if (showWifiDetails && ssid) {
      rows.push([t("header.connWifiSsid"), ssid]);
    }
    const signalEntity = state.entities.wifiSignal;
    if (showWifiDetails && signalEntity) {
      const signal = getEntityNumericValue("wifiSignal");
      if (!Number.isNaN(signal)) {
        rows.push([t("header.connWifiSignal"), formatNumericState(signal, 0, signalEntity.uom || " dBm")]);
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
      ["installation", t("header.itemInstallation"), getInstallationLabel()],
      ["uptime", t("header.itemUptime"), formatUptimeFromMeta()],
      ["connectivity", t("header.itemConnectivity"), getConnectivityStatus()],
      ["time", t("header.itemTime"), formatDeviceClock()],
      ["version", t("header.itemVersion"), getFirmwareVersionChipValue(), Boolean(getFirmwareUpdateEntity())],
      ["debugRecording", t("header.itemDebugRecording"), getDebugRecordingHubStatusLabel(), true],
      ["webserverLog", t("header.itemLog"), getWebServerLogStatusLabel(), true],
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
            <strong class="oq-helper-status-value">${hasBadge ? `<span class="oq-helper-status-value-text">${escapeHtml(value)}</span><span class="oq-helper-status-badge" aria-label="${escapeHtml(t("header.updateAvailable"))}" title="${escapeHtml(t("header.updateAvailable"))}"></span>` : escapeHtml(value)}</strong>
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
    if (state.systemModal === "connectivity") {
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
        ? `<span class="oq-helper-status-value-text">${escapeHtml(value)}</span><span class="oq-helper-status-badge" aria-label="${escapeHtml(t("header.updateAvailable"))}" title="${escapeHtml(t("header.updateAvailable"))}"></span>`
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
        <aside class="oq-helper-devdock oq-helper-devdock--collapsed" aria-label="${escapeHtml(t("header.devCollapsedLabel"))}">
          <button
            class="oq-helper-devdock-toggle"
            type="button"
            data-oq-action="toggle-dev-panel"
            aria-expanded="false"
            aria-label="${escapeHtml(t("header.devOpenLabel"))}"
          >${escapeHtml(t("header.devToggle"))}</button>
        </aside>
      `;
    }

    return `
      <aside class="oq-helper-devdock" aria-label="${escapeHtml(t("header.devCollapsedLabel"))}">
        <div class="oq-helper-devdock-head">
          <div>
            <p class="oq-helper-devdock-kicker">${escapeHtml(t("header.devKicker"))}</p>
            <h2 class="oq-helper-devdock-title">${escapeHtml(t("header.devTitle"))}</h2>
          </div>
          <button
            class="oq-helper-devdock-toggle oq-helper-devdock-toggle--close"
            type="button"
            data-oq-action="toggle-dev-panel"
            aria-expanded="true"
            aria-label="${escapeHtml(t("header.devCloseLabel"))}"
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
        <aside class="oq-helper-hub oq-helper-hub--collapsed" aria-label="${escapeHtml(t("header.panelLabel"))}">
          <div class="oq-helper-hub-head-actions">
            ${debugRecordingStatus}
            <button
              class="oq-helper-hub-toggle${hasUpdateAttention ? " oq-helper-hub-toggle--attention" : ""}"
              type="button"
              data-oq-action="toggle-interface-panel"
              aria-expanded="false"
              aria-label="${escapeHtml(t("header.openPanel"))}"
              title="${escapeHtml(t("header.openPanel"))}"
            >${renderOqIcon("more-horizontal", "oq-helper-hub-toggle-icon")}${hasUpdateAttention ? '<span class="oq-helper-hub-toggle-dot" aria-hidden="true"></span>' : ""}</button>
          </div>
        </aside>
      `;
    }

    return `
      <aside class="oq-helper-hub" aria-label="${escapeHtml(t("header.panelLabel"))}">
        <div class="oq-helper-hub-head">
          <h2 class="oq-helper-hub-title">${escapeHtml(t("header.panelTitle"))}</h2>
          <div class="oq-helper-hub-head-actions">
            <button
              class="oq-helper-hub-toggle oq-helper-hub-toggle--close"
              type="button"
              data-oq-action="toggle-interface-panel"
              aria-expanded="true"
              aria-label="${escapeHtml(t("header.closePanel"))}"
              title="${escapeHtml(t("header.closePanel"))}"
            >×</button>
          </div>
        </div>
        <div class="oq-helper-hub-block">
          <p class="oq-helper-hub-kicker">${escapeHtml(t("header.displaySection"))}</p>
          <div class="oq-helper-hub-switches">
            <button class="oq-helper-hub-chip${surface === "app" ? " is-active" : ""}" type="button" data-oq-action="select-surface" data-surface="app">${escapeHtml(t("header.openquattApp"))}</button>
            <button class="oq-helper-hub-chip${surface === "native" ? " is-active" : ""}" type="button" data-oq-action="select-surface" data-surface="native">${escapeHtml(t("header.esphomeFallback"))}</button>
          </div>
        </div>
        <div class="oq-helper-hub-block">
          <p class="oq-helper-hub-kicker">${escapeHtml(t("header.languageSection"))}</p>
          <div class="oq-helper-hub-switches" role="group" aria-label="${escapeHtml(t("header.languageLabel"))}">
            <button class="oq-helper-hub-chip${getLocale() === "nl" ? " is-active" : ""}" type="button" data-oq-locale="nl">${escapeHtml(t("header.languageNl"))}</button>
            <button class="oq-helper-hub-chip${getLocale() === "en" ? " is-active" : ""}" type="button" data-oq-locale="en">${escapeHtml(t("header.languageEn"))}</button>
          </div>
        </div>
        <div class="oq-helper-hub-block">
          <p class="oq-helper-hub-kicker">${escapeHtml(t("header.appearanceSection"))}</p>
          <div class="oq-helper-hub-actions">
            <button class="oq-helper-button oq-helper-button--ghost oq-helper-hub-action" type="button" data-oq-action="toggle-overview-theme">
              ${state.overviewTheme === "light" ? escapeHtml(t("header.darkMode")) : escapeHtml(t("header.lightMode"))}
            </button>
          </div>
        </div>
        <div class="oq-helper-hub-block">
          <p class="oq-helper-hub-kicker">${escapeHtml(t("header.systemSection"))}</p>
          ${renderHeaderStatusGrid()}
          <div class="oq-helper-hub-actions oq-helper-hub-actions--single">
            <button class="oq-helper-hub-action oq-helper-hub-action--warning" type="button" data-oq-action="open-restart-confirm">
              ${escapeHtml(t("header.restart"))}
            </button>
          </div>
        </div>
      </aside>
    `;
  }

  export function renderNativeSurfaceShell() {
    const surface = state.nativeOpen ? "native" : "app";
    const statusCopy = state.nativeFrontendLoading
      ? t("header.nativeLoading")
      : t("header.nativeDisabled");
    const errorMarkup = state.controlError
      ? `<p class="oq-native-surface-note oq-native-surface-note--error">${escapeHtml(state.controlError)}</p>`
      : "";

    return `
      <div class="oq-helper-shell oq-native-surface-shell">
        <div class="oq-helper-card oq-native-surface-card">
          <div class="oq-native-surface-head">
            <div class="oq-native-surface-copy">
              <p class="oq-helper-kicker">${escapeHtml(t("header.nativeKicker"))}</p>
              <h1>${escapeHtml(t("header.nativeTitle"))}</h1>
              <p>${escapeHtml(statusCopy)}</p>
            </div>
            <div class="oq-native-surface-controls">
              <div class="oq-helper-hub-switches">
                <button class="oq-helper-hub-chip${surface === "app" ? " is-active" : ""}" type="button" data-oq-action="select-surface" data-surface="app">${escapeHtml(t("header.openquattApp"))}</button>
                <button class="oq-helper-hub-chip${surface === "native" ? " is-active" : ""}" type="button" data-oq-action="select-surface" data-surface="native">${escapeHtml(t("header.esphomeFallback"))}</button>
              </div>
            </div>
          </div>
          <p class="oq-native-surface-note">${escapeHtml(t("header.nativeBack"))}</p>
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
      const preferenceMarkup = renderSettingsSelectField(
        "preferredConnection",
        t("header.connModeTitle"),
        t("header.connModeCopy"),
      );
      const preferenceFeedback = state.controlError || state.controlNotice ||
        (state.busyAction === "save-preferredConnection" ? t("common.busy") : "");
      return renderModalShell({
        modalId: "system",
        titleId: "oq-system-modal-title",
        kicker: t("header.connKicker"),
        title: t("header.connTitle"),
        closeAction: "close-system-modal",
        closeLabel: t("header.connCloseLabel"),
        bodyMarkup: `
          <div class="oq-helper-modal-grid">
            ${rows.map(([label, value]) => `
              <div class="oq-helper-modal-row">
                <span class="oq-helper-modal-label">${escapeHtml(label)}</span>
                <strong class="oq-helper-modal-value">${escapeHtml(value)}</strong>
              </div>
            `).join("")}
            ${preferenceMarkup}
          </div>
          ${preferenceMarkup ? `<p class="oq-helper-modal-note">${t("header.connFallbackNote", { installUrl: "https://openquatt.github.io/OpenQuatt/install/" })}</p>` : ""}
          ${preferenceFeedback ? `<p class="${state.controlError ? "oq-helper-error" : "oq-helper-notice"}" role="status">${escapeHtml(preferenceFeedback)}</p>` : ""}
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-system-modal">${escapeHtml(t("header.done"))}</button>
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

    if (state.systemModal === "odu-bottom-plate-settings") {
      return renderOduSettingsModal();
    }

    if (state.systemModal === "odu-frequency-settings") {
      return renderOduRuntimeFrequencyModal();
    }

    if (String(state.systemModal || "").startsWith("service-task-")) {
      return renderSettingsServiceTaskModal();
    }

    if (state.systemModal === "settings-backup-success") {
      const notice = state.controlNotice || t("system.backupRestored");
      const result = state.settingsBackupRestoreResult || { applied: [], skipped: [], unknown: [], mqttIncluded: false };
      const resultItems = [...result.skipped, ...result.unknown];
      const resultDetails = resultItems.length ? `
        <details class="oq-settings-backup-result-details" open>
          <summary>
            <span>
              <strong>${escapeHtml(t("system.backupNotApplied"))}</strong>
              <em>${escapeHtml(t("system.backupSkippedUnknown", { skipped: result.skipped.length, unknown: result.unknown.length }))}</em>
            </span>
          </summary>
          <div class="oq-settings-backup-result-list">
            ${resultItems.map((item) => `
              <div class="oq-settings-backup-result-item oq-settings-backup-result-item--${escapeHtml(item.severity || "warning")}">
                <div>
                  <strong>${escapeHtml(item.label || item.key)}</strong>
                  <code>${escapeHtml(`${item.section || t("system.backupUnknownSection")} · ${item.key}`)}</code>
                </div>
                <div>
                  <strong>${escapeHtml(item.reason || t("system.backupNotApplied"))}</strong>
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
        kicker: t("system.backupKicker"),
        title: t("system.backupTitle"),
        closeAction: "close-system-modal",
        closeLabel: t("system.backupCloseLabel"),
        className: "oq-helper-modal--wide oq-helper-modal--scrollable",
        bodyMarkup: `
          <p class="oq-helper-modal-copy">${escapeHtml(notice)}</p>
          <div class="oq-settings-backup-result-summary">
            <div><span>${escapeHtml(t("system.backupApplied"))}</span><strong>${escapeHtml(String(result.applied.length))}</strong></div>
            <div><span>${escapeHtml(t("system.backupNotApplied"))}</span><strong>${escapeHtml(String(result.skipped.length))}</strong></div>
            <div><span>${escapeHtml(t("system.backupUnknown"))}</span><strong>${escapeHtml(String(result.unknown.length))}</strong></div>
          </div>
          ${resultDetails}
          ${result.mqttIncluded ? "" : `<p class="oq-settings-action-note oq-settings-action-note--warning">${escapeHtml(t("system.backupMqttMissing"))}</p>`}
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-system-modal">${escapeHtml(t("header.done"))}</button>
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
        kicker: t("system.overrideKicker"),
        title: t("system.overrideTitle", { label: getControlModeOverrideLabel(option) }),
        closeAction: "close-system-modal",
        closeLabel: t("system.overrideCloseLabel"),
        bodyMarkup: `
          <p class="oq-helper-modal-copy">${escapeHtml(t("system.overrideCopy"))}</p>
          <p class="oq-settings-action-note oq-settings-action-note--warning">${escapeHtml(t("system.overrideWarnCopy"))}</p>
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${busy ? "disabled" : ""}>${escapeHtml(t("common.cancel"))}</button>
            <button class="oq-helper-button oq-helper-button--warning" type="button" data-oq-action="confirm-control-mode-override" ${busy ? "disabled" : ""}>${busy ? escapeHtml(t("system.overrideBusy")) : escapeHtml(t("system.overrideConfirm"))}</button>
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
        kicker: t("system.runtimeKicker"),
        title: duo ? t("system.runtimeTitleBoth") : t("system.runtimeTitleSingle"),
        closeAction: "close-system-modal",
        closeLabel: t("system.runtimeCloseLabel"),
        bodyMarkup: `
          <p class="oq-helper-modal-copy">${escapeHtml(t("system.runtimeCopy", { duoSuffix: duo ? t("system.runtimeDuoSuffix") : "" }))}</p>
          <p class="oq-settings-action-note oq-settings-action-note--warning">${escapeHtml(t("system.runtimeWarnCopy"))}</p>
          ${state.controlError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>` : ""}
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${busy ? "disabled" : ""}>${escapeHtml(t("common.cancel"))}</button>
            <button class="oq-helper-button oq-helper-button--warning" type="button" data-oq-action="confirm-runtime-reset" ${busy ? "disabled" : ""}>${busy ? escapeHtml(t("system.runtimeBusy")) : escapeHtml(t("system.runtimeConfirm"))}</button>
          </div>
        `,
      });
    }

    if (state.systemModal === "energy-counter-reset-confirm") {
      const busy = state.busyAction === "resetCumulativeEnergyCounters";
      return renderModalShell({
        modalId: "system",
        titleId: "oq-energy-counter-reset-modal-title",
        kicker: t("system.energyKicker"),
        title: t("system.energyTitle"),
        closeAction: "close-system-modal",
        closeLabel: t("system.energyCloseLabel"),
        bodyMarkup: `
          <p class="oq-helper-modal-copy">${escapeHtml(t("system.energyCopy"))}</p>
          <p class="oq-settings-action-note oq-settings-action-note--warning">${escapeHtml(t("system.energyWarnCopy"))}</p>
          ${state.controlError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>` : ""}
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${busy ? "disabled" : ""}>${escapeHtml(t("common.cancel"))}</button>
            <button class="oq-helper-button oq-helper-button--warning" type="button" data-oq-action="confirm-energy-counter-reset" ${busy ? "disabled" : ""}>${busy ? escapeHtml(t("system.energyBusy")) : escapeHtml(t("system.energyConfirm"))}</button>
          </div>
        `,
      });
    }

    if (state.systemModal === "restart-confirm") {
      const busy = state.busyAction === "restartAction";
      return renderModalShell({
        modalId: "system",
        titleId: "oq-restart-modal-title",
        kicker: t("system.restartKicker"),
        title: t("system.restartTitle"),
        closeAction: "close-system-modal",
        closeLabel: t("system.restartCloseLabel"),
        bodyMarkup: `
          <p class="oq-helper-modal-copy">${escapeHtml(t("system.restartCopy"))}</p>
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${busy ? "disabled" : ""}>${escapeHtml(t("common.cancel"))}</button>
            <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="confirm-restart" ${busy ? "disabled" : ""}>${busy ? escapeHtml(t("system.restartBusy")) : escapeHtml(t("system.restartConfirm"))}</button>
          </div>
        `,
      });
    }

    if (state.systemModal === "factory-reset-confirm") {
      const busy = state.busyAction === "factoryResetButton";
      return renderModalShell({
        modalId: "system",
        titleId: "oq-factory-reset-modal-title",
        kicker: t("system.factoryKicker"),
        title: t("system.factoryTitle"),
        closeAction: "close-system-modal",
        closeLabel: t("system.factoryCloseLabel"),
        bodyMarkup: `
          <p class="oq-helper-modal-copy">${escapeHtml(t("system.factoryCopy"))}</p>
          ${state.controlError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>` : ""}
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${busy ? "disabled" : ""}>${escapeHtml(t("common.cancel"))}</button>
            <button class="oq-helper-button oq-helper-button--warning" type="button" data-oq-action="confirm-factory-reset" ${busy ? "disabled" : ""}>${busy ? escapeHtml(t("system.factoryBusy")) : escapeHtml(t("system.factoryConfirm"))}</button>
          </div>
        `,
      });
    }

    if (state.systemModal === "cooling-schedule") {
      return renderModalShell({
        modalId: "system",
        titleId: "oq-cooling-schedule-modal-title",
        kicker: t("system.coolingScheduleKicker"),
        title: t("system.coolingScheduleTitle"),
        modalClass: "oq-helper-modal--wide",
        closeAction: "close-system-modal",
        closeLabel: t("system.coolingScheduleCloseLabel"),
        bodyMarkup: `
          <p class="oq-helper-modal-copy">${escapeHtml(t("system.coolingScheduleCopy"))}</p>
          ${state.controlError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>` : ""}
          <div class="oq-helper-modal-body">
            ${renderCoolingScheduleSettingsFields("oq-settings-grid oq-settings-grid--modal")}
          </div>
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-system-modal">${escapeHtml(t("header.done"))}</button>
          </div>
        `,
      });
    }

    if (state.systemModal === "silent-settings") {
      return renderModalShell({
        modalId: "system",
        titleId: "oq-silent-settings-modal-title",
        kicker: t("system.silentKicker"),
        title: t("system.silentTitle"),
        modalClass: "oq-helper-modal--wide oq-helper-modal--scrollable",
        closeAction: "close-system-modal",
        closeLabel: t("system.silentCloseLabel"),
        bodyMarkup: `
          <p class="oq-helper-modal-copy">${escapeHtml(t("system.silentModalCopy"))}</p>
          ${state.controlError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>` : ""}
          <div class="oq-helper-modal-body">
            ${renderSilentSettingsFields()}
          </div>
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-system-modal">${escapeHtml(t("header.done"))}</button>
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

    if (state.systemModal === "electrical-limit-confirm") {
      const pending = state.pendingElectricalLimit || {};
      const fromA = Number(pending.fromA);
      const toA = Number(pending.toA);
      const standardA = Number(pending.standardA);
      const busy = state.busyAction === "save-electricalCurrentLimit";
      const fromLabel = formatDutchAmps(fromA);
      const toLabel = formatDutchAmps(toA);
      const standardLabel = formatDutchAmps(standardA);
      return renderModalShell({
        modalId: "system",
        titleId: "oq-electrical-limit-modal-title",
        kicker: t("system.electricalKicker"),
        title: t("system.electricalTitle"),
        closeAction: "close-system-modal",
        closeLabel: t("system.electricalCloseLabel"),
        bodyMarkup: `
          <p class="oq-helper-modal-copy">${t("system.electricalRaiseCopy", { from: escapeHtml(fromLabel), to: escapeHtml(toLabel) })}</p>
          <p class="oq-settings-action-note oq-settings-action-note--warning">${escapeHtml(t("system.electricalWarnCopy", { to: toLabel, standard: standardLabel }))}</p>
          <p class="oq-helper-modal-copy">${escapeHtml(t("system.electricalNoteCopy"))}</p>
          ${state.controlError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>` : ""}
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${busy ? "disabled" : ""}>${escapeHtml(t("common.cancel"))}</button>
            <button class="oq-helper-button oq-helper-button--warning" type="button" data-oq-action="confirm-electrical-limit" ${busy ? "disabled" : ""}>${busy ? escapeHtml(t("system.electricalBusy")) : escapeHtml(t("system.electricalSetLabel", { to: toLabel }))}</button>
          </div>
        `,
      });
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
        kicker: t("system.pauseKicker"),
        title: t("system.pauseTitle"),
        modalClass: "oq-helper-modal--wide",
        closeAction: "close-system-modal",
        closeLabel: t("system.pauseCloseLabel"),
        bodyMarkup: `
          <p class="oq-helper-modal-copy">${escapeHtml(enabled
              ? t("system.pauseCopyEnabled")
              : t("system.pauseCopyDisabled")
          )}</p>
          ${resumeScheduled
            ? `<div class="oq-helper-modal-success oq-helper-modal-success--compact">
                <strong>${escapeHtml(t("system.pauseResumeAuto"))}</strong>
                <span>${escapeHtml(resumeLabel)}</span>
              </div>`
            : ""
          }
          ${!resumeEntityReady
            ? `<p class="oq-helper-modal-note" aria-live="polite">${escapeHtml(t("system.pauseLoadingOptions"))}</p>`
            : hasResumeEntity
            ? `
              <div class="oq-helper-modal-presets">
                <button class="oq-helper-button" type="button" data-oq-action="apply-openquatt-preset" data-pause-preset="2h" ${busy ? "disabled" : ""}>${escapeHtml(t("system.pausePreset2h"))}</button>
                <button class="oq-helper-button" type="button" data-oq-action="apply-openquatt-preset" data-pause-preset="8h" ${busy ? "disabled" : ""}>${escapeHtml(t("system.pausePreset8h"))}</button>
                <button class="oq-helper-button" type="button" data-oq-action="apply-openquatt-preset" data-pause-preset="tomorrow-morning" ${busy ? "disabled" : ""}>${escapeHtml(t("system.pausePresetMorning"))}</button>
              </div>
              <div class="oq-helper-modal-channel oq-helper-modal-channel--datetime">
                <span class="oq-helper-modal-label">${escapeHtml(t("system.pauseResumeAt"))}</span>
                <div class="oq-helper-modal-inline">
                  <label class="oq-settings-control oq-settings-control--datetime">
                    <input
                      class="oq-helper-input"
                      type="datetime-local"
                      step="60"
                      lang="${escapeHtml(getIntlLocale())}"
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
                  <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="apply-openquatt-custom-pause" ${busy ? "disabled" : ""}>${escapeHtml(t("system.pausePlanMoment"))}</button>
                </div>
              </div>
            `
            : `<p class="oq-helper-modal-note">${escapeHtml(t("system.pauseNoAutoResume"))}</p>`
          }
          <div class="oq-helper-modal-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${busy ? "disabled" : ""}>${escapeHtml(t("common.cancel"))}</button>
            ${!enabled
              ? `<button class="oq-helper-button" type="button" data-oq-action="enable-openquatt-now" ${busy ? "disabled" : ""}>${escapeHtml(t("system.pauseEnableNow"))}</button>`
              : ""
            }
            <button class="oq-helper-button" type="button" data-oq-action="apply-openquatt-indefinite" ${busy ? "disabled" : ""}>${enabled ? escapeHtml(t("system.pauseDisableIndefinite")) : escapeHtml(t("system.pauseWithoutEnd"))}</button>
          </div>
        `,
      });
    }

    return "";
  }
