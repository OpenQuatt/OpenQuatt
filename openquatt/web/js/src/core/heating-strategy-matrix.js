import { hasEntity, isEntityActive } from "./app-shared.js";
import { isCurveMode } from "./domain-helpers.js";
import { getEntityValue } from "./entity-store.js";
import { t } from "../i18n/index.js";

export const HEATING_ENABLE_RECOMMENDED_POWER_HOUSE = "Disabled";
export const HEATING_ENABLE_RECOMMENDED_CURVE_OT = "OT thermostat";
export const HEATING_ENABLE_RECOMMENDED_CURVE_FALLBACK = "CIC";

function hasConfiguredCicFeed() {
  if (!hasEntity("cicFeedUrl")) {
    return false;
  }
  const value = String(getEntityValue("cicFeedUrl") || "").trim().toLowerCase();
  return Boolean(value) && value !== "unknown" && value !== "unavailable";
}

export function getActiveConfiguredThermostatSource() {
  const roomTempSource = String(getEntityValue("roomTempSource") || "").trim();
  const roomSetpointSource = String(getEntityValue("roomSetpointSource") || "").trim();
  if (!roomTempSource || roomTempSource !== roomSetpointSource) {
    return "";
  }
  if (roomTempSource === HEATING_ENABLE_RECOMMENDED_CURVE_OT) {
    return hasEntity("otEnabled") && isEntityActive("otEnabled") ? roomTempSource : "";
  }
  if (roomTempSource === HEATING_ENABLE_RECOMMENDED_CURVE_FALLBACK) {
    return hasEntity("cicPollingEnabled") && isEntityActive("cicPollingEnabled") && hasConfiguredCicFeed()
      ? roomTempSource
      : "";
  }
  return roomTempSource === "HA input" ? roomTempSource : "";
}

export function getHeatingEnableRecommendation(strategyValue = getEntityValue("strategy")) {
  const isCurve = isCurveMode(strategyValue);
  if (isCurve) {
    return getActiveConfiguredThermostatSource();
  }
  return HEATING_ENABLE_RECOMMENDED_POWER_HOUSE;
}

export function getHeatingEnableCurrent() {
  return String(getEntityValue("heatingEnableSource") || "").trim();
}

export function isHeatingEnableRecommendationDeviant(strategyValue = getEntityValue("strategy")) {
  const recommended = getHeatingEnableRecommendation(strategyValue);
  const current = getHeatingEnableCurrent();
  if (!current) {
    return false;
  }
  if (isCurveMode(strategyValue) && !recommended) {
    return true;
  }
  return current !== recommended;
}

export function getHeatingEnableAdvice(strategyValue = getEntityValue("strategy")) {
  const isCurve = isCurveMode(strategyValue);
  const recommended = getHeatingEnableRecommendation(strategyValue);
  const current = getHeatingEnableCurrent();
  const deviant = current && current !== recommended;
  if (isCurve) {
    if (!recommended) {
      return {
        tone: "warning",
        title: t("heatingAdvice.matrixNoSourceTitle"),
        copy: t("heatingAdvice.matrixNoSourceCopy"),
        recommended: "",
        deviant: true,
      };
    }
    if (current === "Disabled") {
      return {
        tone: "warning",
        title: t("heatingAdvice.matrixDisabledTitle"),
        copy: t("heatingAdvice.matrixDisabledCopy"),
        recommended,
        deviant,
      };
    }
    if (deviant) {
      return {
        tone: "info",
        title: t("heatingAdvice.matrixOtherTitle"),
        copy: t("heatingAdvice.matrixOtherCopy", { recommended, current: current || t("heatpump.summaryUnknown") }),
        recommended,
        deviant,
      };
    }
    return {
      tone: "info",
      title: t("heatingAdvice.matrixOkCurveTitle"),
      copy: t("heatingAdvice.matrixOkCurveCopy"),
      recommended,
      deviant: false,
    };
  }
  // Power House
  if (current !== "Disabled" && current) {
    return {
      tone: "warning",
      title: t("heatingAdvice.matrixExternalTitle"),
      copy: t("heatingAdvice.matrixExternalCopy"),
      recommended,
      deviant,
    };
  }
  return {
    tone: "info",
    title: t("heatingAdvice.matrixOkPhTitle"),
    copy: t("heatingAdvice.matrixOkPhCopy"),
    recommended,
    deviant: false,
  };
}

function localizedMatrixEntry(powerHouseKey, curveKey) {
  return {
    get powerHouse() { return t(powerHouseKey); },
    get curve() { return t(curveKey); },
  };
}

export const STRATEGY_CONFIG_MATRIX = {
  roomTemp: localizedMatrixEntry("heatingAdvice.matrixRequired", "heatingAdvice.matrixRecommended"),
  roomSetpoint: localizedMatrixEntry("heatingAdvice.matrixRequired", "heatingAdvice.matrixRecommended"),
  outsideTemp: localizedMatrixEntry("heatingAdvice.matrixRequired", "heatingAdvice.matrixRequired"),
  waterSupply: localizedMatrixEntry("heatingAdvice.matrixNeededForLimit", "heatingAdvice.matrixRequired"),
  flow: localizedMatrixEntry("heatingAdvice.matrixRequired", "heatingAdvice.matrixRequired"),
  heatingEnable: localizedMatrixEntry("heatingAdvice.matrixUsuallyDisabled", "heatingAdvice.matrixUsuallyExternal"),
};
