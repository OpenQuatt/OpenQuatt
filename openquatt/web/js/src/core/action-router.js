import { render } from "./render-scheduler.js";
import { state } from "./state.js";
import { t } from "../i18n/index.js";

function getActionErrorMessage(error) {
  if (error instanceof Error && error.message) {
    return error.message;
  }
  return String(error || t("actions.unknownError"));
}

export function reportActionError(action, error) {
  state.controlError = t("actions.actionFailedGeneric", {
    action: action || t("actions.unknownAction"),
    error: getActionErrorMessage(error),
  });
  render();
  console.error(`[OpenQuatt] Action failed: ${action || "(unknown)"}`, error);
}

export function invokeActionMap(actionHandlers, action, ...args) {
  const handler = actionHandlers[action];
  if (!handler) {
    return false;
  }

  try {
    const result = handler(...args);
    if (result && typeof result.then === "function") {
      result.catch((error) => reportActionError(action, error));
    }
  } catch (error) {
    reportActionError(action, error);
  }
  return true;
}

export function reportUnknownAction(action, element = null) {
  if (!action) {
    return;
  }
  console.warn(`[OpenQuatt] Unknown action: ${action}`, element || "");
}
