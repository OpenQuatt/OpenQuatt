import { invokeActionMap } from "../core/action-router.js";
import { normalizeControlReplayTab, normalizeControlReplayWindow, syncUrlAppView } from "../core/navigation.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";

const CONTROL_REPLAY_CUSTOM_MAX_RANGE_MS = 7 * 24 * 60 * 60 * 1000;

function setControlReplayGraphMinute(windowId) {
  state.controlReplayGraphMinute = windowId === "week"
    ? 1230
    : windowId === "today" || windowId === "yesterday"
    ? 720
    : 1000;
}

function parseControlReplayCustomDateTime(value) {
  const epochMs = new Date(String(value || "")).getTime();
  return Number.isFinite(epochMs) ? epochMs : Number.NaN;
}

const controlReplayActionHandlers = {
  "select-control-replay-tab": ({ button }) => {
    const tab = button.dataset.replayTab || "status";
    state.controlReplayTab = normalizeControlReplayTab(tab) || "status";
    if (state.appView === "control") {
      syncUrlAppView("push");
    }
    render();
  },
  "select-control-replay-window": ({ button }) => {
    const windowId = normalizeControlReplayWindow(button.dataset.replayWindow || "") || "last24";
    if (windowId !== "custom") {
      state.controlReplayWindow = windowId;
      state.controlReplayPeriodMenuOpen = false;
      state.controlReplayCustomPeriodOpen = false;
      state.controlReplayCustomPeriodError = "";
      setControlReplayGraphMinute(windowId);
      if (state.appView === "control") {
        syncUrlAppView("push");
      }
    }
    render();
  },
  "toggle-control-replay-period-menu": () => {
    state.controlReplayPeriodMenuOpen = !state.controlReplayPeriodMenuOpen;
    state.controlReplayCustomPeriodOpen = state.controlReplayPeriodMenuOpen && state.controlReplayWindow === "custom";
    state.controlReplayCustomPeriodError = "";
    render();
  },
  "toggle-control-replay-custom-period": () => {
    state.controlReplayCustomPeriodOpen = !state.controlReplayCustomPeriodOpen;
    state.controlReplayCustomPeriodError = "";
    render();
  },
  "apply-control-replay-custom-period": ({ button }) => {
    const menu = button.closest("[data-oq-control-replay-period-menu]");
    const startDate = String(menu?.querySelector('[data-oq-control-replay-custom-start-date]')?.value || "");
    const startHour = String(menu?.querySelector('[data-oq-control-replay-custom-start-hour]')?.value || "");
    const endDate = String(menu?.querySelector('[data-oq-control-replay-custom-end-date]')?.value || "");
    const endHour = String(menu?.querySelector('[data-oq-control-replay-custom-end-hour]')?.value || "");
    const start = `${startDate}T${startHour}:00`;
    const end = `${endDate}T${endHour}:00`;
    const startEpochMs = parseControlReplayCustomDateTime(start);
    const endEpochMs = parseControlReplayCustomDateTime(end);
    if (!Number.isFinite(startEpochMs) || !Number.isFinite(endEpochMs) || endEpochMs <= startEpochMs) {
      state.controlReplayCustomPeriodError = t("controlReplay.customEndAfterStart");
      render();
      return;
    }
    if (endEpochMs - startEpochMs > CONTROL_REPLAY_CUSTOM_MAX_RANGE_MS) {
      state.controlReplayCustomPeriodError = t("controlReplay.customMaxRange");
      render();
      return;
    }
    const nowMs = Date.now();
    if (startEpochMs < nowMs - CONTROL_REPLAY_CUSTOM_MAX_RANGE_MS || endEpochMs > nowMs + (60 * 1000)) {
      state.controlReplayCustomPeriodError = t("controlReplay.customInRange");
      render();
      return;
    }
    state.controlReplayCustomStart = start;
    state.controlReplayCustomEnd = end;
    state.controlReplayCustomPeriodError = "";
    state.controlReplayWindow = "custom";
    state.controlReplayPeriodMenuOpen = false;
    state.controlReplayCustomPeriodOpen = false;
    setControlReplayGraphMinute("custom");
    if (state.appView === "control") {
      syncUrlAppView("push");
    }
    render();
  },
  "select-control-replay-episode": ({ button }) => {
    state.controlReplaySelectedEpisode = button.dataset.replayEpisode || "";
    render();
  },
  "toggle-control-replay-support-details": ({ button, event }) => {
    event.preventDefault();
    const details = button.closest(".oq-working-support");
    const itemId = details?.dataset.replaySupportItem || "";
    state.controlReplaySupportDetailsItemId = details && details.hasAttribute("open") ? "" : itemId;
    render();
  },
};

export function handleControlReplayAction(action, button, event) {
  return invokeActionMap(controlReplayActionHandlers, action, { button, event });
}
