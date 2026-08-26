import { hasEntity, isEntityActive } from "./app-shared.js";
import { isCurveMode } from "./domain-helpers.js";
import { getEntityValue } from "./entity-store.js";

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
        title: "Geen actieve thermostaatbron beschikbaar",
        copy: "Configureer of activeer eerst één gekoppelde bron voor kamertemperatuur en kamer-setpoint. Warmtetoestemming wordt niet automatisch op een inactieve bron gezet.",
        recommended: "",
        deviant: true,
      };
    }
    if (current === "Disabled") {
      return {
        tone: "warning",
        title: "Warmtetoestemming staat op Niet gebruiken",
        copy: "Zonder thermostaat kan de stooklijn verwarmen terwijl de kamer al warm is. Met een thermostaat als toestemming voorkom je dat.",
        recommended,
        deviant,
      };
    }
    if (deviant) {
      return {
        tone: "info",
        title: "Andere toestemming dan aanbevolen",
        copy: `Voor stooklijn adviseren we ${recommended}. Je gebruikt nu ${current || "onbekend"}.`,
        recommended,
        deviant,
      };
    }
    return {
      tone: "info",
      title: "Goed zo — thermostaat en stooklijn vullen elkaar aan",
      copy: "Thermostaat bepaalt óf er verwarmd wordt, de stooklijn hoe warm.",
      recommended,
      deviant: false,
    };
  }
  // Power House
  if (current !== "Disabled" && current) {
    return {
      tone: "warning",
      title: "Externe toestemming bij Power House",
      copy: "Power House bepaalt zelf of verwarmen nodig is. Een extra thermostaat als harde schakelaar laat de pomp vaker aan en uit gaan. Alleen handig bij zone-verwarming.",
      recommended,
      deviant,
    };
  }
  return {
    tone: "info",
    title: "Goed zo — Power House regelt de warmtevraag zelf",
    copy: "Geen extra toestemming nodig. Power House kijkt zelf naar kamer en buitentemperatuur.",
    recommended,
    deviant: false,
  };
}

export const STRATEGY_CONFIG_MATRIX = {
  roomTemp: { powerHouse: "vereist", curve: "aanbevolen" },
  roomSetpoint: { powerHouse: "vereist", curve: "aanbevolen" },
  outsideTemp: { powerHouse: "vereist", curve: "vereist" },
  waterSupply: { powerHouse: "nodig voor begrenzing", curve: "vereist" },
  flow: { powerHouse: "vereist", curve: "vereist" },
  heatingEnable: { powerHouse: "meestal Niet gebruiken", curve: "meestal externe thermostaat/zonevraag" },
};
