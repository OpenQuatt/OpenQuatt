import { invokeActionMap } from "../core/action-router.js";
import { commitSwitch } from "../core/entity-write-actions.js";
import { render } from "../core/render-scheduler.js";
import { clearQuickStartSetupInstall, hasCompletedQuickStartSetupInstallFor, state } from "../core/state.js";
import {
  abortQuickStartFlowTest,
  applyQuickStartFlowSourceConfiguration,
  applyQuickStartHeatingEnableSource,
  applyQuickStartThermostatSourceConfiguration,
  initializeQuickStartPerformanceTelemetryChoice,
  initializeQuickStartUsageTelemetryChoice,
  refreshQuickStartFlowSignal,
  refreshQuickStartStepHydration,
  startQuickStartFlowTest,
} from "./quickstart-actions.js";
import { isQuickStartStepSelectionAllowed, selectQuickStepByOffset } from "./quickstart.js";
import { t } from "../i18n/index.js";
import { installQuickStartSetupSwitch, keepCurrentQuickStartSetup } from "./firmware-actions.js";
import { getFirmwareBuildConnection, getInstallationTopology } from "./device-context.js";
import {
  captureUsageTelemetryPreview,
  loadUsageTelemetryPreviewMqttEnabled,
} from "../core/usage-telemetry-preview.js";

const USAGE_TELEMETRY_PREPARATION_ACTION = "quickstart-usage-telemetry-prepare";
let quickStartPreparationId = 0;

export function confirmQuickStartSetup(confirmed) {
  if (confirmed && !state.quickStartSetupDraft) {
    const topology = getInstallationTopology();
    const connection = getFirmwareBuildConnection();
    state.quickStartSetupDraft = topology && connection ? `${topology}:${connection}` : "";
  }
  state.quickStartSetupConfirmed = Boolean(confirmed) && Boolean(state.quickStartSetupDraft);
}

async function prepareQuickStartStep(stepId) {
  const preparationId = ++quickStartPreparationId;
  const preparesUsageTelemetry = stepId === "usage-telemetry";
  const preparesPerformanceTelemetry = stepId === "performance-telemetry";
  const preparesTelemetry = preparesUsageTelemetry || preparesPerformanceTelemetry;
  if (preparesTelemetry) {
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
    if (preparesPerformanceTelemetry) {
      await initializeQuickStartPerformanceTelemetryChoice();
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
  if (!selectQuickStepByOffset(offset)) {
    state.controlError = t("quickStartUi.completeSetupFirst");
    render();
    return;
  }
  if (state.currentStep === "usage-telemetry" || state.currentStep === "performance-telemetry") {
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
    state.quickStartSetupDraft = "";
    state.quickStartSetupConfirmed = false;
    state.quickStartModalMode = "wizard";
    state.quickStartModalOpen = true;
    render();
  },
  "open-generation-modal": () => {
    if (!isQuickStartStepSelectionAllowed("generation")) {
      state.currentStep = "setup";
      state.quickStartModalMode = "wizard";
      state.quickStartModalOpen = true;
      state.controlError = t("quickStartUi.completeSetupFirst");
      render();
      return;
    }
    state.currentStep = "generation";
    state.quickStartModalMode = "generation";
    state.quickStartModalOpen = true;
    render();
  },
  "select-step": (button) => {
    const stepId = button.dataset.stepId || "generation";
    if (!isQuickStartStepSelectionAllowed(stepId)) {
      state.controlError = t("quickStartUi.completeSetupFirst");
      render();
      return;
    }
    state.currentStep = stepId;
    if (state.currentStep === "usage-telemetry" || state.currentStep === "performance-telemetry") {
      state.controlError = "";
      state.controlNotice = "";
    }
    render();
    void prepareQuickStartStep(state.currentStep);
  },
  "select-quickstart-setup": (button) => {
    const target = button.dataset.setupTarget || "";
    const [targetTopology, targetConnection] = target.split(":");
    const preserveCompletedInstall = hasCompletedQuickStartSetupInstallFor(targetTopology, targetConnection);
    if (!preserveCompletedInstall) {
      clearQuickStartSetupInstall();
    }
    state.quickStartSetupUpdateComplete = preserveCompletedInstall;
    state.quickStartSetupDraft = target;
    state.quickStartSetupConfirmed = false;
    state.controlError = "";
    state.controlNotice = "";
    render();
    void refreshQuickStartStepHydration("setup");
  },
  "install-quickstart-setup": () => installQuickStartSetupSwitch(),
  "keep-current-quickstart-setup": () => keepCurrentQuickStartSetup(),
  "apply-quickstart-flow-source": () => applyQuickStartFlowSourceConfiguration(),
  "refresh-quickstart-flow-signal": () => refreshQuickStartFlowSignal(),
  "start-quickstart-flow-test": () => startQuickStartFlowTest(),
  "abort-quickstart-flow-test": () => abortQuickStartFlowTest(),
  "apply-quickstart-thermostat-source": () => applyQuickStartThermostatSourceConfiguration(),
  "apply-quickstart-heating-enable": (button) => applyQuickStartHeatingEnableSource(button?.dataset?.heatingEnableTarget || null),
  "retry-usage-telemetry-choice": () => prepareQuickStartStep("usage-telemetry"),
  "confirm-no-usage-telemetry": () => commitSwitch("usageTelemetryEnabled", false),
  "retry-performance-telemetry-choice": () => prepareQuickStartStep("performance-telemetry"),
  "confirm-no-performance-telemetry": () => commitSwitch("performanceTelemetryEnabled", false),
  "previous-step": () => moveQuickStartStep(-1),
  "next-step": () => moveQuickStartStep(1),
};

export function handleQuickStartAction(action, button) {
  return invokeActionMap(quickStartActionHandlers, action, button);
}
