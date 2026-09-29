import { invokeActionMap } from "./action-router.js";
import { CURVE_POINTS } from "./config.js";
import { hasEntity } from "./app-shared.js";
import { verifyEntityBackupSelectState } from "./entity-backup.js";
import { getCurveFallbackSuggestion, getEntityValue } from "./entity-store.js";
import { commitNumber, commitSelect, commitSwitch, triggerButton } from "./entity-write-actions.js";
import { refreshEntities } from "./entity-sync.js";
import { fetchWithTimeout } from "./browser-utils.js";
import { getBasePath } from "./url-path.js";
import { getHeatingEnableRecommendation } from "./heating-strategy-matrix.js";
import { t } from "../i18n/index.js";
import { state } from "./state.js";
import { applySimpleCurveBatch, applySimpleCurvePoints, generateSimpleCurve, getSimpleCurveDraft } from "./simple-curve.js";
import { render } from "./render-scheduler.js";

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

async function submitSimpleCurveBatch(points) {
  const statusResponse = await fetchWithTimeout("/auth/status", { cache: "no-store" }, 8000);
  if (!statusResponse.ok) return "rejected";
  const csrfToken = String((await statusResponse.json()).csrf_token || "");
  if (!csrfToken) return "rejected";
  const body = new URLSearchParams({ csrf_token: csrfToken });
  for (const point of points) body.set(point.key, point.value.toFixed(1));
  const response = await fetchWithTimeout(`${getBasePath()}/openquatt/curve/apply`, {
    method: "POST",
    headers: { "Content-Type": "application/x-www-form-urlencoded" },
    body,
  }, 8000);
  if (response.status === 404) return "unsupported";
  if (response.status !== 202) return "rejected";
  const result = await response.json();
  return result.ok === true && result.queued === true ? "accepted" : "rejected";
}

const controlActionHandlers = {
  "apply-simple-curve": async () => {
    if (state.simpleCurveApplying) return false;
    const points = generateSimpleCurve(getSimpleCurveDraft().slope, getSimpleCurveDraft().level);
    const originals = CURVE_POINTS.map((point) => {
      const value = getEntityValue(point.key);
      return value == null || value === "" ? NaN : Number(value);
    });
    if (!points || originals.some((value) => !Number.isFinite(value))) return false;
    state.simpleCurveApplying = true;
    render();
    const batch = await applySimpleCurveBatch(points, submitSimpleCurveBatch,
      () => refreshEntities(CURVE_POINTS.map((point) => point.key), "state"), getEntityValue);
    const result = batch.unsupported
      ? await applySimpleCurvePoints(points, originals,
        async (key, value) => (await commitNumber(key, value)) && !state.controlError,
        getEntityValue)
      : batch;
    if (!result.applied) {
      state.controlError = batch.unsupported
        ? t(result.restored ? "settingsHeating.simpleApplyFailed" : "settingsHeating.simpleRestoreFailed")
        : t("settingsHeating.simpleApplyUnconfirmed");
    } else {
      state.simpleCurveDraft = null;
      state.controlNotice = t("settingsHeating.simpleApplied");
    }
    state.simpleCurveApplying = false;
    render();
    return result.applied;
  },
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
