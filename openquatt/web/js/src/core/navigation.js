import { isTrendHistoryEnabled } from "./app-shared.js";
import { APP_VIEW_IDS, SETTINGS_GROUP_IDS, SETTINGS_GROUPS } from "./config.js";
import { state } from "./state.js";

export function getDefaultAppView() {
  return "overview";
}

export function normalizeAppView(view) {
  if (view === "trends") {
    view = "diagnosis";
  }
  if (!APP_VIEW_IDS.has(view)) {
    return "";
  }
  if (view === "diagnosis" && !isTrendHistoryEnabled()) {
    return "";
  }
  return view;
}

export function normalizeUrlToken(value) {
  return String(value || "").trim().toLowerCase();
}

export function normalizeControlReplayTab(value) {
  const token = normalizeUrlToken(value);
  const aliases = {
    actueel: "status",
    current: "status",
    status: "status",
    situatie: "status",
    tijdlijn: "timeline",
    timeline: "timeline",
    log: "timeline",
    grafiek: "graphs",
    grafieken: "graphs",
    graphs: "graphs",
    graph: "graphs",
  };
  return aliases[token] || "";
}

export function getControlReplayTabUrlToken(tab = state.controlReplayTab) {
  const tokens = {
    status: "current",
    timeline: "timeline",
    graphs: "graphs",
  };
  return tokens[tab] || tokens.status;
}

export function normalizeControlReplayWindow(value) {
  const token = normalizeUrlToken(value);
  const aliases = {
    "1h": "last1",
    last1: "last1",
    "2h": "last2",
    last2: "last2",
    "4h": "last4",
    last4: "last4",
    "8h": "last8",
    last8: "last8",
    "12h": "last12",
    last12: "last12",
    "24h": "last24",
    last24: "last24",
    "48h": "last48",
    last48: "last48",
    "3d": "last3d",
    last3d: "last3d",
    "7d": "week",
    week: "week",
    today: "today",
    yesterday: "yesterday",
    custom: "custom",
  };
  return aliases[token] || "";
}

export function getControlReplayWindowUrlToken(windowId = state.controlReplayWindow) {
  const tokens = {
    last1: "1h",
    last2: "2h",
    last4: "4h",
    last8: "8h",
    last12: "12h",
    last24: "24h",
    last48: "48h",
    last3d: "3d",
    today: "today",
    yesterday: "yesterday",
    week: "7d",
    custom: "custom",
  };
  return tokens[windowId] || tokens.last24;
}

export function getUrlAppView() {
  try {
    const url = new URL(window.location.href);
    const rawQueryView = normalizeUrlToken(url.searchParams.get("view") || "");
    const queryView = normalizeAppView(rawQueryView);
    if (queryView) {
      return queryView;
    }

    const rawHashView = normalizeUrlToken(url.hash.replace(/^#/, ""));
    const hashView = normalizeAppView(rawHashView);
    return hashView || "";
  } catch (_error) {
    return "";
  }
}

export function getUrlControlReplayTab() {
  try {
    const url = new URL(window.location.href);
    return normalizeControlReplayTab(
      url.searchParams.get("controlTab") ||
      url.searchParams.get("controlView") ||
      "",
    );
  } catch (_error) {
    return "";
  }
}

export function getUrlControlReplayWindow() {
  try {
    const url = new URL(window.location.href);
    return normalizeControlReplayWindow(url.searchParams.get("controlPeriod") || "");
  } catch (_error) {
    return "";
  }
}

export function getUrlControlReplayCustomRange() {
  try {
    const url = new URL(window.location.href);
    return {
      start: String(url.searchParams.get("controlStart") || ""),
      end: String(url.searchParams.get("controlEnd") || ""),
    };
  } catch (_error) {
    return { start: "", end: "" };
  }
}

export function getUrlSettingsGroup() {
  try {
    const url = new URL(window.location.href);
    const section = normalizeUrlToken(url.searchParams.get("section") || "");
    if (SETTINGS_GROUP_IDS.has(section)) {
      return section;
    }

    const legacyGroup = normalizeUrlToken(url.searchParams.get("group") || "");
    if (SETTINGS_GROUP_IDS.has(legacyGroup)) {
      return legacyGroup;
    }

    return "";
  } catch (_error) {
    return "";
  }
}

export function syncUrlAppView(mode = "replace") {
  try {
    const url = new URL(window.location.href);
    const normalized = normalizeAppView(state.appView) || getDefaultAppView();
    url.searchParams.set("view", normalized);
    if (normalized === "settings") {
      const group = SETTINGS_GROUP_IDS.has(state.settingsGroup) ? state.settingsGroup : SETTINGS_GROUPS[0].id;
      url.searchParams.set("section", group);
      url.searchParams.delete("group");
    } else {
      url.searchParams.delete("section");
      url.searchParams.delete("group");
    }
    if (normalized === "control") {
      url.searchParams.set("controlTab", getControlReplayTabUrlToken());
      url.searchParams.set("controlPeriod", getControlReplayWindowUrlToken());
      if (state.controlReplayWindow === "custom" && state.controlReplayCustomStart && state.controlReplayCustomEnd) {
        url.searchParams.set("controlStart", state.controlReplayCustomStart);
        url.searchParams.set("controlEnd", state.controlReplayCustomEnd);
      } else {
        url.searchParams.delete("controlStart");
        url.searchParams.delete("controlEnd");
      }
      url.searchParams.delete("controlView");
    } else {
      url.searchParams.delete("controlTab");
      url.searchParams.delete("controlPeriod");
      url.searchParams.delete("controlStart");
      url.searchParams.delete("controlEnd");
      url.searchParams.delete("controlView");
    }
    if (url.hash && normalizeAppView(url.hash.replace(/^#/, ""))) {
      url.hash = "";
    }

    const method = mode === "push" ? "pushState" : "replaceState";
    window.history[method]({
      oqView: normalized,
      oqSettingsSection: normalized === "settings" ? state.settingsGroup : "",
      oqControlTab: normalized === "control" ? state.controlReplayTab : "",
      oqControlPeriod: normalized === "control" ? state.controlReplayWindow : "",
    }, "", url.toString());
  } catch (_error) {
    // Ignore history failures in embedded browsers.
  }
}

export function setAppView(view, options = {}) {
  const normalized = normalizeAppView(view) || getDefaultAppView();
  const mode = options.syncMode || "replace";
  const changed = state.appView !== normalized;
  state.appView = normalized;
  if (normalized !== "settings" && state.usageTelemetryPreviewSurface === "settings-system") {
    state.usageTelemetryPreviewSurface = "";
  }

  if (changed || options.forceSync) {
    syncUrlAppView(mode);
  }
}
