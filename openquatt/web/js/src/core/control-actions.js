import { invokeActionMap } from "./action-router.js";
import { hasEntity } from "./app-shared.js";
import { verifyEntityBackupSelectState } from "./entity-backup.js";
import { getCurveFallbackSuggestion, getEntityValue } from "./entity-store.js";
import { commitNumber, commitSelect, commitSwitch, triggerButton } from "./entity-write-actions.js";
import { getHeatingEnableRecommendation } from "./heating-strategy-matrix.js";
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
      error: confirmed ? "" : `${key === "strategy" ? "Verwarmingsstrategie" : "Warmtetoestemming"} is niet door de controller bevestigd.`,
    };
  } catch (error) {
    return {
      ok: false,
      writeAccepted: true,
      error: `${key === "strategy" ? "Verwarmingsstrategie" : "Warmtetoestemming"} kon niet worden bevestigd. ${error.message}`,
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
      ? `${strategyResult.error} De warmtetoestemming is niet aangepast.`
      : `${strategyResult.error} De vorige strategie kon niet worden hersteld; controleer beide instellingen.`;
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
    ? `${heatingEnableResult.error} De strategieswitch is daarom teruggezet.`
    : `${heatingEnableResult.error} De vorige combinatie kon niet volledig worden hersteld; controleer beide instellingen.`;
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
      commitNumber("curveFallbackSupply", suggestion.value, "Fallback-aanvoertemperatuur uit de stooklijn overgenomen.");
    }
  },
  apply: () => triggerButton("apply"),
  reset: () => triggerButton("reset"),
};

export function handleControlAction(action, button) {
  return invokeActionMap(controlActionHandlers, action, button);
}
