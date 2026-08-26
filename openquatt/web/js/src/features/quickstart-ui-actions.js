import { invokeActionMap } from "../core/action-router.js";
import { commitSwitch } from "../core/entity-write-actions.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import {
  abortQuickStartFlowTest,
  applyQuickStartFlowSourceConfiguration,
  applyQuickStartHeatingEnableSource,
  applyQuickStartThermostatSourceConfiguration,
  initializeQuickStartUsageTelemetryChoice,
  refreshQuickStartFlowSignal,
  refreshQuickStartStepHydration,
  startQuickStartFlowTest,
} from "./quickstart-actions.js";
import { selectQuickStepByOffset } from "./quickstart.js";
import { installQuickStartSetupSwitch } from "./firmware-actions.js";
import {
  captureUsageTelemetryPreview,
  loadUsageTelemetryPreviewMqttEnabled,
} from "../core/usage-telemetry-preview.js";

const USAGE_TELEMETRY_PREPARATION_ACTION = "quickstart-usage-telemetry-prepare";
let quickStartPreparationId = 0;

async function prepareQuickStartStep(stepId) {
  const preparationId = ++quickStartPreparationId;
  const preparesUsageTelemetry = stepId === "usage-telemetry";
  if (preparesUsageTelemetry) {
    if (state.busyAction && state.busyAction !== USAGE_TELEMETRY_PREPARATION_ACTION) {
      return;
    }
    state.busyAction = USAGE_TELEMETRY_PREPARATION_ACTION;
    render();
  } else if (state.busyAction === USAGE_TELEMETRY_PREPARATION_ACTION) {
    state.busyAction = "";
    render();
  }

  try {
    await refreshQuickStartStepHydration(stepId);
    if (preparationId !== quickStartPreparationId || state.currentStep !== stepId) {
      return;
    }
    if (preparesUsageTelemetry) {
      await initializeQuickStartUsageTelemetryChoice();
      if (preparationId !== quickStartPreparationId || state.currentStep !== stepId) {
        return;
      }
      const mqttEnabled = await loadUsageTelemetryPreviewMqttEnabled();
      if (preparationId !== quickStartPreparationId || state.currentStep !== stepId) {
        return;
      }
      captureUsageTelemetryPreview("quickstart", { mqttEnabled });
    }
  } finally {
    if (preparationId === quickStartPreparationId
      && state.busyAction === USAGE_TELEMETRY_PREPARATION_ACTION) {
      state.busyAction = "";
      render();
    }
  }
}

function moveQuickStartStep(offset) {
  selectQuickStepByOffset(offset);
  if (state.currentStep === "usage-telemetry") {
    state.controlError = "";
    state.controlNotice = "";
  }
  render();
  void prepareQuickStartStep(state.currentStep);
}

const quickStartActionHandlers = {
  "close-quickstart-modal": () => {
    quickStartPreparationId += 1;
    if (state.busyAction === USAGE_TELEMETRY_PREPARATION_ACTION) {
      state.busyAction = "";
    }
    state.quickStartModalOpen = false;
    render();
  },
  "open-quickstart-modal": () => {
    state.currentStep = "setup";
    state.quickStartModalMode = "wizard";
    state.quickStartModalOpen = true;
    render();
  },
  "open-generation-modal": () => {
    state.currentStep = "generation";
    state.quickStartModalMode = "generation";
    state.quickStartModalOpen = true;
    render();
  },
  "select-step": (button) => {
    state.currentStep = button.dataset.stepId || "generation";
    if (state.currentStep === "usage-telemetry") {
      state.controlError = "";
      state.controlNotice = "";
    }
    render();
    void prepareQuickStartStep(state.currentStep);
  },
  "select-quickstart-setup": (button) => {
    state.quickStartSetupDraft = button.dataset.setupTarget || "";
    state.quickStartSetupConfirmed = false;
    state.controlError = "";
    state.controlNotice = "";
    render();
    void refreshQuickStartStepHydration("setup");
  },
  "install-quickstart-setup": () => installQuickStartSetupSwitch(),
  "apply-quickstart-flow-source": () => applyQuickStartFlowSourceConfiguration(),
  "refresh-quickstart-flow-signal": () => refreshQuickStartFlowSignal(),
  "start-quickstart-flow-test": () => startQuickStartFlowTest(),
  "abort-quickstart-flow-test": () => abortQuickStartFlowTest(),
  "apply-quickstart-thermostat-source": () => applyQuickStartThermostatSourceConfiguration(),
  "apply-quickstart-heating-enable": (button) => applyQuickStartHeatingEnableSource(button?.dataset?.heatingEnableTarget || null),
  "retry-usage-telemetry-choice": () => prepareQuickStartStep("usage-telemetry"),
  "confirm-no-usage-telemetry": () => commitSwitch("usageTelemetryEnabled", false),
  "previous-step": () => moveQuickStartStep(-1),
  "next-step": () => moveQuickStartStep(1),
};

export function handleQuickStartAction(action, button) {
  return invokeActionMap(quickStartActionHandlers, action, button);
}
