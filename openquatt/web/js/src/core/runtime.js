import { FAST_POLL_INTERVAL_MS, HIDDEN_POLL_INTERVAL_MS, OFFICIAL_ESPHOME_UI_URL, POLL_JITTER_MAX_MS, POLL_JITTER_MIN_MS, SETTINGS_GROUP_IDS, SETTINGS_GROUPS } from "./config.js";
import { setEntityPollingControls } from "./entity-polling-controls.js";
import { getPrefersReducedMotion, getReducedMotionMedia, getStoredHpLayoutMode, getStoredHpVisualMode, getStoredOverviewTheme, getStoredSurface, getStoredTrendWindowHours, state } from "./state.js";
export { DEFAULT_TREND_WINDOW_HOURS, TREND_WINDOW_HOURS_OPTIONS, state } from "./state.js";
import { handleChange, handleClick, handleFocusChange, handleInput, handleKeyDown, handlePointerDown, handlePointerMove, handlePointerUp, handleSettingsInteractionEnd, handleSettingsInteractionStart, handleWheel } from "./event-handlers.js";
import { getDefaultAppView, getUrlAppView, getUrlControlReplayCustomRange, getUrlControlReplayTab, getUrlControlReplayWindow, getUrlSettingsGroup, setAppView, syncUrlAppView } from "./navigation.js";
import { primeEntities, syncEntities } from "./entity-sync.js";
import { refreshDebugRecordingDeviceStatus } from "../features/debug-recording.js";
import { isFirmwareOtaQuietActive } from "./firmware-quiet.js";
import { clearLegacyMotionVariables, startMotionLoop, stopMotionLoop } from "./motion.js";
import { render } from "./render-scheduler.js";

  export function setOverviewTheme(theme) {
    state.overviewTheme = theme === "dark" ? "dark" : "light";
    try {
      window.localStorage.setItem("oq-overview-theme", state.overviewTheme);
    } catch (_error) {
      // Ignore storage failures in embedded browsers.
    }
  }

  export function setInterfacePanelOpen(open) {
    state.interfacePanelOpen = open === true;
  }

  export function setStoredSurface(surface) {
    try {
      window.localStorage.setItem("oq-active-surface", surface === "native" ? "native" : "app");
    } catch (_error) {
      // Ignore storage failures in embedded browsers.
    }
  }

  export function setSettingsGroup(groupId, options = {}) {
    state.settingsGroup = SETTINGS_GROUP_IDS.has(groupId) ? groupId : SETTINGS_GROUPS[0].id;
    if (state.settingsGroup !== "system" && state.usageTelemetryPreviewSurface === "settings-system") {
      state.usageTelemetryPreviewSurface = "";
    }
    try {
      window.localStorage.setItem("oq-settings-group", state.settingsGroup);
    } catch (_error) {
      // Ignore storage failures in embedded browsers.
    }
    if (options.syncUrl !== false && state.appView === "settings") {
      syncUrlAppView(options.syncMode || "replace");
    }
  }

  export function setDevPanelOpen(open) {
    if (!__OQ_PREVIEW__) {
      return;
    }
    state.devPanelOpen = open === true;
    try {
      window.localStorage.setItem("oq-dev-panel-open", state.devPanelOpen ? "true" : "false");
    } catch (_error) {
      // Ignore storage failures in embedded browsers.
    }
  }

  export function setHpVisualMode(mode) {
    state.hpVisualMode = mode === "compact" ? "compact" : "schematic";
    try {
      window.localStorage.setItem("oq-hp-visual-mode", state.hpVisualMode);
    } catch (_error) {
      // Ignore storage failures in embedded browsers.
    }
  }

  export function setHpLayoutMode(mode) {
    state.hpLayoutMode = mode === "focus-hp1" || mode === "focus-hp2" ? mode : "equal";
    try {
      window.localStorage.setItem("oq-hp-layout-mode", state.hpLayoutMode);
    } catch (_error) {
      // Ignore storage failures in embedded browsers.
    }
  }

  export function handleReducedMotionPreferenceChange(event) {
    state.reducedMotion = Boolean(event?.matches);
    if (state.reducedMotion) {
      stopMotionLoop();
      return;
    }
    startMotionLoop();
  }

  export function bindReducedMotionPreference() {
    const media = getReducedMotionMedia();
    if (!media || state.motionPreferenceMedia === media) {
      return;
    }

    state.motionPreferenceMedia = media;
    state.motionPreferenceListener = handleReducedMotionPreferenceChange;
    if (typeof media.addEventListener === "function") {
      media.addEventListener("change", state.motionPreferenceListener);
    } else if (typeof media.addListener === "function") {
      media.addListener(state.motionPreferenceListener);
    }
    state.reducedMotion = Boolean(media.matches);
  }

  export function hasLoadedEntities() {
    return Object.keys(state.entities).length > 0;
  }

  export function getEntityPollJitterMs() {
    return POLL_JITTER_MIN_MS + Math.floor(Math.random() * (POLL_JITTER_MAX_MS - POLL_JITTER_MIN_MS + 1));
  }

  export function getEntityPollDelayMs() {
    const base = document.hidden ? HIDDEN_POLL_INTERVAL_MS : FAST_POLL_INTERVAL_MS;
    return base + getEntityPollJitterMs();
  }

  export function scheduleEntityPolling(delayMs = getEntityPollDelayMs()) {
    if (state.pollTimer || state.nativeOpen || state.updateInstallBusy) {
      return;
    }
    if (isFirmwareOtaQuietActive()) {
      return;
    }
    state.pollTimer = window.setTimeout(async () => {
      state.pollTimer = null;
      await syncEntities();
      scheduleEntityPolling();
    }, delayMs);
  }

  export function startEntityPolling() {
    scheduleEntityPolling();
  }

  export function stopEntityPolling() {
    if (!state.pollTimer) {
      return;
    }
    window.clearTimeout(state.pollTimer);
    state.pollTimer = null;
  }

  setEntityPollingControls({ start: startEntityPolling, stop: stopEntityPolling });

  export function handleVisibilityChange() {
    if (state.nativeOpen) {
      return;
    }
    stopEntityPolling();
    startEntityPolling();
    if (!document.hidden) {
      void syncEntities({ forceProbe: true });
    }
  }

  export function syncSurfaceRuntime(options = {}) {
    syncNativeVisibility();
    if (state.nativeOpen) {
      stopEntityPolling();
      stopMotionLoop();
      if (!state.nativeFrontendLoaded) {
        void ensureNativeFrontendLoaded();
      }
      return;
    }

    startMotionLoop();
    startEntityPolling();

    if (options.refresh === false) {
      return;
    }
    if (!hasLoadedEntities()) {
      void primeEntities();
      return;
    }
    void syncEntities({ forceFast: true });
  }

  export function handlePopState() {
    const nextView = getUrlAppView() || getDefaultAppView();
    const nextSettingsGroup = nextView === "settings" ? (getUrlSettingsGroup() || state.settingsGroup) : "";
    const nextControlReplayTab = nextView === "control" ? (getUrlControlReplayTab() || "status") : state.controlReplayTab;
    const nextControlReplayWindow = nextView === "control" ? (getUrlControlReplayWindow() || "last24") : state.controlReplayWindow;
    const nextControlReplayCustomRange = nextView === "control" ? getUrlControlReplayCustomRange() : null;
    if (nextView === state.appView &&
        (nextView !== "settings" || nextSettingsGroup === state.settingsGroup) &&
        (nextView !== "control" || (
          nextControlReplayTab === state.controlReplayTab &&
          nextControlReplayWindow === state.controlReplayWindow &&
          (!nextControlReplayCustomRange || (
            nextControlReplayCustomRange.start === state.controlReplayCustomStart &&
            nextControlReplayCustomRange.end === state.controlReplayCustomEnd
          ))
        ))) {
      return;
    }

    state.appView = nextView;
    if (nextView === "control") {
      state.controlReplayTab = nextControlReplayTab;
      state.controlReplayWindow = nextControlReplayWindow;
      state.controlReplayCustomStart = nextControlReplayCustomRange?.start || "";
      state.controlReplayCustomEnd = nextControlReplayCustomRange?.end || "";
      state.controlReplayPeriodMenuOpen = false;
      state.controlReplayCustomPeriodOpen = false;
      state.controlReplayCustomPeriodError = "";
    }
    if (nextView === "settings" && nextSettingsGroup) {
      state.settingsGroup = nextSettingsGroup;
      try {
        window.localStorage.setItem("oq-settings-group", state.settingsGroup);
      } catch (_error) {
        // Ignore storage failures in embedded browsers.
      }
    }
    render();
    void syncEntities({ forceFast: true });
  }

  export function syncNativeVisibility() {
    if (!state.nativeApp) {
      return;
    }

    state.nativeApp.classList.add("oq-native-app");
    state.nativeApp.classList.toggle("oq-native-app--collapsed", !state.nativeOpen);
    state.nativeApp.setAttribute("aria-hidden", state.nativeOpen ? "false" : "true");
  }

  export function boot() {
    if (document.readyState === "loading") {
      document.addEventListener("DOMContentLoaded", mountWhenReady, { once: true });
    } else {
      mountWhenReady();
    }
    window.addEventListener("pointermove", handlePointerMove);
    window.addEventListener("pointerup", handlePointerUp);
    window.addEventListener("popstate", handlePopState);
    if (__OQ_PREVIEW__) {
      window.addEventListener("oq-mock-updated", handleMockUpdated);
      window.addEventListener("oq-dev-controls-changed", handleDevControlsChanged);
    }
    document.addEventListener("visibilitychange", handleVisibilityChange);
  }

  export function handleMockUpdated() {
    if (!__OQ_PREVIEW__ || !state.mounted) {
      return;
    }
    void syncEntities({ forceDecisionLog: true, forceIncidentMonitoring: true });
  }

  export function handleDevControlsChanged() {
    if (!__OQ_PREVIEW__ || !state.mounted) {
      return;
    }
    render();
  }

  export function mountWhenReady() {
    ensureViewportMeta();
    let app = document.querySelector("esp-app");
    if (!app) {
      app = document.createElement("esp-app");
      document.body.appendChild(app);
    }

    state.nativeApp = app;
    state.nativeFrontendLoaded = Array.from(document.scripts).some((script) => script.src === OFFICIAL_ESPHOME_UI_URL);

    if (!state.mounted) {
      mountPanel(app);
      state.mounted = true;
      syncSurfaceRuntime();
    }

    syncNativeVisibility();
    if (!state.nativeOpen) {
      void syncEntities();
      void refreshDebugRecordingDeviceStatus({ silent: true });
    }
  }

  export function ensureViewportMeta() {
    if (!document.head) {
      return;
    }

    let viewport = document.head.querySelector('meta[name="viewport"]');
    if (!viewport) {
      viewport = document.createElement("meta");
      viewport.name = "viewport";
      document.head.appendChild(viewport);
    }
    viewport.setAttribute("content", "width=device-width, initial-scale=1");
  }

  export function mountPanel(app) {
    const root = document.createElement("section");
    root.id = "oq-helper-root";
    root.lang = "nl-NL";
    if (document.documentElement && !document.documentElement.lang) {
      document.documentElement.lang = "nl-NL";
    }
    app.parentNode.insertBefore(root, app);
    root.addEventListener("click", handleClick);
    root.addEventListener("change", handleChange);
    root.addEventListener("input", handleInput);
    root.addEventListener("keydown", handleKeyDown);
    root.addEventListener("wheel", handleWheel, { passive: false });
    root.addEventListener("focusin", handleFocusChange);
    root.addEventListener("focusout", handleFocusChange);
    root.addEventListener("mouseover", handleSettingsInteractionStart);
    root.addEventListener("mouseout", handleSettingsInteractionEnd);
    root.addEventListener("pointerdown", handlePointerDown);
    state.root = root;
    bindReducedMotionPreference();
    const initialUrlView = getUrlAppView() || getDefaultAppView();
    const initialUrlSettingsGroup = initialUrlView === "settings" ? getUrlSettingsGroup() : "";
    const initialUrlControlReplayTab = initialUrlView === "control" ? getUrlControlReplayTab() : "";
    const initialUrlControlReplayWindow = initialUrlView === "control" ? getUrlControlReplayWindow() : "";
    const initialUrlControlReplayCustomRange = initialUrlView === "control" ? getUrlControlReplayCustomRange() : null;
    if (initialUrlSettingsGroup) {
      setSettingsGroup(initialUrlSettingsGroup, { syncUrl: false });
    }
    if (initialUrlControlReplayTab) {
      state.controlReplayTab = initialUrlControlReplayTab;
    }
    if (initialUrlControlReplayWindow) {
      state.controlReplayWindow = initialUrlControlReplayWindow;
      state.controlReplayCustomStart = initialUrlControlReplayCustomRange?.start || "";
      state.controlReplayCustomEnd = initialUrlControlReplayCustomRange?.end || "";
    }
    setAppView(initialUrlView, { syncMode: "replace", forceSync: true });
    clearLegacyMotionVariables();
    render();
  }

  export function loadScriptOnce(src) {
    return new Promise((resolve, reject) => {
      if (!src) {
        resolve();
        return;
      }

      const existing = Array.from(document.scripts).find((script) => script.src === src);
      if (existing) {
        if (existing.dataset.loaded === "true") {
          resolve();
          return;
        }

        existing.addEventListener("load", () => resolve(), { once: true });
        existing.addEventListener("error", (event) => reject(event), { once: true });
        return;
      }

      const script = document.createElement("script");
      script.src = src;
      script.async = false;
      script.addEventListener("load", () => {
        script.dataset.loaded = "true";
        resolve();
      }, { once: true });
      script.addEventListener("error", (event) => reject(event), { once: true });
      document.head.appendChild(script);
    });
  }

  export async function ensureNativeFrontendLoaded() {
    if (state.nativeFrontendLoaded || state.nativeFrontendLoading) {
      return;
    }

    state.nativeFrontendLoading = true;
    if (state.nativeOpen) {
      render();
    }
    try {
      await loadScriptOnce(OFFICIAL_ESPHOME_UI_URL);
      state.nativeFrontendLoaded = true;
    } catch (error) {
      state.controlError = `ESPHome fallback kon niet worden geladen. ${error.message || error}`;
      state.nativeOpen = false;
      setStoredSurface("app");
      render();
      syncSurfaceRuntime();
    } finally {
      state.nativeFrontendLoading = false;
      if (state.nativeOpen) {
        render();
      }
    }
  }

  export function bindHeaderDevControls() {
    if (!__OQ_PREVIEW__ || !state.root) {
      return;
    }
    const controls = typeof window !== "undefined" ? window.__OQ_DEV_CONTROLS__ : null;
    if (!controls || typeof controls.bind !== "function") {
      return;
    }
    controls.bind(state.root);
  }
