import { invokeActionMap } from "./action-router.js";
import { hasEntity } from "./app-shared.js";
import { verifyEntityBackupSelectState } from "./entity-backup.js";
import { getCurveFallbackSuggestion, getEntityValue } from "./entity-store.js";
import { commitNumber, commitSelect, commitSwitch, triggerButton } from "./entity-write-actions.js";
import { getHeatingEnableRecommendation } from "./heating-strategy-matrix.js";
import { t } from "../i18n/index.js";
import { state } from "./state.js";

async function commitConfirmedSelection(key, value, commit, confirm) {
  const writeAccepted = await commit(key, value);
  if (!writeAccepted) {
    return { ok: false, writeAccepted: false, error: state.controlError };
  }
  try {
    const confirmed = await confirm(key, value);
    return {
      ok: confirmed,
      writeAccepted: true,
      error: confirmed ? "" : t("controlActions.notConfirmed", { field: t(key === "strategy" ? "controlActions.strategy" : "controlActions.heatingEnable") }),
    };
  } catch (error) {
    return {
      ok: false,
      writeAccepted: true,
      error: t("controlActions.confirmFailed", { field: t(key === "strategy" ? "controlActions.strategy" : "controlActions.heatingEnable"), error: error.message }),
    };
  }
}

export async function commitQuickStartStrategySelection(option, commit = commitSelect, confirm = verifyEntityBackupSelectState) {
  const previousStrategy = String(getEntityValue("strategy") || "");
  const previousHeatingEnable = String(getEntityValue("heatingEnableSource") || "");
  const strategyResult = await commitConfirmedSelection("strategy", option, commit, confirm);
  if (!strategyResult.ok) {
    let rolledBack = previousStrategy === option;
    if (previousStrategy && previousStrategy !== option) {
      rolledBack = (await commitConfirmedSelection("strategy", previousStrategy, commit, confirm)).ok;
    }
    state.controlNotice = "";
    state.controlError = rolledBack
      ? t("controlActions.heatingEnableUnchanged", { error: strategyResult.error })
      : t("controlActions.strategyRestoreFailed", { error: strategyResult.error });
    return false;
  }
  if (!state.quickStartModalOpen || !hasEntity("heatingEnableSource")) {
    return true;
  }

  const recommended = getHeatingEnableRecommendation(option);
  const current = String(getEntityValue("heatingEnableSource") || "");
  if (!recommended || current === recommended) {
    return true;
  }

  const heatingEnableResult = await commitConfirmedSelection("heatingEnableSource", recommended, commit, confirm);
  if (heatingEnableResult.ok) {
    return true;
  }

  const heatingEnableRolledBack = previousHeatingEnable === recommended
    ? true
    : previousHeatingEnable
      ? (await commitConfirmedSelection("heatingEnableSource", previousHeatingEnable, commit, confirm)).ok
      : false;
  const strategyRolledBack = previousStrategy === option
    ? true
    : previousStrategy
      ? (await commitConfirmedSelection("strategy", previousStrategy, commit, confirm)).ok
      : false;
  state.controlNotice = "";
  state.controlError = heatingEnableRolledBack && strategyRolledBack
    ? t("controlActions.strategyReverted", { error: heatingEnableResult.error })
    : t("controlActions.combinationRestoreFailed", { error: heatingEnableResult.error });
  return false;
}

const controlActionHandlers = {
  "select-settings-option": async (button) => {
    const key = button.dataset.selectKey || "";
    const option = button.dataset.selectOption || "";
    if (key && option && String(getEntityValue(key) || "") !== option) {
      if (key === "strategy" && state.quickStartModalOpen) {
        return commitQuickStartStrategySelection(option);
      }
      return commitSelect(key, option);
    }
    return true;
  },
  "toggle-overview-control": (button) => {
    const key = button.dataset.controlKey || "";
    const nextState = (button.dataset.controlState || "").toLowerCase();
    if (key && (nextState === "on" || nextState === "off")) {
      commitSwitch(key, nextState === "on");
    }
  },
  "select-overview-control-option": (button) => {
    const key = button.dataset.controlKey || "";
    const option = button.dataset.controlOption || "";
    if (key && option && String(getEntityValue(key) || "") !== option) {
      commitSelect(key, option);
    }
  },
  "suggest-curve-fallback": () => {
    const suggestion = getCurveFallbackSuggestion();
    if (suggestion) {
      commitNumber("curveFallbackSupply", suggestion.value, t("controlActions.curveFallbackApplied"));
    }
  },
  apply: () => triggerButton("apply"),
  reset: () => triggerButton("reset"),
};

export function handleControlAction(action, button) {
  return invokeActionMap(controlActionHandlers, action, button);
}
