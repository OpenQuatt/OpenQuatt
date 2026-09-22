import { renderAppNav, syncDocumentTheme, syncDocumentTitle } from "../core/app-shared.js";
import { LOGO_MARKUP } from "../core/embedded-assets.js";
import { getSettingsRenderSignature } from "../core/render-signatures.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell, syncModalFocus } from "../core/modal-shell.js";
import { captureModalContinuity, restoreModalContinuity } from "../core/modal-continuity.js";
import { captureSettingsFocusContinuity, restoreSettingsFocusContinuity } from "../core/settings-focus-continuity.js";
import { setRenderCallback } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { deferTimeInputRender } from "../core/time-input.js";
import { clearLegacyMotionVariables, startMotionLoop, stopMotionLoop } from "../core/motion.js";
import { bindHeaderDevControls, syncNativeVisibility } from "../core/runtime.js";
import { renderDeviceReconnectModal, renderUpdateModal } from "../features/firmware-update.js";
import { getDeviceVersionLabel, getHeaderRenderSignature, renderControlModeOverrideBanner, renderDevPanel, renderHeaderStatus, renderNativeSurfaceShell, renderSystemModal } from "../features/header-status.js";
import { getMqttSensorsModalRenderSignature } from "../features/mqtt-actions.js";
import { updateMqttState } from "../core/feature-state.js";
import { captureQuickStartScrollState, queueQuickStartScrollRestore, renderQuickStartModal } from "../features/quickstart.js";
import { captureCm100CommissioningScrollState, captureHistoryStorageModalScrollState, captureServiceTaskModalScrollState, captureSettingsBackupRestoreModalScrollState, captureWebServerLogScrollState, queueCm100CommissioningScrollRestore, queueHistoryStorageModalScrollRestore, queueServiceTaskModalScrollRestore, queueSettingsBackupRestoreModalScrollRestore, queueWebServerLogScrollRestore, syncWebServerLogStream } from "../features/webserver-logs.js";
import { renderSettingsGroupContent, renderSettingsGroupNav } from "../settings/core.js";
import { renderEnergyView, renderResultsView } from "./energy.js";
import { renderControlReplayView } from "../features/control-replay-view.js";
import { renderOverviewView, syncTechTooltipLayers } from "./heatpump.js";
import { renderDiagnosisView, syncOverviewTrendInteractions } from "./overview.js";
import { t } from "../i18n/index.js";

const captureFocusedSettingsField = () => captureSettingsFocusContinuity(state.root, state.appView);
const restoreFocusedSettingsField = (focusState) => restoreSettingsFocusContinuity(state.root, focusState);

export function renderSettingsView() {
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(t("shell.settingsLabel"))}</p>
        <h2 class="oq-helper-section-title">${escapeHtml(t("shell.settingsTitle"))}</h2>
        <p class="oq-helper-section-copy">${escapeHtml(t("shell.settingsCopy"))}</p>
        ${state.controlError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.controlError)}</p>` : ""}
        ${state.controlNotice ? `<p class="oq-helper-notice" role="status">${escapeHtml(state.controlNotice)}</p>` : ""}
        ${renderSettingsGroupNav()}
        ${renderSettingsGroupContent()}
      </section>
    `;
  }

  export function renderInitialLoadingView() {
    return renderModalShell({
      modalId: "initial-load",
      titleId: "oq-loading-modal-title",
      kicker: t("navigation.appName"),
      title: t("shell.loadingTitle"),
      backdropClass: "oq-helper-modal-backdrop--loading",
      modalClass: "oq-helper-modal--reconnect oq-helper-modal--loading",
      role: "status",
      ariaLive: "polite",
      bodyMarkup: `
        <p class="oq-helper-modal-copy">${escapeHtml(t("shell.loadingCopy"))}</p>
        <div class="oq-helper-reconnect-status oq-helper-loading-status">
          <span class="oq-helper-reconnect-spinner" aria-hidden="true"></span>
          <div>
            <strong>${escapeHtml(t("shell.firstSync"))}</strong>
            <span>${escapeHtml(t("shell.firstSyncCopy"))}</span>
          </div>
        </div>
      `,
    });
  }

  export function renderCurrentAppView() {
    return state.appView === "overview"
      ? renderOverviewView()
      : state.appView === "control"
      ? renderControlReplayView()
      : state.appView === "energy"
      ? renderEnergyView()
      : state.appView === "diagnosis"
      ? renderDiagnosisView()
      : state.appView === "results"
      ? renderResultsView()
      : renderSettingsView();
  }

  export function renderPoweredByFooter() {
    const version = getDeviceVersionLabel();
    const versionMarkup = version && version !== "—"
      ? `<span class="oq-helper-footer-version">OpenQuatt ${escapeHtml(version)}</span>`
      : "";
    return `
      <footer class="oq-helper-powered-by" aria-label="${escapeHtml(t("shell.platformAria"))}">
        ${versionMarkup}
        <nav class="oq-helper-footer-links" aria-label="${escapeHtml(t("shell.linksAria"))}">
          <a href="https://openquatt.github.io/OpenQuatt/" target="_blank" rel="noreferrer">Docs</a>
          <a href="https://github.com/OpenQuatt/OpenQuatt" target="_blank" rel="noreferrer">GitHub</a>
        </nav>
        <a class="oq-helper-powered-by-link" href="https://esphome.io/" target="_blank" rel="noreferrer" aria-label="${escapeHtml(t("shell.builtWithAria"))}">
          <span>${escapeHtml(t("shell.builtWith"))}</span>
          <img class="oq-helper-powered-by-logo" src="https://media.esphome.io/logo/logo-text-on-light.svg" alt="ESPHome" loading="lazy" decoding="async">
        </a>
      </footer>
    `;
  }

  export function getActiveDevControlSelect() {
    const active = typeof document !== "undefined" ? document.activeElement : null;
    if (!active || typeof active.matches !== "function") {
      return null;
    }
    return active.matches('select[data-oq-dev-control]') ? active : null;
  }

  export function deferRenderUntilDevControlSelectSettles(select) {
    if (!select || state.deferDevControlSelectRender) {
      return;
    }

    state.deferDevControlSelectRender = true;
    const flush = () => {
      select.removeEventListener("blur", flush);
      select.removeEventListener("change", flush);
      state.deferDevControlSelectRender = false;
      window.setTimeout(() => render(), 0);
    };
    select.addEventListener("blur", flush, { once: true });
    select.addEventListener("change", flush, { once: true });
  }

  export function captureSettingsPageScrollState() {
    if (
      state.nativeOpen ||
      state.appView !== "settings" ||
      state.renderedAppView !== "settings" ||
      state.renderedSettingsGroup !== state.settingsGroup
    ) {
      return null;
    }

    const scroller = document.scrollingElement || document.documentElement;
    const top = Number(window.scrollY || scroller?.scrollTop || 0);
    if (!Number.isFinite(top) || top <= 0) {
      return null;
    }

    return {
      group: state.settingsGroup,
      left: Number(window.scrollX || scroller?.scrollLeft || 0),
      top,
    };
  }

  export function queueSettingsPageScrollRestore(scrollState) {
    if (!scrollState) {
      return;
    }

    const token = (state.settingsPageScrollRestoreToken || 0) + 1;
    state.settingsPageScrollRestoreToken = token;
    const restore = () => {
      if (
        token !== state.settingsPageScrollRestoreToken ||
        state.nativeOpen ||
        state.appView !== "settings" ||
        state.settingsGroup !== scrollState.group
      ) {
        return;
      }

      const scroller = document.scrollingElement || document.documentElement;
      if (!scroller) {
        return;
      }

      const maxTop = Math.max(0, scroller.scrollHeight - scroller.clientHeight);
      const top = Math.min(scrollState.top, maxTop);
      window.scrollTo({ left: scrollState.left, top, behavior: "auto" });
    };

    window.requestAnimationFrame(() => {
      restore();
      window.requestAnimationFrame(restore);
      window.setTimeout(restore, 80);
    });
  }

  export function render() {
    if (!state.root) {
      return;
    }
    if (deferTimeInputRender()) {
      return;
    }

    const activeDevControlSelect = getActiveDevControlSelect();
    if (activeDevControlSelect) {
      deferRenderUntilDevControlSelectSettles(activeDevControlSelect);
      return;
    }

    const focusedSettingsField = captureFocusedSettingsField();
    const modalContinuity = captureModalContinuity(state.root);
    const oduEepromLauncher = state.root.querySelector('[data-oq-action="open-odu-eeprom-dump-modal"]');
    const oduEepromLauncherFocused = oduEepromLauncher === document.activeElement;

    const webServerLogScrollState = state.systemModal === "webserver-logs"
      ? captureWebServerLogScrollState()
      : null;
    const cm100CommissioningScrollState = state.systemModal === "cm100-commissioning"
      ? captureCm100CommissioningScrollState()
      : null;
    const serviceTaskModalScrollState = String(state.systemModal || "").startsWith("service-task-")
      ? captureServiceTaskModalScrollState()
      : null;
    const historyStorageModalScrollState = state.systemModal === "history-storage"
      ? captureHistoryStorageModalScrollState()
      : null;
    const settingsBackupRestoreModalScrollState = state.systemModal === "settings-backup-restore"
      ? captureSettingsBackupRestoreModalScrollState()
      : null;
    const quickStartScrollState = state.quickStartModalOpen
      ? captureQuickStartScrollState()
      : null;
    const settingsPageScrollState = captureSettingsPageScrollState();

    if (state.nativeOpen) {
      state.root.innerHTML = `
        ${renderDevPanel()}
        ${renderNativeSurfaceShell()}
      `;
      syncModalFocus(state.root);
      restoreModalContinuity(state.root, modalContinuity);
      state.renderedAppView = "native";
      state.renderedSettingsGroup = "";
      state.settingsRenderSignature = "";
      state.headerRenderSignature = getHeaderRenderSignature();
      updateMqttState({ mqttSensorsModalRenderSignature: "" });
      stopMotionLoop();
      syncNativeVisibility();
      syncWebServerLogStream();
      bindHeaderDevControls();
      syncDocumentTheme();
      syncDocumentTitle();
      queueWebServerLogScrollRestore(webServerLogScrollState);
      queueCm100CommissioningScrollRestore(cm100CommissioningScrollState);
      queueServiceTaskModalScrollRestore(serviceTaskModalScrollState);
      queueHistoryStorageModalScrollRestore(historyStorageModalScrollState);
      queueSettingsBackupRestoreModalScrollRestore(settingsBackupRestoreModalScrollState);
      queueQuickStartScrollRestore(quickStartScrollState);
      return;
    }

    const currentViewContent = renderCurrentAppView();
    const mainContent = state.loadingEntities
      ? `${currentViewContent}${renderInitialLoadingView()}`
      : currentViewContent;
    const wideFlushCard = state.appView === "overview" || state.appView === "control" || state.appView === "energy" ||
      state.appView === "diagnosis" || state.appView === "results";

    state.root.innerHTML = `
      ${renderDevPanel()}
      <div class="oq-helper-shell${state.overviewTheme === "dark" ? " oq-helper-shell--dark" : ""}">
        <div class="oq-helper-card${wideFlushCard ? " oq-helper-card--wide-flush" : ""}">
          <div class="oq-helper-head">
            <div class="oq-helper-brand">
              <div class="oq-helper-logo-lockup">
                <div data-oq-action="matrix-version-tap" style="display:contents">${LOGO_MARKUP}</div>
              </div>
              <p class="oq-helper-lead">${escapeHtml(t("shell.lead"))}</p>
            </div>
            ${renderHeaderStatus()}
          </div>
      ${renderAppNav()}
      ${renderControlModeOverrideBanner()}
      ${mainContent}
      ${renderPoweredByFooter()}
        </div>
      </div>
      ${renderQuickStartModal()}
      ${renderUpdateModal()}
      ${renderSystemModal()}
      ${renderDeviceReconnectModal()}
    `;
    const replacementOduEepromLauncher = state.root.querySelector('[data-oq-action="open-odu-eeprom-dump-modal"]');
    if (oduEepromLauncher && replacementOduEepromLauncher) {
      replacementOduEepromLauncher.replaceWith(oduEepromLauncher);
      if (oduEepromLauncherFocused) oduEepromLauncher.focus({ preventScroll: true });
    }
    syncModalFocus(state.root);
    restoreModalContinuity(state.root, modalContinuity);
    restoreFocusedSettingsField(focusedSettingsField);
    state.renderedAppView = state.appView;
    state.renderedSettingsGroup = state.appView === "settings" ? state.settingsGroup : "";
    state.settingsRenderSignature = state.appView === "settings" ? getSettingsRenderSignature() : "";
    state.headerRenderSignature = getHeaderRenderSignature();
    updateMqttState({
      mqttSensorsModalRenderSignature: state.systemModal === "mqtt-sensors" ? getMqttSensorsModalRenderSignature() : "",
    });
    clearLegacyMotionVariables();
    syncTechTooltipLayers();
    syncWebServerLogStream();
    startMotionLoop();
    syncOverviewTrendInteractions();
    syncNativeVisibility();
    bindHeaderDevControls();
    syncDocumentTheme();
    syncDocumentTitle();
    queueWebServerLogScrollRestore(webServerLogScrollState);
    queueCm100CommissioningScrollRestore(cm100CommissioningScrollState);
    queueServiceTaskModalScrollRestore(serviceTaskModalScrollState);
    queueHistoryStorageModalScrollRestore(historyStorageModalScrollState);
    queueSettingsBackupRestoreModalScrollRestore(settingsBackupRestoreModalScrollState);
    queueQuickStartScrollRestore(quickStartScrollState);
    queueSettingsPageScrollRestore(settingsPageScrollState);
  }

setRenderCallback(render);
