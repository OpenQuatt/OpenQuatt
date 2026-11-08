import { suspendWeatherForecast } from "./weather-forecast.js";
import { render } from "../core/render-scheduler.js";
import { ensureNativeFrontendLoaded, setDevPanelOpen, setInterfacePanelOpen, setStoredSurface, syncSurfaceRuntime } from "../core/runtime.js";
import { state } from "../core/state.js";
import { invokeActionMap } from "../core/action-router.js";
import { registerMatrixVersionTap } from "./matrix-easter-egg.js";
import { resetFirmwareManualUploadSelection, resetFirmwareTestSelection } from "./firmware-update.js";
import { updateFirmwareState } from "../core/feature-state.js";
import { stopLoginAuthStatusPolling } from "./security-actions.js";

function selectSurface(button) {
  const nextNativeOpen = button.dataset.surface === "native";
  if (state.nativeOpen === nextNativeOpen) {
    return state.nativeOpen ? ensureNativeFrontendLoaded() : undefined;
  }

  if (nextNativeOpen) suspendWeatherForecast();
  state.nativeOpen = nextNativeOpen;
  setStoredSurface(state.nativeOpen ? "native" : "app");
  state.controlError = "";
  state.controlNotice = "";
  state.settingsInfoOpen = "";
  updateFirmwareState({
    updateModalOpen: false,
    firmwareAdvancedOpen: false,
    firmwareConnectionSwitchOpen: false,
    firmwareTopologySwitchOpen: false,
    updateManualUploadOpen: false,
    updateTestFirmwareOpen: false,
    firmwareConnectionSwitchConfirmed: false,
    firmwareTopologySwitchConfirmed: false,
  });
  resetFirmwareManualUploadSelection();
  resetFirmwareTestSelection();
  stopLoginAuthStatusPolling();
  state.systemModal = "";
  const nativeLoad = state.nativeOpen ? ensureNativeFrontendLoaded() : undefined;
  render();
  syncSurfaceRuntime();
  window.requestAnimationFrame(() => {
    if (state.nativeOpen && state.nativeApp) {
      state.nativeApp.scrollIntoView({ behavior: "smooth", block: "start" });
    } else if (!state.nativeOpen) {
      window.scrollTo({ top: 0, behavior: "smooth" });
    }
  });
  return nativeLoad;
}

const shellActionHandlers = {
  ...(__OQ_PREVIEW__ ? {
    "set-mock-boiler": (button) => {
      if (typeof window.__OQ_SET_MOCK_BOILER__ === "function") {
        window.__OQ_SET_MOCK_BOILER__(button.dataset.boilerMode || "off");
      }
    },
    "toggle-dev-panel": () => {
      setDevPanelOpen(!state.devPanelOpen);
      render();
    },
  } : {}),
  "toggle-interface-panel": () => {
    setInterfacePanelOpen(!state.interfacePanelOpen);
    render();
  },
  "select-surface": (button) => selectSurface(button),
  "matrix-version-tap": () => {
    registerMatrixVersionTap();
  },
};

export function handleShellAction(action, button) {
  return invokeActionMap(shellActionHandlers, action, button);
}
