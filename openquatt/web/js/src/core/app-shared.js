import { APP_VIEWS, renderOqIcon } from "./config.js";
import { isCurveMode } from "./domain-helpers.js";
import { formatValue, getEntityValue, hasEntity } from "./entity-store.js";
import { formatNumericState } from "./formatting.js";
import { escapeHtml } from "./html.js";
import { state } from "./state.js";

export { hasEntity } from "./entity-store.js";

  export function renderAppSummary() {
    const parts = [];
    parts.push(isCurveMode() ? "Stooklijn" : "Power House");
    const profile = String(getEntityValue(isCurveMode() ? "curveControlProfile" : "phResponseProfile") || "").trim();
    if (profile) {
      parts.push(`profiel ${profile}`);
    }
    const flowMode = String(getEntityValue("flowControlMode") || "").trim();
    if (flowMode) {
      parts.push(`flow ${flowMode === "Manual PWM" ? "handmatig" : "setpoint"}`);
    }
    if (flowMode === "Manual PWM" && hasEntity("manualIpwm")) {
      parts.push(`iPWM ${formatValue("manualIpwm")}`);
    } else if (hasEntity("flowSetpoint")) {
      parts.push(`flow ${formatValue("flowSetpoint")}`);
    }

    if (hasEntity("dayHeatingMaxHz")) {
      parts.push(`dag H ${formatValue("dayHeatingMaxHz")} / C ${formatValue("dayCoolingMaxHz")}`);
      parts.push(`stil H ${formatValue("silentHeatingMaxHz")} / C ${formatValue("silentCoolingMaxHz")}`);
    } else {
      if (hasEntity("dayMax")) {
        parts.push(`dag ${formatValue("dayMax")}`);
      }
      if (hasEntity("silentMax")) {
        parts.push(`stil ${formatValue("silentMax")}`);
      }
    }
    if (hasEntity("maxWater")) {
      parts.push(`max water ${formatValue("maxWater")}`);
    }

    return parts.filter(Boolean).join(", ") || "Instellingen beschikbaar";
  }

  export function getEntityStateText(key, fallback = "—") {
    const entity = state.entities[key];
    if (!entity) {
      return fallback;
    }
    if (typeof entity.state === "string" && entity.state.trim() !== "") {
      return entity.state;
    }
    const value = entity.value ?? entity.state;
    if (value === undefined || value === null || value === "") {
      return fallback;
    }
    if (typeof value === "boolean") {
      return value ? "Aan" : "Uit";
    }
    if (typeof value === "number" && !Number.isNaN(value)) {
      return entity.uom ? `${value} ${entity.uom}` : String(value);
    }
    return String(value);
  }

  export function getEntityNumericValue(key) {
    const value = Number(getEntityValue(key));
    return Number.isNaN(value) ? NaN : value;
  }

  export function isEfficiencyKey(key) {
    const normalized = String(key || "").toLowerCase();
    return normalized.includes("cop") || normalized.includes("eer");
  }

  export function getDerivedEfficiencyValue(key) {
    const normalized = String(key || "");
    if (normalized === "totalEer") {
      const output = getEntityNumericValue("totalCoolingPower");
      const input = getEntityNumericValue("coolingPowerInput");
      const fallbackInput = Number.isNaN(input) ? getEntityNumericValue("totalPower") : input;
      return (!Number.isNaN(output) && !Number.isNaN(fallbackInput) && fallbackInput >= 5.0)
        ? output / fallbackInput
        : NaN;
    }
    if (normalized === "totalCop") {
      const output = getEntityNumericValue("totalHeat");
      const input = getEntityNumericValue("heatingPowerInput");
      const fallbackInput = Number.isNaN(input) ? getEntityNumericValue("totalPower") : input;
      return (!Number.isNaN(output) && !Number.isNaN(fallbackInput) && fallbackInput >= 5.0)
        ? output / fallbackInput
        : NaN;
    }
    return NaN;
  }

  export function getEntityDisplayUnit(key, fallbackUnit = "") {
    const entityUnit = String(state.entities[key]?.uom || "").trim();
    if (entityUnit) {
      return entityUnit;
    }

    const fallbackUnits = {
      totalPower: "W",
      heatingPowerInput: "W",
      coolingPowerInput: "W",
      totalHeat: "W",
      totalCoolingPower: "W",
      boilerHeatPower: "W",
      systemHeatPower: "W",
      hpCapacity: "W",
      hpDeficit: "W",
      flowSelected: "L/h",
      hp1Flow: "L/h",
      hp2Flow: "L/h",
      supplyTemp: "°C",
      curveSupplyTarget: "°C",
      coolingSupplyTarget: "°C",
      coolingEffectiveMinSupplyTemp: "°C",
      coolingDemandRaw: "W",
      hp1EvaporatorCoilTemp: "°C",
      hp1InnerCoilTemp: "°C",
      hp1OutsideTemp: "°C",
      hp1CondenserPressure: "bar",
      hp1DischargeTemp: "°C",
      hp1EvaporatorPressure: "bar",
      hp1ReturnTemp: "°C",
      hp1WaterIn: "°C",
      hp1WaterOut: "°C",
      hp2EvaporatorCoilTemp: "°C",
      hp2InnerCoilTemp: "°C",
      hp2OutsideTemp: "°C",
      hp2CondenserPressure: "bar",
      hp2DischargeTemp: "°C",
      hp2EvaporatorPressure: "bar",
      hp2ReturnTemp: "°C",
      hp2WaterIn: "°C",
      hp2WaterOut: "°C",
    };

    return fallbackUnits[key] || fallbackUnit;
  }

  export function formatOverviewStatValue(key) {
    const entity = state.entities[key];
    const derived = getDerivedEfficiencyValue(key);
    if (!entity) {
      return Number.isNaN(derived) ? "—" : formatNumericState(derived, 1, getEntityDisplayUnit(key));
    }
    const numeric = getEntityNumericValue(key);
    if (Number.isNaN(numeric)) {
      if (!Number.isNaN(derived)) {
        return formatNumericState(derived, 1, getEntityDisplayUnit(key));
      }
      return getEntityStateText(key);
    }
    const value = numeric > 0 || Number.isNaN(derived) ? numeric : derived;
    const decimals = isEfficiencyKey(key) ? 1 : 0;
    return formatNumericState(value, decimals, getEntityDisplayUnit(key));
  }

  export function isEntityActive(key) {
    const entity = state.entities[key];
    if (!entity) {
      return false;
    }
    if (typeof entity.value === "boolean") {
      return entity.value;
    }
    const raw = String(entity.state ?? entity.value ?? "").toLowerCase();
    return raw === "on" || raw === "true" || raw === "1";
  }

  export function isTrendHistoryEnabled() {
    return !hasEntity("trendHistoryEnabled") || isEntityActive("trendHistoryEnabled");
  }

  export function getSetupCompleteState() {
    const entity = state.entities.setupComplete;
    if (!entity) {
      return null;
    }

    const raw = String(entity.state ?? entity.value ?? "").trim().toLowerCase();
    if (!raw || raw === "unknown" || raw === "unavailable") {
      return null;
    }
    if (raw === "on" || raw === "true" || raw === "1") {
      return true;
    }
    if (raw === "off" || raw === "false" || raw === "0") {
      return false;
    }
    return null;
  }

  export function renderAppNav() {
    return `
      <div class="oq-helper-app-nav">
        ${APP_VIEWS.filter((view) => view.id !== "diagnosis" || isTrendHistoryEnabled()).map((view) => `
          <button
            class="oq-helper-app-tab ${state.appView === view.id ? "is-active" : ""}"
            type="button"
            data-oq-action="select-view"
            data-view-id="${escapeHtml(view.id)}"
          >
            ${renderOqIcon(view.icon, "oq-helper-app-tab-icon")}
            <span>${escapeHtml(view.label)}</span>
            ${view.badge ? `<span class="oq-helper-app-tab-badge">${escapeHtml(view.badge)}</span>` : ""}
          </button>
        `).join("")}
      </div>
    `;
  }

  export function getAppViewLabel(view = state.appView) {
    return APP_VIEWS.find((item) => item.id === view)?.label || "OpenQuatt";
  }

  export function syncDocumentTitle() {
    if (typeof document === "undefined") {
      return;
    }
    if (state.nativeOpen) {
      document.title = "ESPHome fallback • OpenQuatt";
      return;
    }
    const viewLabel = getAppViewLabel();
    document.title = `${viewLabel} • OpenQuatt`;
  }

  export function syncDocumentTheme() {
    if (typeof document === "undefined") {
      return;
    }
    if (state.nativeOpen) {
      document.documentElement.classList.add("oq-surface-native");
      if (document.body) {
        document.body.classList.add("oq-surface-native");
      }
      document.documentElement.classList.remove("oq-page-dark", "oq-page-light");
      if (document.body) {
        document.body.classList.remove("oq-page-dark", "oq-page-light");
      }
      return;
    }

    document.documentElement.classList.remove("oq-surface-native");
    if (document.body) {
      document.body.classList.remove("oq-surface-native");
    }

    const isDark = state.overviewTheme === "dark";
    document.documentElement.classList.toggle("oq-page-dark", isDark);
    document.documentElement.classList.toggle("oq-page-light", !isDark);
    if (document.body) {
      document.body.classList.toggle("oq-page-dark", isDark);
      document.body.classList.toggle("oq-page-light", !isDark);
    }
  }
