import { describeFrequencyLimit } from "./frequency-limits.js";
import { getEntityNumericValue, getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";
import { renderOqIcon } from "../core/config.js";
import { escapeHtml } from "../core/html.js";
import { getRenderSignature } from "../core/render-signatures.js";
import { state } from "../core/state.js";
import { setViewPatchControls } from "../core/view-patch-controls.js";
import { getControlReplayIncidentDisplaySeverity, getControlReplayIncidentEventCopy, getControlReplayIncidentModeAfterEvent, getControlReplayIncidentModeTransition, getControlReplayIncidentReasonMeta } from "./control-replay-incidents.js";
import { formatWorkingMode, getHeatPumpPanels } from "../views/heatpump.js";
import { isCoolingOverviewActive } from "../views/overview.js";
import { replaceOuterHtmlIfSignatureChanged } from "../views/view-utils.js";
import { formatDate, formatNumber, formatTime, getIntlLocale, optionLabel, t } from "../i18n/index.js";

  function clampControlReplayPercent(value) {
    const numeric = Number(value);
    if (!Number.isFinite(numeric)) {
      return 0;
    }
    return Math.max(0, Math.min(100, numeric));
  }

  function formatControlReplayInteger(key, fallback = "—") {
    if (!hasEntity(key)) {
      return fallback;
    }
    const numeric = getEntityNumericValue(key);
    if (!Number.isFinite(numeric)) {
      return fallback;
    }
    return String(Math.round(numeric));
  }

  function formatControlReplayNumber(key, decimals = 1, unit = "", fallback = "—") {
    if (!hasEntity(key)) {
      return fallback;
    }
    const numeric = getEntityNumericValue(key);
    if (!Number.isFinite(numeric)) {
      return fallback;
    }
    return `${formatNumber(numeric, { minimumFractionDigits: decimals, maximumFractionDigits: decimals })}${unit ? ` ${unit}` : ""}`;
  }

  function formatControlReplayRuntimeHours(key, fallback = "—") {
    if (!hasEntity(key)) {
      return fallback;
    }
    const hours = getEntityNumericValue(key);
    if (!Number.isFinite(hours)) {
      return fallback;
    }
    return t("controlReplay.runtimeHours", { value: formatNumber(Math.round(hours), { maximumFractionDigits: 0 }) });
  }

  function isControlReplayHpRunning(panel) {
    if (!panel || !panel.keys) {
      return false;
    }
    const mode = formatWorkingMode(getEntityStateText(panel.keys.mode, "Unknown"));
    const compressorLevel = getEntityNumericValue(panel.keys.freq);
    return mode === t("heatpump.modeHeating")
      || mode === t("heatpump.modeCooling")
      || isEntityActive(panel.keys.defrost)
      || (mode === t("overview.statusUnknown") && Number.isFinite(compressorLevel) && compressorLevel > 0);
  }

  const CONTROL_WORKING_COOLING_LIMITER_REASONS = Object.freeze({
    0: "inactive",
    1: "full",
    2: "projected_floor",
    3: "simmer",
    4: "falling_gap",
    5: "buffer_stop",
    6: "dew_stop",
    7: "fallback_floor",
    8: "restart_wait",
    9: "room_cap",
    10: "fallback_cap1",
    11: "level1_hold",
    12: "oil_return_hold",
    13: "oil_return_recovery",
    14: "capacity_cap",
  });

  function normalizeControlWorkingCoolingReason(reasonCode) {
    const normalized = String(reasonCode || "").trim().toLowerCase();
    if (!normalized) {
      return "";
    }
    const numericCode = Number(normalized);
    if (Number.isInteger(numericCode)) {
      return CONTROL_WORKING_COOLING_LIMITER_REASONS[numericCode] || "unknown";
    }
    return normalized;
  }

  function isControlWorkingCoolingReasonInactive(reasonCode) {
    return ["", "full", "inactive", "none", "unknown", "unavailable"].includes(normalizeControlWorkingCoolingReason(reasonCode));
  }

  function isControlWorkingCoolingProtectionReason(reasonCode) {
    return [
      "dew_stop",
      "falling_gap",
      "projected_floor",
      "restart_wait",
      "sensor_fallback",
      "oil_return_recovery",
      "level1_hold",
    ].includes(normalizeControlWorkingCoolingReason(reasonCode));
  }

  function getControlReplayModeModel(heatPumpPanels) {
    const coolingRequest = isEntityActive("coolingRequestActive");
    const limiterReason = getEntityStateText("coolingLimiterReasonCode", "");
    const normalizedLimiterReason = normalizeControlWorkingCoolingReason(limiterReason);
    const coolingLimitedByLimiter = coolingRequest
      && normalizedLimiterReason
      && !isControlWorkingCoolingReasonInactive(normalizedLimiterReason);
    const coolingBlocked = coolingRequest && hasEntity("coolingPermitted") && !isEntityActive("coolingPermitted");
    const coolingProtection = coolingBlocked || (coolingLimitedByLimiter && isControlWorkingCoolingProtectionReason(normalizedLimiterReason));
    const coolingCapped = coolingLimitedByLimiter && !coolingProtection;
    const coolingMode = isCoolingOverviewActive() || coolingRequest;
    const hpRunningCount = heatPumpPanels.filter(isControlReplayHpRunning).length;
    const hp2Available = heatPumpPanels.some((panel) => panel.title === "HP2");
    const defrostActive = heatPumpPanels.some((panel) => isEntityActive(panel.keys.defrost));
    const boilerActive = hasEntity("boilerActive") && isEntityActive("boilerActive");
    return {
      title: t("controlReplay.modeTitle"),
      copy: t("controlReplay.modeCopy"),
      hpRunningCount,
      hp2Available,
      defrostActive,
      boilerActive,
      coolingMode,
      coolingRequest,
      coolingBlocked,
      coolingLimited: coolingProtection || coolingCapped,
      coolingProtection,
      coolingCapped,
      coolingLimiterReason: normalizedLimiterReason || "inactive",
    };
  }

  function normalizeControlReplayModeId(value) {
    const normalized = String(value || "").trim().toLowerCase();
    if (normalized.includes("cm100")) return "cm100";
    if (normalized.includes("cm98")) return "cm98";
    if (normalized.includes("cm5")) return "cm5";
    if (normalized.includes("cm4")) return "cm4";
    if (normalized.includes("cm3")) return "cm3";
    if (normalized.includes("cm2")) return "cm2";
    if (normalized.includes("cm1")) return "cm1";
    if (normalized.includes("cm0")) return "cm0";
    return "";
  }

  function formatControlReplayStrategyLabel() {
    const code = Math.round(getEntityNumericValue("strategyActiveCode"));
    if (code === 1) return t("overview.strategyCooling");
    if (code === 2) return t("overview.strategyCurve");
    if (code === 3) return optionLabel("Power House");
    return getEntityStateText("strategy", "—");
  }

  function getControlReplayCounterValue(key, fallback = "—") {
    const value = formatControlReplayInteger(key, fallback);
    return value === "—" ? fallback : value;
  }

  const CONTROL_WORKING_TABS = Object.freeze([
    ["status", "controlReplay.tabStatus", "shield"],
    ["timeline", "controlReplay.tabTimeline", "activity"],
    ["graphs", "controlReplay.tabGraphs", "bar-chart"],
  ].map(([id, labelKey, icon]) => Object.freeze({ id, labelKey, icon })));

  const CONTROL_WORKING_WINDOW_OPTIONS = Object.freeze([
    ["last1", "controlReplay.windowLast1", "controlReplay.windowShort1", "controlReplay.windowLast1", "controlReplay.windowCopyLast1", "controlReplay.windowGraphLast1", { durationMinutes: 60 }],
    ["last2", "controlReplay.windowLast2", "controlReplay.windowShort2", "controlReplay.windowLast2", "controlReplay.windowCopyLast2", "controlReplay.windowGraphLast2", { durationMinutes: 120 }],
    ["last4", "controlReplay.windowLast4", "controlReplay.windowShort4", "controlReplay.windowLast4", "controlReplay.windowCopyLast4", "controlReplay.windowGraphLast4", { durationMinutes: 240, quick: true }],
    ["last8", "controlReplay.windowLast8", "controlReplay.windowShort8", "controlReplay.windowLast8", "controlReplay.windowCopyLast8", "controlReplay.windowGraphLast8", { durationMinutes: 480 }],
    ["last12", "controlReplay.windowLast12", "controlReplay.windowShort12", "controlReplay.windowLast12", "controlReplay.windowCopyLast12", "controlReplay.windowGraphLast12", { durationMinutes: 720 }],
    ["last24", "controlReplay.windowLast24", "controlReplay.windowShort24", "controlReplay.windowLast24", "controlReplay.windowCopyLast24", "controlReplay.windowGraphLast24", { durationMinutes: 1440, quick: true }],
    ["last48", "controlReplay.windowLast48", "controlReplay.windowShort48", "controlReplay.windowLast48", "controlReplay.windowCopyLast48", "controlReplay.windowGraphLast48", { durationMinutes: 2880 }],
    ["last3d", "controlReplay.windowLast3d", "controlReplay.windowShort3d", "controlReplay.windowLast3d", "controlReplay.windowCopyLast3d", "controlReplay.windowGraphLast3d", { durationMinutes: 4320 }],
    ["today", "controlReplay.windowToday", "controlReplay.windowToday", "controlReplay.windowToday", "controlReplay.windowCopyToday", "controlReplay.windowGraphToday", { calendarDay: "today", quick: true }],
    ["yesterday", "controlReplay.windowYesterday", "controlReplay.windowYesterday", "controlReplay.windowYesterday", "controlReplay.windowCopyYesterday", "controlReplay.windowGraphYesterday", { calendarDay: "yesterday", quick: true }],
    ["week", "controlReplay.windowWeek", "controlReplay.windowWeek", "controlReplay.windowWeekAgo", "controlReplay.windowCopyWeek", "controlReplay.windowGraphWeek", { durationMinutes: 7 * 24 * 60, quick: true }],
    ["custom", "controlReplay.windowCustom", "controlReplay.windowCustom", "controlReplay.windowCustom", "controlReplay.windowCopyCustom", "controlReplay.windowGraphCustom", { custom: true }],
  ].map(([id, labelKey, shortLabelKey, eyebrowKey, copyKey, graphCopyKey, options]) => Object.freeze({
    id,
    labelKey,
    shortLabelKey,
    eyebrowKey,
    titleKey: "controlReplay.timelineTitle",
    copyKey,
    graphCopyKey,
    ...options,
  })));

  function resolveControlWorkingTab(tab) {
    return { ...tab, label: t(tab.labelKey) };
  }

  function resolveControlWorkingWindowOption(option) {
    return {
      ...option,
      label: t(option.labelKey),
      shortLabel: t(option.shortLabelKey),
      eyebrow: t(option.eyebrowKey),
      title: t(option.titleKey),
      copy: t(option.copyKey),
      graphCopy: t(option.graphCopyKey),
    };
  }

  function getControlWorkingTabs() {
    return CONTROL_WORKING_TABS.map(resolveControlWorkingTab);
  }

  function getControlWorkingWindowOptions() {
    return CONTROL_WORKING_WINDOW_OPTIONS.map(resolveControlWorkingWindowOption);
  }

  function getControlWorkingQuickWindowOptions() {
    return getControlWorkingWindowOptions().filter((option) => option.quick);
  }

  function getControlWorkingCustomEpoch(value) {
    const epochMs = new Date(String(value || "")).getTime();
    return Number.isFinite(epochMs) ? epochMs : Number.NaN;
  }

  function getControlWorkingCustomWindowBounds() {
    const start = getControlWorkingCustomEpoch(state.controlReplayCustomStart);
    const end = getControlWorkingCustomEpoch(state.controlReplayCustomEnd);
    if (!Number.isFinite(start) || !Number.isFinite(end) || end <= start) {
      return null;
    }
    return { start, end };
  }

  function formatControlWorkingDateTimeInput(epochMs) {
    const date = new Date(epochMs);
    date.setMinutes(0, 0, 0);
    const pad = (value) => String(value).padStart(2, "0");
    return `${date.getFullYear()}-${pad(date.getMonth() + 1)}-${pad(date.getDate())}T${pad(date.getHours())}:${pad(date.getMinutes())}`;
  }

  function formatControlWorkingDateInput(epochMs) {
    return formatControlWorkingDateTimeInput(epochMs).slice(0, 10);
  }

  function getControlWorkingCustomDateTimeParts(value) {
    const normalized = String(value || "");
    const match = normalized.match(/^(\d{4}-\d{2}-\d{2})T(\d{2}):00$/);
    return {
      date: match?.[1] || "",
      hour: match?.[2] || "00",
    };
  }

  function renderControlWorkingHourOptions(selectedHour) {
    return Array.from({ length: 24 }, (_value, hour) => {
      const value = String(hour).padStart(2, "0");
      return `<option value="${value}"${value === selectedHour ? " selected" : ""}>${value} ${escapeHtml(t("controlReplay.hourUnit"))}</option>`;
    }).join("");
  }

  function getControlWorkingCustomDraft() {
    const nowMs = Date.now();
    return {
      start: state.controlReplayCustomStart || formatControlWorkingDateTimeInput(nowMs - (24 * 60 * 60 * 1000)),
      end: state.controlReplayCustomEnd || formatControlWorkingDateTimeInput(nowMs),
    };
  }

  function getControlWorkingCustomInputBounds(draft, nowMs = Date.now()) {
    const maxRangeMs = 7 * 24 * 60 * 60 * 1000;
    const latestMs = new Date(nowMs).setMinutes(0, 0, 0);
    const earliestMs = Math.ceil((nowMs - maxRangeMs) / (60 * 60 * 1000)) * 60 * 60 * 1000;
    const draftStartMs = getControlWorkingCustomEpoch(draft.start);
    const startMs = Number.isFinite(draftStartMs)
      ? Math.max(earliestMs, Math.min(latestMs, draftStartMs))
      : latestMs - (24 * 60 * 60 * 1000);
    const draftEndMs = getControlWorkingCustomEpoch(draft.end);
    const endMs = Number.isFinite(draftEndMs)
      ? Math.max(startMs, Math.min(latestMs, draftEndMs))
      : latestMs;
    return {
      earliestDate: formatControlWorkingDateInput(earliestMs),
      latestDate: formatControlWorkingDateInput(latestMs),
      startMaxDate: formatControlWorkingDateInput(Math.min(latestMs, endMs)),
      endMinDate: formatControlWorkingDateInput(startMs),
      endMaxDate: formatControlWorkingDateInput(Math.min(latestMs, startMs + maxRangeMs)),
    };
  }

  function getControlWorkingWindowBounds(selectedWindow = getControlWorkingSelectedWindow(), nowMs = Date.now()) {
    const option = getControlWorkingWindowOptions().find((candidate) => candidate.id === selectedWindow)
      || getControlWorkingWindowOptions().find((candidate) => candidate.id === "last24");
    if (option?.calendarDay) {
      const start = new Date(nowMs);
      start.setHours(0, 0, 0, 0);
      if (option.calendarDay === "yesterday") {
        start.setDate(start.getDate() - 1);
      }
      return { start: start.getTime(), end: start.getTime() + (24 * 60 * 60 * 1000) };
    }
    if (option?.custom) {
      return getControlWorkingCustomWindowBounds() || {
        start: nowMs - (24 * 60 * 60 * 1000),
        end: nowMs,
      };
    }
    const durationMinutes = Number(option?.durationMinutes) || 1440;
    return {
      start: nowMs - (durationMinutes * 60 * 1000),
      end: nowMs,
    };
  }

  function getControlWorkingWindowDurationMinutes(selectedWindow = getControlWorkingSelectedWindow(), nowMs = Date.now()) {
    const bounds = getControlWorkingWindowBounds(selectedWindow, nowMs);
    return Math.max(1, (bounds.end - bounds.start) / (60 * 1000));
  }

  function formatControlWorkingAxisTime(epochMs, includeDay = false) {
    const intlLocale = getIntlLocale();
    const date = new Date(epochMs);
    const time = date.toLocaleTimeString(intlLocale, { hour: "2-digit", minute: "2-digit" });
    if (!includeDay) {
      return time;
    }
    const day = date.toLocaleDateString(intlLocale, { weekday: "short" }).replace(".", "");
    return `${day} ${time}`;
  }

  function getControlWorkingWindowAxis(selectedWindow = getControlWorkingSelectedWindow(), nowMs = Date.now()) {
    if (selectedWindow === "today" || selectedWindow === "yesterday") {
      return ["00:00", "06:00", "12:00", "18:00", "24:00"];
    }
    const bounds = getControlWorkingWindowBounds(selectedWindow, nowMs);
    const durationMinutes = getControlWorkingWindowDurationMinutes(selectedWindow, nowMs);
    const includeDay = durationMinutes > 24 * 60 || selectedWindow === "custom";
    return [0, 0.25, 0.5, 0.75, 1].map((fraction, index) => {
      if (index === 4 && selectedWindow !== "custom") {
        return t("controlReplay.relNow");
      }
      return formatControlWorkingAxisTime(bounds.start + ((bounds.end - bounds.start) * fraction), includeDay);
    });
  }

  function getControlWorkingSelectedTab() {
    return getControlWorkingTabs().some((tab) => tab.id === state.controlReplayTab)
      ? state.controlReplayTab
      : "status";
  }

  function getControlWorkingSelectedWindow() {
    const selected = getControlWorkingWindowOptions().find((option) => option.id === state.controlReplayWindow);
    if (selected?.custom && !getControlWorkingCustomWindowBounds()) {
      return "last24";
    }
    return selected
      ? state.controlReplayWindow
      : "last24";
  }

  function getControlWorkingWindowModel() {
    const selectedWindow = getControlWorkingSelectedWindow();
    const option = getControlWorkingWindowOptions().find((candidate) => candidate.id === selectedWindow)
      || getControlWorkingWindowOptions().find((candidate) => candidate.id === "last24");
    return {
      ...option,
      axis: getControlWorkingWindowAxis(selectedWindow),
    };
  }

  const CONTROL_WORKING_SEVERITY_METAS = Object.freeze({
    normal: { labelKey: "controlReplay.sevNormal", tone: "normal" },
    limited: { labelKey: "controlReplay.sevLimited", tone: "limited" },
    attention: { labelKey: "controlReplay.sevAttention", tone: "attention" },
    fault: { labelKey: "controlReplay.sevFault", tone: "fault" },
  });

  function getControlWorkingSeverityMeta(severity = "normal") {
    const meta = CONTROL_WORKING_SEVERITY_METAS[severity] || CONTROL_WORKING_SEVERITY_METAS.normal;
    return { label: t(meta.labelKey), tone: meta.tone };
  }

  function createControlWorkingReasonMetas(definitions) {
    return Object.freeze(Object.fromEntries(definitions.map(([code, labelKey, summaryKey, ...checkKeys]) => [
      code,
      { labelKey, summaryKey, checkKeys },
    ])));
  }

  const CONTROL_WORKING_REASON_METAS = createControlWorkingReasonMetas([
["frequency_cap_below_minimum", "controlReplay.reasonFrequencyCapBelowMinimum", "controlReplay.reasonFrequencyCapBelowMinimumCopy", "controlReplay.reasonFrequencyCapBelowMinimumCheck1", "controlReplay.reasonFrequencyCapBelowMinimumCheck2"],
["keep_current", "controlReplay.reasonKeepCurrent", "controlReplay.reasonKeepCurrentCopy", "controlReplay.reasonKeepCurrentCheck1", "controlReplay.reasonKeepCurrentCheck2", "controlReplay.reasonKeepCurrentCheck3"],
["hold_active", "controlReplay.reasonHoldActive", "controlReplay.reasonHoldActiveCopy", "controlReplay.reasonHoldActiveCheck1", "controlReplay.reasonHoldActiveCheck2", "controlReplay.reasonHoldActiveCheck3"],
["defrost_hold", "controlReplay.reasonDefrostHold", "controlReplay.reasonDefrostHoldCopy", "controlReplay.reasonDefrostHoldCheck1", "controlReplay.reasonDefrostHoldCheck2", "controlReplay.reasonDefrostHoldCheck3"],
["better_heat", "controlReplay.reasonBetterHeat", "controlReplay.reasonBetterHeatCopy", "controlReplay.reasonBetterHeatCheck1", "controlReplay.reasonBetterHeatCheck2", "controlReplay.reasonBetterHeatCheck3"],
["soft_guard", "controlReplay.reasonSoftGuard", "controlReplay.reasonSoftGuardCopy", "controlReplay.reasonSoftGuardCheck1", "controlReplay.reasonSoftGuardCheck2", "controlReplay.reasonSoftGuardCheck3"],
["less_power", "controlReplay.reasonLessPower", "controlReplay.reasonLessPowerCopy", "controlReplay.reasonLessPowerCheck1", "controlReplay.reasonLessPowerCheck2", "controlReplay.reasonLessPowerCheck3"],
["cooling_request_cleared", "controlReplay.reasonCoolingCleared", "controlReplay.reasonCoolingClearedCopy", "controlReplay.reasonCoolingClearedCheck1", "controlReplay.reasonCoolingClearedCheck2", "controlReplay.reasonCoolingClearedCheck3"],
["heating_request_cleared", "controlReplay.reasonHeatingCleared", "controlReplay.reasonHeatingClearedCopy", "controlReplay.reasonHeatingClearedCheck1", "controlReplay.reasonHeatingClearedCheck2", "controlReplay.reasonHeatingClearedCheck3"],
["no_candidate", "controlReplay.reasonNoCandidate", "controlReplay.reasonNoCandidateCopy", "controlReplay.reasonNoCandidateCheck1", "controlReplay.reasonNoCandidateCheck2", "controlReplay.reasonNoCandidateCheck3"],
["candidate_in_rest", "controlReplay.reasonCandidateRest", "controlReplay.reasonCandidateRestCopy", "controlReplay.reasonCandidateRestCheck1", "controlReplay.reasonCandidateRestCheck2", "controlReplay.reasonCandidateRestCheck3"],
["candidate_in_defrost", "controlReplay.reasonCandidateDefrost", "controlReplay.reasonCandidateDefrostCopy", "controlReplay.reasonCandidateDefrostCheck1", "controlReplay.reasonCandidateDefrostCheck2", "controlReplay.reasonCandidateDefrostCheck3"],
["candidate_unavailable", "controlReplay.reasonCandidateUnavailable", "controlReplay.reasonCandidateUnavailableCopy", "controlReplay.reasonCandidateUnavailableCheck1", "controlReplay.reasonCandidateUnavailableCheck2", "controlReplay.reasonCandidateUnavailableCheck3"],
["defrost_boost", "controlReplay.reasonDefrostBoost", "controlReplay.reasonDefrostBoostCopy", "controlReplay.reasonDefrostBoostCheck1", "controlReplay.reasonDefrostBoostCheck2", "controlReplay.reasonDefrostBoostCheck3"],
["boiler_assist", "controlReplay.reasonBoilerAssist", "controlReplay.reasonBoilerAssistCopy", "controlReplay.reasonBoilerAssistCheck1", "controlReplay.reasonBoilerAssistCheck2", "controlReplay.reasonBoilerAssistCheck3"],
["runtime_lead", "controlReplay.reasonRuntimeLead", "controlReplay.reasonRuntimeLeadCopy", "controlReplay.reasonRuntimeLeadCheck1", "controlReplay.reasonRuntimeLeadCheck2", "controlReplay.reasonRuntimeLeadCheck3"],
["oil_return_hold", "controlReplay.reasonOilReturnHold", "controlReplay.reasonOilReturnHoldCopy", "controlReplay.reasonOilReturnHoldCheck1", "controlReplay.reasonOilReturnHoldCheck2", "controlReplay.reasonOilReturnHoldCheck3"],
["single_topology", "controlReplay.reasonSingleTopology", "controlReplay.reasonSingleTopologyCopy", "controlReplay.reasonSingleTopologyCheck1", "controlReplay.reasonSingleTopologyCheck2", "controlReplay.reasonSingleTopologyCheck3"],
["demand_decreased", "controlReplay.reasonDemandDecreased", "controlReplay.reasonDemandDecreasedCopy", "controlReplay.reasonDemandDecreasedCheck1", "controlReplay.reasonDemandDecreasedCheck2", "controlReplay.reasonDemandDecreasedCheck3"],
["min_rest_active", "controlReplay.reasonMinRest", "controlReplay.reasonMinRestCopy", "controlReplay.reasonMinRestCheck1", "controlReplay.reasonMinRestCheck2", "controlReplay.reasonMinRestCheck3"],
["start_stop_rate_high", "controlReplay.reasonStartStopRate", "controlReplay.reasonStartStopRateCopy", "controlReplay.reasonStartStopRateCheck1", "controlReplay.reasonStartStopRateCheck2", "controlReplay.reasonStartStopRateCheck3"],
["sticky_protection", "controlReplay.reasonSticky", "controlReplay.reasonStickyCopy", "controlReplay.reasonStickyCheck1", "controlReplay.reasonStickyCheck2", "controlReplay.reasonStickyCheck3"],
["frost_protection", "controlReplay.reasonFrost", "controlReplay.reasonFrostCopy", "controlReplay.reasonFrostCheck1", "controlReplay.reasonFrostCheck2", "controlReplay.reasonFrostCheck3"],
["flow_preflow", "controlReplay.reasonPreflow", "controlReplay.reasonPreflowCopy", "controlReplay.reasonPreflowCheck1", "controlReplay.reasonPreflowCheck2", "controlReplay.reasonPreflowCheck3"],
["flow_postflow", "controlReplay.reasonPostflow", "controlReplay.reasonPostflowCopy", "controlReplay.reasonPostflowCheck1", "controlReplay.reasonPostflowCheck2", "controlReplay.reasonPostflowCheck3"],
["flow_too_low", "controlReplay.reasonFlowLow", "controlReplay.reasonFlowLowCopy", "controlReplay.reasonFlowLowCheck1", "controlReplay.reasonFlowLowCheck2", "controlReplay.reasonFlowLowCheck3"],
["startup_inhibit", "controlReplay.reasonStartupInhibit", "controlReplay.reasonStartupInhibitCopy", "controlReplay.reasonStartupInhibitCheck1", "controlReplay.reasonStartupInhibitCheck2", "controlReplay.reasonStartupInhibitCheck3"],
["capacity_cap", "controlReplay.reasonCapacityCap", "controlReplay.reasonCapacityCapCopy", "controlReplay.reasonCapacityCapCheck1", "controlReplay.reasonCapacityCapCheck2", "controlReplay.reasonCapacityCapCheck3"],
["falling_gap", "controlReplay.reasonFallingGap", "controlReplay.reasonFallingGapCopy", "controlReplay.reasonFallingGapCheck1", "controlReplay.reasonFallingGapCheck2", "controlReplay.reasonFallingGapCheck3"],
["projected_floor", "controlReplay.reasonProjectedFloor", "controlReplay.reasonProjectedFloorCopy", "controlReplay.reasonProjectedFloorCheck1", "controlReplay.reasonProjectedFloorCheck2", "controlReplay.reasonProjectedFloorCheck3"],
["simmer", "controlReplay.reasonSimmer", "controlReplay.reasonSimmerCopy", "controlReplay.reasonSimmerCheck1", "controlReplay.reasonSimmerCheck2", "controlReplay.reasonSimmerCheck3"],
["buffer_stop", "controlReplay.reasonBufferStop", "controlReplay.reasonBufferStopCopy", "controlReplay.reasonBufferStopCheck1", "controlReplay.reasonBufferStopCheck2", "controlReplay.reasonBufferStopCheck3"],
["dew_stop", "controlReplay.reasonDewStop", "controlReplay.reasonDewStopCopy", "controlReplay.reasonDewStopCheck1", "controlReplay.reasonDewStopCheck2", "controlReplay.reasonDewStopCheck3"],
["cooling_limiter", "controlReplay.reasonCoolingLimiter", "controlReplay.reasonCoolingLimiterCopy", "controlReplay.reasonCoolingLimiterCheck1", "controlReplay.reasonCoolingLimiterCheck2", "controlReplay.reasonCoolingLimiterCheck3"],
["sensor_fallback", "controlReplay.reasonSensorFallback", "controlReplay.reasonSensorFallbackCopy", "controlReplay.reasonSensorFallbackCheck1", "controlReplay.reasonSensorFallbackCheck2", "controlReplay.reasonSensorFallbackCheck3"],
["restart_wait", "controlReplay.reasonRestartWait", "controlReplay.reasonRestartWaitCopy", "controlReplay.reasonRestartWaitCheck1", "controlReplay.reasonRestartWaitCheck2", "controlReplay.reasonRestartWaitCheck3"],
["level1_hold", "controlReplay.reasonLevel1Hold", "controlReplay.reasonLevel1HoldCopy", "controlReplay.reasonLevel1HoldCheck1", "controlReplay.reasonLevel1HoldCheck2", "controlReplay.reasonLevel1HoldCheck3"],
["room_cap", "controlReplay.reasonRoomCap", "controlReplay.reasonRoomCapCopy", "controlReplay.reasonRoomCapCheck1", "controlReplay.reasonRoomCapCheck2", "controlReplay.reasonRoomCapCheck3"],
["oil_return_recovery", "controlReplay.reasonOilRecovery", "controlReplay.reasonOilRecoveryCopy", "controlReplay.reasonOilRecoveryCheck1", "controlReplay.reasonOilRecoveryCheck2", "controlReplay.reasonOilRecoveryCheck3"],
  ]);

  const CONTROL_WORKING_REASON_FALLBACK = Object.freeze({
    labelKey: "controlReplay.reasonFallbackLabel",
    summaryKey: "controlReplay.reasonFallbackCopy",
    checkKeys: [],
  });

  function resolveControlWorkingReasonMeta(meta) {
    if (!meta) return { label: t("controlReplay.reasonFallbackLabel"), summary: t("controlReplay.reasonFallbackCopy"), checks: [] };
    if (meta.labelKey) {
      return {
        label: t(meta.labelKey),
        summary: t(meta.summaryKey),
        checks: (meta.checkKeys || []).map((key) => t(key)),
      };
    }
    return meta;
  }

  function getControlWorkingReasonMeta(reasonCode) {
    return resolveControlWorkingReasonMeta(
      CONTROL_WORKING_REASON_METAS[reasonCode]
      || getControlReplayIncidentReasonMeta(reasonCode)
      || CONTROL_WORKING_REASON_FALLBACK,
    );
  }

  function getControlWorkingReasonLabel(reasonCode) {
    return getControlWorkingReasonMeta(reasonCode).label;
  }

  function formatControlWorkingModeCode(cm, allowZero = false) {
    const normalized = Number(cm);
    return Number.isFinite(normalized) && (normalized > 0 || (allowZero && normalized === 0)) ? `CM${normalized}` : "";
  }

  function formatControlWorkingModeTransition(fromCm, toCm) {
    const fromLabel = formatControlWorkingModeCode(fromCm);
    const toLabel = formatControlWorkingModeCode(toCm, true);
    return fromLabel && toLabel && fromLabel !== toLabel ? `${fromLabel} → ${toLabel}` : "";
  }

  function deriveControlWorkingModeTransition(event, previousCm) {
    const eventType = String(event?.event_type || "");
    const cm = Number(event?.cm) || 0;
    const valueA = Number(event?.value_a);
    if (eventType === "boiler_assist_start") {
      return formatControlWorkingModeTransition(previousCm || 2, cm === 3 ? 3 : cm);
    }
    if (eventType === "boiler_assist_stop") {
      return formatControlWorkingModeTransition(previousCm === 3 ? 3 : previousCm, cm > 0 ? cm : 2);
    }
    if (eventType === "flow_hold_start" && cm === 1) {
      return formatControlWorkingModeTransition(previousCm, 1);
    }
    if (eventType === "flow_hold_clear" && cm === 1 && Number.isFinite(valueA)) {
      return formatControlWorkingModeTransition(1, valueA);
    }
    const incidentTransition = getControlReplayIncidentModeTransition(event, previousCm);
    if (incidentTransition) {
      return formatControlWorkingModeTransition(incidentTransition.from, incidentTransition.to);
    }
    return "";
  }

  function getControlWorkingModeAfterEvent(event) {
    const eventType = String(event?.event_type || "");
    const cm = Number(event?.cm) || 0;
    const valueA = Number(event?.value_a);
    if (eventType === "flow_hold_clear" && cm === 1 && Number.isFinite(valueA)) {
      return valueA;
    }
    if (eventType === "frost_protection_clear") {
      return 0;
    }
    const incidentMode = getControlReplayIncidentModeAfterEvent(event);
    if (incidentMode !== null) {
      return incidentMode;
    }
    return cm;
  }

  function getControlWorkingModeMetaLabel(item) {
    const transitionLabel = String(item?.modeTransitionLabel || "").trim();
    if (transitionLabel) {
      return transitionLabel;
    }
    const modeLabel = String(item?.modeLabel || "").trim();
    return modeLabel.includes("→") ? modeLabel : "";
  }

  function getControlWorkingCoolingContext() {
    const reasonCode = normalizeControlWorkingCoolingReason(getEntityStateText("coolingLimiterReasonCode", ""));
    return {
      requestActive: isEntityActive("coolingRequestActive"),
      permitted: hasEntity("coolingPermitted") ? isEntityActive("coolingPermitted") : true,
      reasonCode: reasonCode || "inactive",
      rawDemand: formatControlReplayNumber("coolingDemandRaw", 0, "", "—"),
      limitedDemand: formatControlReplayNumber("coolingLimitedDemand", 0, "", "—"),
      allowedMax: formatControlReplayNumber("coolingLimiterAllowedMax", 0, "", "—"),
      dewPoint: formatControlReplayNumber("coolingDewPointSelected", 1, "°C", "—"),
      safeSupply: formatControlReplayNumber("coolingEffectiveMinSupplyTemp", 1, "°C", "—"),
      guardMode: getEntityStateText("coolingGuardMode", "Dauwpuntbewaking"),
      blockReason: getEntityStateText("coolingBlockReason", "Ready"),
    };
  }

  function getControlWorkingKindLabel(kind) {
    const labels = {
      event: t("controlReplay.kindEvent"),
      span: t("controlReplay.kindSpan"),
      aggregate: t("controlReplay.kindAggregate"),
    };
    return labels[kind] || t("controlReplay.kindRecord");
  }

  function renderControlWorkingPill(label, tone = "neutral", icon = "") {
    const iconMarkup = icon ? renderOqIcon(icon, "oq-working-pill-icon") : "";
    return `<span class="oq-working-pill oq-working-pill--${escapeHtml(tone)}">${iconMarkup}<span>${escapeHtml(label)}</span></span>`;
  }

  function shouldShowControlWorkingModeBadge(item) {
    const reasonCode = item?.reasonCode || item?.primaryReason;
    return normalizeControlReplayModeId(item?.modeLabel) === "cm98" && reasonCode === "frost_protection";
  }

  function renderControlWorkingModeBadge(item) {
    if (!shouldShowControlWorkingModeBadge(item)) {
      return "";
    }
    return `<span class="oq-working-mode-badge" aria-label="${t("controlReplay.modeBadgeCm98")}">CM98</span>`;
  }

  function getControlWorkingOptimizerModel(target) {
    const reasonCode = target?.reasonCode || target?.primaryReason || "keep_current";
    const source = target?.source || "HP1 + HP2";
    if (reasonCode === "better_heat") {
      return {
        title: t("controlReplay.optSystemChoice"),
        verdict: t("controlReplay.optTwoHpActive"),
        summary: t("controlReplay.optTwoHpCopy"),
        rows: [
          { option: t("controlReplay.optOneHp"), result: t("controlReplay.optTooLittleReserve"), code: "better_heat", detail: t("controlReplay.optDemandHigh"), tone: "muted" },
          { option: t("controlReplay.optOtherPump"), result: t("controlReplay.optNoBenefit"), code: "hold_active", detail: t("controlReplay.optSwitchNoBenefit"), tone: "muted" },
          { option: t("controlReplay.optTwoHp"), result: t("controlReplay.optChosen"), code: "better_heat", detail: t("controlReplay.optTogetherReserve"), tone: "selected" },
        ],
      };
    }
    if (reasonCode === "demand_decreased" || reasonCode === "less_power") {
      return {
        title: t("controlReplay.optSystemChoice"),
        verdict: t("controlReplay.optOneHpEnough"),
        summary: t("controlReplay.optOneHpEnoughCopy"),
        rows: [
          { option: t("controlReplay.optTwoHp"), result: t("controlReplay.optNoLongerNeeded"), code: "less_power", detail: t("controlReplay.optTogetherTooMuch"), tone: "muted" },
          { option: source, result: t("controlReplay.optStaysActive"), code: "less_power", detail: t("controlReplay.optOneHpCovers"), tone: "selected" },
        ],
      };
    }
    if (reasonCode === "runtime_lead") {
      return {
        title: t("controlReplay.optSystemChoice"),
        verdict: t("controlReplay.optStarted", { source }),
        summary: t("controlReplay.optHpEqual"),
        rows: [
          { option: "HP1", result: source === "HP1" ? t("controlReplay.optChosen") : t("controlReplay.optNotNow"), code: "runtime_lead", detail: t("controlReplay.optFitsBalance"), tone: source === "HP1" ? "selected" : "muted" },
          { option: "HP2", result: source === "HP2" ? t("controlReplay.optChosen") : t("controlReplay.optNotNow"), code: "runtime_lead", detail: t("controlReplay.optEqualLessGood"), tone: source === "HP2" ? "selected" : "muted" },
        ],
      };
    }
    if (["min_rest_active", "no_candidate", "candidate_in_rest", "candidate_in_defrost", "candidate_unavailable"].includes(reasonCode)) {
      return {
        title: t("controlReplay.optStartCheck"),
        verdict: t("controlReplay.optStartDelayed"),
        summary: getControlWorkingReasonMeta(reasonCode).summary,
        rows: [
          { option: source, result: t("controlReplay.optWaitStill"), code: reasonCode, detail: getControlWorkingReasonMeta(reasonCode).summary, tone: "limited" },
          { option: t("controlReplay.optReassess"), result: t("controlReplay.optLater"), code: "hold_active", detail: t("controlReplay.optReassessCopy"), tone: "muted" },
        ],
      };
    }
    if (["flow_preflow", "flow_postflow", "flow_too_low"].includes(reasonCode)) {
      const eventType = target?.realEventType || target?.rawDecisionEvent?.event_type || "";
      const flowCleared = eventType === "flow_hold_clear";
      const postflow = reasonCode === "flow_postflow";
      if (flowCleared) {
        return {
          title: postflow ? t("controlReplay.optFlowDoneTitle") : t("controlReplay.optFlowConfirmedTitle"),
          verdict: postflow ? t("controlReplay.optFlowDoneVerdict") : t("controlReplay.optFlowGoVerdict"),
          summary: postflow
            ? t("controlReplay.optFlowDoneCopy")
            : t("controlReplay.optFlowGoCopy"),
          rows: [
            { option: t("controlReplay.optFlow"), result: t("controlReplay.optFlowEnough"), code: reasonCode, detail: t("controlReplay.optFlowReleased"), tone: "selected" },
            { option: t("controlReplay.optHeatpump"), result: postflow ? t("controlReplay.optStopped") : t("controlReplay.optReleased"), code: reasonCode, detail: postflow ? t("controlReplay.optStoppedPostflow") : t("controlReplay.optCompressorMayStart"), tone: "selected" },
            { option: t("controlReplay.optController"), result: t("controlReplay.optContinues"), code: "keep_current", detail: t("controlReplay.optControllerAuto"), tone: "muted" },
          ],
        };
      }
      const lowFlowFault = reasonCode === "flow_too_low";
      return {
        title: t("controlReplay.optFlowFirst"),
        verdict: postflow ? t("controlReplay.optFlowPostActive") : lowFlowFault ? t("controlReplay.optFlowBlocked") : t("controlReplay.optFlowPreActive"),
        summary: getControlWorkingReasonMeta(reasonCode).summary,
        rows: [
          { option: t("controlReplay.optFlow"), result: lowFlowFault ? t("controlReplay.optStaysLow") : postflow ? t("controlReplay.optRoundingOff") : t("controlReplay.optBuildingUp"), code: reasonCode, detail: t("controlReplay.optPumpCirculates"), tone: lowFlowFault ? "limited" : "selected" },
          { option: t("controlReplay.optHeatpump"), result: postflow ? t("controlReplay.optStopped") : lowFlowFault ? t("controlReplay.optFlowBlocked") : t("controlReplay.optWaitPreflow"), code: reasonCode, detail: t("controlReplay.optCompressorWaits"), tone: lowFlowFault ? "limited" : "muted" },
          { option: t("controlReplay.optController"), result: lowFlowFault ? t("controlReplay.optKeepsChecking") : t("controlReplay.optChecksAuto"), code: "keep_current", detail: t("controlReplay.optFlowAuto"), tone: "muted" },
        ],
      };
    }
    if (reasonCode === "defrost_hold" || reasonCode === "defrost_boost") {
      return {
        title: t("controlReplay.optProtection"),
        verdict: t("controlReplay.optDefrostPriority"),
        summary: t("controlReplay.optDefrostCalm"),
        rows: [
          { option: t("controlReplay.optActiveHp"), result: t("controlReplay.optRecoverCalmly"), code: "defrost_hold", detail: t("controlReplay.optNoSwitch"), tone: "selected" },
          { option: t("controlReplay.optExtraSource"), result: reasonCode === "defrost_boost" ? t("controlReplay.optHelps") : t("controlReplay.optStandby"), code: reasonCode, detail: t("controlReplay.optDeployIfNeeded"), tone: reasonCode === "defrost_boost" ? "selected" : "muted" },
        ],
      };
    }
    if (reasonCode === "boiler_assist") {
      return {
        title: t("controlReplay.optSourceChoice"),
        verdict: t("controlReplay.optBoilerSupports"),
        summary: t("controlReplay.optBoilerBase"),
        rows: [
          { option: t("controlReplay.optOnlyHps"), result: t("controlReplay.optTooLittleReserve"), code: "better_heat", detail: t("controlReplay.optDemandHigh"), tone: "muted" },
          { option: t("controlReplay.optBoiler"), result: t("controlReplay.optTempExtra"), code: "boiler_assist", detail: t("controlReplay.optBoilerExtra"), tone: "selected" },
          { option: t("controlReplay.optAfterPeak"), result: t("controlReplay.optBackToHp"), code: "less_power", detail: t("controlReplay.optHpTakeOver"), tone: "muted" },
        ],
      };
    }
    if (reasonCode === "sticky_protection") {
      return {
        title: t("controlReplay.optPumpProtection"),
        verdict: t("controlReplay.optBriefRun"),
        summary: t("controlReplay.optPumpOnly"),
        rows: [
          { option: t("controlReplay.optHeating"), result: t("controlReplay.optNotNeeded"), code: "keep_current", detail: t("controlReplay.optNoHeatDemand"), tone: "muted" },
          { option: t("controlReplay.optCooling"), result: t("controlReplay.optNotNeeded"), code: "keep_current", detail: t("controlReplay.optNoCoolDemand"), tone: "muted" },
          { option: t("controlReplay.optPump"), result: t("controlReplay.optBriefOn"), code: "sticky_protection", detail: t("controlReplay.optDailyPump"), tone: "selected" },
        ],
      };
    }
    if (["capacity_cap", "room_cap", "cooling_limiter"].includes(reasonCode)) {
      const cooling = getControlWorkingCoolingContext();
      return {
        title: t("controlReplay.optCoolControl"),
        verdict: t("controlReplay.optMaxLevel", { max: cooling.allowedMax }),
        summary: t("controlReplay.optCoolNormal"),
        rows: [
          { option: t("controlReplay.optAskedLevel"), result: cooling.rawDemand, code: "coolingDemandRaw", detail: t("controlReplay.optAskedBeforeMax"), tone: "muted" },
          { option: t("controlReplay.optSetMax"), result: cooling.allowedMax, code: reasonCode, detail: t("controlReplay.optHighestAllowed"), tone: "selected" },
          { option: t("controlReplay.optSentLevel"), result: cooling.limitedDemand, code: "coolingLimitedDemand", detail: t("controlReplay.optLevelNow"), tone: "normal" },
        ],
      };
    }
    if (reasonCode === "buffer_stop") {
      return {
        title: t("controlReplay.optCoolControl"),
        verdict: t("controlReplay.optWaterCold"),
        summary: t("controlReplay.optWaterNoCool"),
        rows: [
          { option: t("controlReplay.optCoolDemand"), result: t("controlReplay.optStaysActive"), code: "coolingDemandRaw", detail: t("controlReplay.optRoomKeepsAsking"), tone: "muted" },
          { option: t("controlReplay.optWaterTemp"), result: t("controlReplay.optColdEnough"), code: "buffer_stop", detail: t("controlReplay.optSupplyCold"), tone: "selected" },
          { option: t("controlReplay.optHeatpump"), result: t("controlReplay.optWait"), code: "keep_current", detail: t("controlReplay.optHpAutoCool"), tone: "muted" },
        ],
      };
    }
    if (["falling_gap", "projected_floor", "dew_stop", "restart_wait", "level1_hold", "oil_return_recovery", "sensor_fallback"].includes(reasonCode)) {
      const cooling = getControlWorkingCoolingContext();
      return {
        title: t("controlReplay.optCoolMonitor"),
        verdict: cooling.permitted ? t("controlReplay.optMaxCoolLevel", { max: cooling.allowedMax }) : t("controlReplay.optCoolPaused"),
        summary: t("controlReplay.optCoolCareful"),
        rows: [
          { option: t("controlReplay.optAskedLevel"), result: cooling.rawDemand, code: "coolingDemandRaw", detail: t("controlReplay.optAskedBeforeMonitor"), tone: "muted" },
          { option: t("controlReplay.optMaxSafe"), result: cooling.allowedMax, code: reasonCode, detail: t("controlReplay.optHighestSafe"), tone: "selected" },
          { option: t("controlReplay.optSentLevel"), result: cooling.limitedDemand, code: "coolingLimitedDemand", detail: t("controlReplay.optLevelNow"), tone: "limited" },
        ],
      };
    }
    return null;
  }

  function renderControlWorkingOptimizer(model) {
    if (!model) {
      return "";
    }
    return `
      <div class="oq-working-optimizer">
        <div class="oq-working-optimizer-head">
          <span class="oq-working-eyebrow">${escapeHtml(model.title)}</span>
          <strong>${escapeHtml(model.verdict)}</strong>
          <p>${escapeHtml(model.summary)}</p>
        </div>
        <div class="oq-working-optimizer-options">
          ${model.rows.map((row) => `
            <div class="oq-working-optimizer-option oq-working-optimizer-option--${escapeHtml(row.tone || "muted")}">
              <span>${escapeHtml(row.option)}</span>
              <strong>${escapeHtml(row.result)}</strong>
              <p>${escapeHtml(row.detail)}</p>
            </div>
          `).join("")}
        </div>
      </div>
    `;
  }

  function getControlWorkingActiveStartupInhibit(nowMs = Date.now()) {
    const events = getDecisionLogEvents()
      .filter((event) => ["startup_inhibit_start", "startup_inhibit_refresh", "startup_inhibit_clear"].includes(String(event?.event_type || "")))
      .sort(compareDecisionEvents);
    const latest = events[events.length - 1];
    if (!latest || !["startup_inhibit_start", "startup_inhibit_refresh"].includes(String(latest.event_type))) {
      return null;
    }
    const startedEpochMs = getDecisionEventEpochMs(latest);
    const initialRemainingS = Math.max(0, Number(latest?.value_b) || 0);
    const elapsedS = Number.isFinite(startedEpochMs) ? Math.max(0, (nowMs - startedEpochMs) / 1000) : 0;
    const remainingS = Math.max(0, Math.ceil(initialRemainingS - elapsedS));
    if (initialRemainingS > 0 && remainingS <= 0) {
      return null;
    }
    return {
      event: latest,
      subject: String(latest?.subject || "SYSTEM").toUpperCase(),
      targetMode: Number(latest?.value_a) || 0,
      remainingS,
      remainingLabel: remainingS > 0 ? t("controlReplay.curWaitRemaining", { minutes: formatNumber(Math.max(1, Math.ceil(remainingS / 60)), { maximumFractionDigits: 0 }) }) : t("controlReplay.curWaitActive"),
    };
  }

  function getControlWorkingCurrent(heatPumpPanels) {
    const modeModel = getControlReplayModeModel(heatPumpPanels);
    const rawControlModeLabel = getEntityStateText("controlModeLabel", "—");
    const currentModeId = normalizeControlReplayModeId(rawControlModeLabel);
    const currentModeLabel = currentModeId ? currentModeId.toUpperCase() : rawControlModeLabel;
    const hp1Panel = heatPumpPanels.find((panel) => panel.title === "HP1") || heatPumpPanels[0];
    const hp2Panel = heatPumpPanels.find((panel) => panel.title === "HP2");
    const hp1Running = isControlReplayHpRunning(hp1Panel);
    const hp2Running = hp2Panel ? isControlReplayHpRunning(hp2Panel) : false;
    const duoActive = hp1Running && hp2Running;
    const defrostActive = modeModel.defrostActive;
    const coolingContext = getControlWorkingCoolingContext();
    const coolingProtection = modeModel.coolingProtection;
    const coolingCapped = modeModel.coolingCapped;
    const coolingActive = modeModel.coolingMode || modeModel.coolingRequest;
    const stickyActive = hasEntity("stickyActive") && isEntityActive("stickyActive");
    const boilerActive = modeModel.boilerActive;
    const startupInhibit = getControlWorkingActiveStartupInhibit();
    let title = t("controlReplay.curOneHp");
    let copy = t("controlReplay.curOneHpCopy");
    let expectation = t("controlReplay.curOneHpExpect");
    let severity = "normal";
    let primaryReason = "keep_current";
    let sinceLabel = t("controlReplay.curLive");

    if (currentModeId === "cm98") {
      title = t("controlReplay.curFrostTitle");
      copy = t("controlReplay.curFrostCopy");
      expectation = t("controlReplay.curFrostExpect");
      severity = "limited";
      primaryReason = "frost_protection";
      sinceLabel = t("controlReplay.curFrostSince");
    } else if (stickyActive) {
      title = t("controlReplay.curStickyTitle");
      copy = t("controlReplay.curStickyCopy");
      expectation = t("controlReplay.curStickyExpect");
      primaryReason = "sticky_protection";
      sinceLabel = t("controlReplay.curStickySince");
    } else if (startupInhibit) {
      const coolingWait = startupInhibit.targetMode === 1;
      title = coolingWait ? t("controlReplay.curCoolWaitTitle") : t("controlReplay.curHeatWaitTitle");
      copy = coolingWait
        ? t("controlReplay.curCoolWaitCopy")
        : t("controlReplay.curHeatWaitCopy");
      expectation = coolingWait
        ? t("controlReplay.curCoolWaitExpect")
        : t("controlReplay.curHeatWaitExpect");
      primaryReason = "startup_inhibit";
      sinceLabel = startupInhibit.remainingLabel || t("controlReplay.curWaitActive");
    } else if (coolingContext.reasonCode === "buffer_stop") {
      title = t("controlReplay.curBufferTitle");
      copy = t("controlReplay.curBufferCopy");
      expectation = t("controlReplay.curBufferExpect");
      primaryReason = "buffer_stop";
      sinceLabel = t("controlReplay.curCoolDemandSince");
    } else if (coolingProtection) {
      const limiterReason = coolingContext.reasonCode && coolingContext.reasonCode !== "inactive" ? coolingContext.reasonCode : "soft_guard";
      const waitingForRestart = limiterReason === "restart_wait";
      title = waitingForRestart
        ? t("controlReplay.curRestartWaitTitle")
        : coolingContext.permitted ? t("controlReplay.curLimitedTitle") : t("controlReplay.curPausedTitle");
      copy = waitingForRestart
        ? t("controlReplay.curRestartWaitCopy")
        : t("controlReplay.curCappedCopy", { max: coolingContext.allowedMax });
      expectation = waitingForRestart
        ? t("controlReplay.curRestartExpect")
        : t("controlReplay.curRampExpect");
      severity = "limited";
      primaryReason = limiterReason;
      sinceLabel = t("controlReplay.curCoolDemandSince");
    } else if (coolingCapped) {
      const coolingMaxLabel = coolingContext.allowedMax && coolingContext.allowedMax !== "—"
        ? t("controlReplay.curCappedLevel", { max: coolingContext.allowedMax })
        : t("controlReplay.curCappedMax");
      const cappedReason = ["capacity_cap", "room_cap", "cooling_limiter"].includes(coolingContext.reasonCode)
        ? coolingContext.reasonCode
        : "capacity_cap";
      title = t("controlReplay.curCappedTitle");
      copy = t("controlReplay.curCappedCopy", { level: coolingMaxLabel });
      expectation = t("controlReplay.curCappedExpect");
      primaryReason = cappedReason;
      sinceLabel = t("controlReplay.curCoolDemandSince");
    } else if (coolingActive) {
      title = t("controlReplay.curCoolingTitle");
      copy = t("controlReplay.curCoolingCopy");
      expectation = t("controlReplay.curCoolingExpect");
      primaryReason = "keep_current";
      sinceLabel = t("controlReplay.curCoolingSince");
    } else if (currentModeId === "cm4") {
      title = boilerActive ? t("controlReplay.curFallbackOnTitle") : t("controlReplay.curFallbackOffTitle");
      copy = boilerActive
        ? t("controlReplay.curFallbackOnCopy")
        : t("controlReplay.curFallbackOffCopy");
      expectation = boilerActive
        ? t("controlReplay.curFallbackOnExpect")
        : t("controlReplay.curFallbackOffExpect");
      severity = "fault";
      primaryReason = boilerActive ? "boiler_fallback" : "fallback_blocked";
      sinceLabel = boilerActive ? t("controlReplay.curFallbackOnSince") : t("controlReplay.curFallbackOffSince");
    } else if (boilerActive) {
      title = t("controlReplay.curBoilerTitle");
      copy = t("controlReplay.curBoilerCopy");
      expectation = t("controlReplay.curBoilerExpect");
      severity = "limited";
      primaryReason = "boiler_assist";
      sinceLabel = t("controlReplay.curBoilerSince");
    } else if (defrostActive) {
      title = t("controlReplay.curDefrostTitle");
      copy = t("controlReplay.curDefrostCopy");
      expectation = t("controlReplay.curDefrostExpect");
      severity = "limited";
      primaryReason = "defrost_hold";
      sinceLabel = t("controlReplay.curDefrostSince");
    } else if (duoActive) {
      title = t("controlReplay.curDuoTitle");
      copy = t("controlReplay.curDuoCopy");
      expectation = t("controlReplay.curDuoExpect");
      primaryReason = "better_heat";
      sinceLabel = t("controlReplay.curDuoSince");
    } else if (!hp1Running && !hp2Running) {
      title = t("controlReplay.curNoneTitle");
      copy = t("controlReplay.curNoneCopy");
      expectation = t("controlReplay.curNoneExpect");
      primaryReason = "keep_current";
      sinceLabel = t("controlReplay.curNoneSince");
    }

    const hp1Waiting = startupInhibit && ["HP1", "BOTH"].includes(startupInhibit.subject);
    const hp2Waiting = startupInhibit && ["HP2", "BOTH"].includes(startupInhibit.subject);
    return {
      title,
      copy,
      expectation,
      severity,
      primaryReason,
      sinceLabel,
      modeLabel: currentModeLabel,
      strategyLabel: formatControlReplayStrategyLabel(),
      reasonLabel: getControlWorkingReasonLabel(primaryReason),
      hp1Running,
      hp2Running,
      hp2Available: Boolean(hp2Panel),
      hp1Status: hp1Running ? t("controlReplay.curHpActive") : hp1Waiting ? t("controlReplay.curHpWait") : t("controlReplay.curHpAvailable"),
      hp2Status: hp2Panel ? (hp2Running ? t("controlReplay.curHpActive") : hp2Waiting ? t("controlReplay.curHpWait") : t("controlReplay.curHpAvailable")) : t("controlReplay.curHpMissing"),
      cvStatus: boilerActive ? (currentModeId === "cm4" ? t("controlReplay.curCvFallback") : t("controlReplay.curCvActive")) : t("controlReplay.curCvOff"),
      outsideTemp: formatControlReplayNumber("outsideTempSelected", 1, "°C", "—"),
      supplyTemp: formatControlReplayNumber("supplyTemp", 1, "°C", "—"),
      flow: formatControlReplayNumber("flowSelected", 0, "L/h", "—"),
      hp1Starts: getControlReplayCounterValue("hp1CompressorStarts24h", "—"),
      hp2Starts: getControlReplayCounterValue("hp2CompressorStarts24h", hp2Panel ? "—" : t("controlReplay.cardNotApplicable")),
      hp1Hours: formatControlReplayRuntimeHours("hp1RuntimeHours", "—"),
      hp2Hours: hp2Panel ? formatControlReplayRuntimeHours("hp2RuntimeHours", "—") : t("controlReplay.curNa"),
      cooling: coolingContext,
      coolingProtection,
      startupInhibit,
      coolingCapped,
    };
  }

  function getDecisionLogEvents() {
    const payload = state.decisionLog;
    return payload?.ok && Array.isArray(payload.events) ? payload.events : [];
  }

  function getDecisionEventEpochMs(event) {
    const epochS = Number(event?.epoch_s);
    if (Number.isFinite(epochS) && epochS > 0) {
      return epochS * 1000;
    }
    const bootEpochS = Number(state.decisionLog?.meta?.boot_epoch_s);
    const uptimeS = Number(event?.uptime_s);
    if (Number.isFinite(bootEpochS) && bootEpochS > 0 && Number.isFinite(uptimeS) && uptimeS >= 0) {
      return (bootEpochS + uptimeS) * 1000;
    }
    return Number.NaN;
  }

  function getDecisionEventSortValue(event) {
    const epochMs = getDecisionEventEpochMs(event);
    if (Number.isFinite(epochMs)) {
      return epochMs / 1000;
    }
    const uptimeS = Number(event?.uptime_s);
    if (Number.isFinite(uptimeS)) {
      return uptimeS;
    }
    return Number(event?.seq) || 0;
  }

  function compareDecisionEvents(left, right) {
    const timeDifference = getDecisionEventSortValue(left) - getDecisionEventSortValue(right);
    if (timeDifference !== 0) {
      return timeDifference;
    }
    return (Number(left?.seq) || 0) - (Number(right?.seq) || 0);
  }

  function getDecisionEventAgeMinutes(event, nowMs = Date.now()) {
    const epochMs = getDecisionEventEpochMs(event);
    if (Number.isFinite(epochMs)) {
      return Math.max(0, Math.round((nowMs - epochMs) / 60000));
    }
    const payloadUptimeS = Number(state.decisionLog?.meta?.uptime_s);
    const eventUptimeS = Number(event?.uptime_s);
    if (Number.isFinite(payloadUptimeS) && Number.isFinite(eventUptimeS)) {
      return Math.max(0, Math.round((payloadUptimeS - eventUptimeS) / 60));
    }
    return Number.NaN;
  }

  function isControlWorkingSameLocalDay(left, right) {
    return left.getFullYear() === right.getFullYear()
      && left.getMonth() === right.getMonth()
      && left.getDate() === right.getDate();
  }

  function formatControlWorkingAbsoluteTimeLabel(epochMs, nowMs = Date.now(), mode = "auto") {
    if (!Number.isFinite(epochMs)) {
      return t("overview.statusUnknown");
    }
    const date = new Date(epochMs);
    const time = formatTime(date, { hour: "2-digit", minute: "2-digit" });
    if (mode === "time") {
      return time;
    }
    if (mode === "weekday") {
      const day = formatDate(date, { weekday: "short" }).replace(".", "");
      return `${day} ${time}`;
    }
    const today = new Date(nowMs);
    const yesterday = new Date(today);
    yesterday.setDate(yesterday.getDate() - 1);
    if (isControlWorkingSameLocalDay(date, today)) {
      return time;
    }
    if (isControlWorkingSameLocalDay(date, yesterday)) {
      return t("controlReplay.yesterdayPrefix", { time });
    }
    const day = formatDate(date, { weekday: "short" }).replace(".", "");
    return `${day} ${time}`;
  }

  function getControlWorkingWindowEpochForMinute(minute, selectedWindow = getControlWorkingSelectedWindow(), nowMs = Date.now()) {
    const normalized = Math.max(0, Math.min(1440, Number(minute) || 0));
    const bounds = getControlWorkingWindowBounds(selectedWindow, nowMs);
    return bounds.start + ((normalized / 1440) * (bounds.end - bounds.start));
  }

  function getDecisionEventWindowMinute(event, selectedWindow = getControlWorkingSelectedWindow(), nowMs = Date.now()) {
    const epochMs = getDecisionEventEpochMs(event);
    const minuteInWindow = (value, start, end) => {
      if (!Number.isFinite(value) || value < start || value > end) {
        return Number.NaN;
      }
      return ((value - start) / Math.max(1, end - start)) * 1440;
    };

    if (Number.isFinite(epochMs)) {
      const bounds = getControlWorkingWindowBounds(selectedWindow, nowMs);
      return minuteInWindow(epochMs, bounds.start, bounds.end);
    }

    const ageMinutes = getDecisionEventAgeMinutes(event, nowMs);
    if (!Number.isFinite(ageMinutes)) {
      return Number.NaN;
    }
    const option = getControlWorkingWindowOptions().find((candidate) => candidate.id === selectedWindow);
    if (option?.calendarDay || option?.custom) {
      return Number.NaN;
    }
    const durationMinutes = getControlWorkingWindowDurationMinutes(selectedWindow, nowMs);
    return ageMinutes <= durationMinutes
      ? 1440 - ((ageMinutes / durationMinutes) * 1440)
      : Number.NaN;
  }

  function formatDecisionLogTimeLabel(event, selectedWindow = getControlWorkingSelectedWindow(), nowMs = Date.now()) {
    const epochMs = getDecisionEventEpochMs(event);
    if (!Number.isFinite(epochMs)) {
      const ageMinutes = getDecisionEventAgeMinutes(event, nowMs);
      return Number.isFinite(ageMinutes) ? formatControlWorkingRelativeOffset(ageMinutes) : t("controlReplay.timeUnknown");
    }
    if (selectedWindow === "week" || selectedWindow === "last48" || selectedWindow === "last3d" || selectedWindow === "custom") {
      return formatControlWorkingAbsoluteTimeLabel(epochMs, nowMs, "weekday");
    }
    if (selectedWindow.startsWith("last")) {
      return formatControlWorkingAbsoluteTimeLabel(epochMs, nowMs, "auto");
    }
    return formatControlWorkingAbsoluteTimeLabel(epochMs, nowMs, "time");
  }

  function formatDecisionDuration(seconds) {
    const normalized = Math.max(0, Math.round(Number(seconds) || 0));
    if (!normalized) {
      return "";
    }
    if (normalized < 60) {
      return t("controlReplay.durationSeconds", { value: formatNumber(normalized, { maximumFractionDigits: 0 }) });
    }
    if (normalized < 3600) {
      return t("controlReplay.durationMinutes", { value: formatNumber(Math.round(normalized / 60), { maximumFractionDigits: 0 }) });
    }
    const hours = Math.floor(normalized / 3600);
    const minutes = Math.round((normalized % 3600) / 60);
    return minutes
      ? t("controlReplay.durationHoursMinutes", { hours: formatNumber(hours, { maximumFractionDigits: 0 }), minutes: formatNumber(minutes, { maximumFractionDigits: 0 }) })
      : t("controlReplay.durationHours", { hours: formatNumber(hours, { maximumFractionDigits: 0 }) });
  }

  function getDecisionSubjectLabel(subject) {
    const normalized = String(subject || "").toUpperCase();
    const labels = {
      SYSTEM: t("controlReplay.subjSystem"),
      HP1: "HP1",
      HP2: "HP2",
      BOTH: "HP1 + HP2",
      CV: t("controlReplay.subjBoiler"),
      COOLING: t("controlReplay.subjCooling"),
      PUMP: t("controlReplay.subjPump"),
      CONTROLLER: t("controlReplay.subjController"),
    };
    return labels[normalized] || t("controlReplay.subjSystem");
  }

  function getDecisionModeSubjectLabel(subject, contextCm) {
    const normalized = String(subject || "").toUpperCase();
    const subjectLabel = getDecisionSubjectLabel(subject);
    if (normalized !== "HP1" && normalized !== "HP2" && normalized !== "BOTH") {
      return subjectLabel;
    }
    if (Number(contextCm) === 5) {
      return t("controlReplay.subjCoolSuffix", { label: subjectLabel });
    }
    if (Number(contextCm) > 0) {
      return t("controlReplay.subjHeatSuffix", { label: subjectLabel });
    }
    return subjectLabel;
  }

  function getDecisionCoolingSourceLabel(event) {
    const coolingSubject = String(event?._oq_active_cooling_subject || "").toUpperCase();
    if (coolingSubject === "HP1" || coolingSubject === "HP2" || coolingSubject === "BOTH") {
      return getDecisionModeSubjectLabel(coolingSubject, 5);
    }
    return getDecisionModeSubjectLabel(event?.subject, 5);
  }

  function getControlWorkingSingleTopologySource(event) {
    const subject = String(event?.subject || "").toUpperCase();
    return subject === "HP1" || subject === "HP2" ? subject : "";
  }

  function getDecisionEventCopy(event) {
    const eventType = String(event?.event_type || "");
    const subject = getDecisionSubjectLabel(event?.subject);
    const reasonCode = String(event?.reason || "unknown");
    const isCoolingModeEvent = Number(event?._oq_context_cm ?? event?.cm) === 5;
    const activeCoolingSource = event?._oq_active_cooling_source || t("controlReplay.subjHp");
    const activeHeatingSource = event?._oq_active_heating_source || t("controlReplay.subjHp");
    const coolingStopReason = String(event?._oq_cooling_stop_reason || (reasonCode === "dew_stop" ? "dew_stop" : ""));
    const coolingDemandEnded = ["less_power", "demand_decreased", "cooling_request_cleared"].includes(reasonCode);
    const heatingDemandEnded = reasonCode === "heating_request_cleared";
    const coolingRuntimeHold = Boolean(event?._oq_cooling_runtime_hold);
    const heatingRuntimeHold = Boolean(event?._oq_heating_runtime_hold);
    const coolingProtectionReason = isControlWorkingCoolingProtectionReason(reasonCode);
    const boilerStopBlocked = ["soft_guard", "sensor_fallback", "no_candidate", "flow_preflow"].includes(reasonCode);
    const reason = getControlWorkingReasonMeta(reasonCode);
    const isFlowPreStart = reasonCode === "flow_preflow";
    const isFlowFault = reasonCode === "flow_too_low";
    const incidentCopy = getControlReplayIncidentEventCopy(event, subject);
    if (incidentCopy) {
      return incidentCopy;
    }
    if (reasonCode === "frequency_cap_below_minimum") {
      const silent = (Number(event?.flags) & 1) !== 0;
      return {
        title: t("controlReplay.freqCantStart", { subject }),
        summary: describeFrequencyLimit(Number(event.value_a), {
          unit: event.subject === "HP2" ? "hp2" : "hp1",
          mode: Number(event.cm) === 5 ? "cooling" : "heating",
          minimum: Number(event.value_b),
        }),
        detail: silent ? t("controlReplay.freqDetailSilent") : t("controlReplay.freqDetailDay"),
        next: silent ? t("controlReplay.freqNextSilent") : t("controlReplay.freqNextDay"),
      };
    }
    const fallback = {
      title: t("controlReplay.evFallbackTitle"),
      summary: t("controlReplay.evFallbackCopy"),
      detail: reason.summary,
      next: t("controlReplay.evFallbackNext"),
    };
    switch (eventType) {
      case "source_start":
        return {
        title: isCoolingModeEvent ? t("controlReplay.evCoolStarted", { subject }) : t("controlReplay.evHpStarted", { subject }),
        reasonLabel: isCoolingModeEvent ? t("controlReplay.evCoolStartedLabel") : "",
        reasonSummary: isCoolingModeEvent ? t("controlReplay.evCoolStartedCopy") : "",
        summary: isCoolingModeEvent
          ? t("controlReplay.evCoolStartCopy", { subject })
          : t("controlReplay.evHpChosenCopy", { subject }),
        detail: isCoolingModeEvent
          ? t("controlReplay.evCoolReleasedCopy")
          : t("controlReplay.evHpEqualCopy"),
        next: isCoolingModeEvent
          ? t("controlReplay.evCoolStaysActive")
          : t("controlReplay.evHighDemand"),
      };
      case "source_stop":
        return {
        title: isCoolingModeEvent
          ? coolingStopReason === "dew_stop"
            ? t("controlReplay.evCoolDewTitle", { subject })
            : coolingDemandEnded
            ? t("controlReplay.evCoolNoDemandTitle")
            : t("controlReplay.evCoolDoneTitle", { subject })
          : heatingDemandEnded
          ? t("controlReplay.evHeatStoppedTitle")
          : reasonCode === "less_power"
          ? t("controlReplay.evOneHpStops")
          : t("controlReplay.evHpStoppedTitle", { subject }),
        reasonLabel: isCoolingModeEvent
          ? coolingStopReason === "dew_stop"
            ? t("controlReplay.evDewStopLabel")
            : coolingDemandEnded
            ? t("controlReplay.evNoCoolLabel")
            : t("controlReplay.evCoolDoneLabel")
          : heatingDemandEnded
          ? t("controlReplay.evNoHeatLabel")
          : reasonCode === "less_power"
          ? t("controlReplay.evOneHpEnoughLabel")
          : "",
        reasonSummary: isCoolingModeEvent
          ? coolingStopReason === "dew_stop"
            ? t("controlReplay.evDewPausedCopy")
            : coolingDemandEnded
            ? t("controlReplay.evCoolGoneCopy")
            : t("controlReplay.evCoolRoundedCopy")
          : heatingDemandEnded
          ? t("controlReplay.evHeatGoneCopy")
          : reasonCode === "less_power"
          ? t("controlReplay.evDemandLowerCopy")
          : "",
        summary: isCoolingModeEvent
          ? coolingStopReason === "dew_stop"
            ? t("controlReplay.evCoolDewSummary", { subject })
            : coolingDemandEnded
            ? t("controlReplay.evNoCoolLeft")
            : t("controlReplay.evCoolReadySummary", { subject })
          : heatingDemandEnded
          ? t("controlReplay.evNoHeatLeft")
          : reasonCode === "less_power"
          ? t("controlReplay.evDemandLowerSummary")
          : t("controlReplay.evStoppedLessPower", { subject }),
        detail: isCoolingModeEvent
          ? coolingStopReason === "dew_stop"
            ? t("controlReplay.evProtectionBehavior")
            : t("controlReplay.evPumpRerun")
          : heatingDemandEnded
          ? t("controlReplay.evNoHeatControl")
          : t("controlReplay.evNoIdleRun"),
        next: isCoolingModeEvent
          ? coolingStopReason === "dew_stop"
            ? t("controlReplay.evDewRestart")
            : t("controlReplay.evStandbyRerun")
          : heatingDemandEnded
          ? t("controlReplay.evStandbyHeat")
          : t("controlReplay.evRisingRestart"),
      };
      case "topology_change":
        return {
        title: isCoolingModeEvent
          ? event?.to === "idle"
            ? reasonCode === "cooling_request_cleared"
              ? t("controlReplay.evTopoCoolNoDemandTitle")
              : reasonCode === "dew_stop"
              ? t("controlReplay.evTopoCoolDewTitle")
              : t("controlReplay.evTopoCoolStoppedTitle")
            : t("controlReplay.evTopoCoolActiveTitle")
          : event?.to === "idle" && heatingDemandEnded
          ? t("controlReplay.evTopoHeatStoppedTitle")
          : event?.to === "duo"
          ? t("controlReplay.evTopoTwoHpTitle")
          : t("controlReplay.evTopoOneHpTitle"),
        reasonLabel: isCoolingModeEvent
          ? event?.to === "idle"
            ? reasonCode === "cooling_request_cleared"
              ? t("controlReplay.evNoCoolLabel")
              : reasonCode === "dew_stop"
              ? t("controlReplay.evDewStopLabel")
              : t("controlReplay.evTopoCoolStoppedTitle")
            : t("controlReplay.evTopoCoolActive")
          : event?.to === "idle" && heatingDemandEnded
          ? t("controlReplay.evNoHeatLabel")
          : "",
        reasonSummary: isCoolingModeEvent
          ? event?.to === "idle"
            ? reasonCode === "cooling_request_cleared"
              ? t("controlReplay.evTopoCoolGone")
              : reasonCode === "dew_stop"
              ? t("controlReplay.evTopoCoolPause")
              : t("controlReplay.evTopoNoHpCool")
            : t("controlReplay.evTopoCoolMargins")
          : event?.to === "idle" && heatingDemandEnded
          ? t("controlReplay.evTopoHeatGone")
          : "",
        summary: isCoolingModeEvent
          ? event?.to === "idle"
            ? reasonCode === "cooling_request_cleared"
              ? t("controlReplay.evTopoCoolGoneSummary")
              : reasonCode === "dew_stop"
              ? t("controlReplay.evTopoCoolDewSummary")
              : t("controlReplay.evTopoNoHpCoolSummary")
            : t("controlReplay.evTopoCoolingSummary", { subject })
          : event?.to === "duo"
          ? t("controlReplay.evTopoTwoHpSummary")
          : event?.to === "idle" && heatingDemandEnded
          ? t("controlReplay.evTopoNoHpHeatSummary")
          : t("controlReplay.evTopoLowerSummary"),
        detail: isCoolingModeEvent
          ? t("controlReplay.evTopoCoolLogic")
          : t("controlReplay.evTopoDuoLogic"),
        next: isCoolingModeEvent
          ? t("controlReplay.evTopoCoolStays")
          : event?.to === "duo"
          ? t("controlReplay.evTopoDuoStays")
          : event?.to === "idle" && heatingDemandEnded
          ? t("controlReplay.evTopoStandbyHeat")
          : t("controlReplay.evTopoSecondAvailable"),
      };
      case "decision_hold":
        return {
        title: reasonCode === "defrost_hold" ? t("controlReplay.evHoldDefrostTitle") : t("controlReplay.evHoldDelayedTitle"),
        summary: reasonCode === "defrost_hold"
          ? t("controlReplay.evHoldDefrostCopy")
          : t("controlReplay.evHoldWaitCopy"),
        detail: reason.summary,
        next: t("controlReplay.evHoldNext"),
      };
      case "decision_blocked":
        return {
        title: reasonCode === "flow_too_low"
          ? t("controlReplay.evBlockedFlowTitle")
          : subject === t("controlReplay.subjBoiler") ? t("controlReplay.evBlockedBoilerTitle") : t("controlReplay.evBlockedGenericTitle"),
        reasonLabel: reasonCode === "flow_too_low" ? t("controlReplay.evBlockedFlowLabel") : "",
        reasonSummary: reasonCode === "flow_too_low"
          ? t("controlReplay.evBlockedFlowCopy")
          : "",
        summary: reasonCode === "flow_too_low"
          ? t("controlReplay.evBlockedFlowSummary")
          : subject === t("controlReplay.subjBoiler")
          ? t("controlReplay.evBlockedBoilerSummary")
          : t("controlReplay.evBlockedGenericSummary"),
        detail: reasonCode === "flow_too_low"
          ? t("controlReplay.evBlockedFlowDetail")
          : reason.summary,
        next: reasonCode === "flow_too_low"
          ? t("controlReplay.evBlockedFlowNext")
          : t("controlReplay.evBlockedRetryNext"),
        checks: reasonCode === "flow_too_low"
          ? [t("controlReplay.evBlockedFlowChecks"), t("controlReplay.evBlockedSafeOff"), t("controlReplay.evBlockedFlowReassess")]
          : null,
      };
      case "candidate_blocked":
        return {
        title: t("controlReplay.evCandidateWaiting", { subject }),
        summary: reasonCode === "candidate_in_rest"
          ? t("controlReplay.evCandidateRest", { subject })
          : t("controlReplay.evCandidateUnsafe", { subject }),
        detail: reason.summary,
        next: t("controlReplay.evCandidateRetry"),
      };
      case "flow_hold_start":
        return {
        title: reasonCode === "flow_postflow"
          ? coolingRuntimeHold ? t("controlReplay.evFlowPostCoolShort") : heatingRuntimeHold ? t("controlReplay.evFlowPostHeatShort") : isCoolingModeEvent ? t("controlReplay.evFlowPostCoolActive") : t("controlReplay.evFlowPostActive")
          : isFlowFault ? t("controlReplay.evFlowStartWait")
          : isCoolingModeEvent ? t("controlReplay.evFlowPreCool") : t("controlReplay.evFlowPreStart"),
        reasonLabel: reasonCode === "flow_postflow"
          ? coolingRuntimeHold || heatingRuntimeHold ? t("controlReplay.evFlowPostLabel") : isCoolingModeEvent ? t("controlReplay.evFlowPostCoolLabel") : t("controlReplay.evFlowPostLabelActive")
          : isFlowFault ? t("controlReplay.evFlowLowLabel")
          : isCoolingModeEvent ? t("controlReplay.evFlowPreCoolLabel") : t("controlReplay.evFlowPreLabel"),
        reasonSummary: isCoolingModeEvent
          ? reasonCode === "flow_postflow"
            ? coolingRuntimeHold
              ? t("controlReplay.evRuntimeCoolCopy", { source: activeCoolingSource })
              : t("controlReplay.evPumpPostCoolCopy")
            : t("controlReplay.evPumpPreCoolCopy")
          : heatingRuntimeHold
          ? t("controlReplay.evRuntimeHeatCopy", { source: activeHeatingSource })
          : "",
        summary: isCoolingModeEvent
          ? reasonCode === "flow_postflow"
            ? coolingRuntimeHold
              ? t("controlReplay.evRuntimeCoolSummary", { source: activeCoolingSource })
              : t("controlReplay.evPumpPostCoolSummary")
            : isFlowFault
            ? t("controlReplay.evFlowLowCoolSummary")
            : t("controlReplay.evPumpPreCoolSummary")
          : isFlowFault
          ? t("controlReplay.evFlowLowHeatSummary")
          : isFlowPreStart
            ? t("controlReplay.evPumpPreHeatSummary")
          : heatingRuntimeHold
          ? t("controlReplay.evRuntimeHeatSummary", { source: activeHeatingSource })
          : reason.summary,
        detail: isCoolingModeEvent
          ? coolingRuntimeHold
            ? t("controlReplay.evControllerNoNewCool")
            : t("controlReplay.evNormalStartCool")
          : heatingRuntimeHold
          ? t("controlReplay.evControllerNoNewHeat")
          : t("controlReplay.evCm1FlowPhase"),
        next: isCoolingModeEvent
          ? reasonCode === "flow_postflow"
            ? coolingRuntimeHold
              ? t("controlReplay.evRuntimeCoolStops", { source: activeCoolingSource })
              : t("controlReplay.evStandbyNewCool")
            : t("controlReplay.evAutoContinueCool")
          : heatingRuntimeHold
          ? t("controlReplay.evRuntimeHeatStops", { source: activeHeatingSource })
          : t("controlReplay.evAutoContinueFlow"),
      };
      case "flow_hold_clear":
        return {
        title: reasonCode === "flow_postflow"
          ? isCoolingModeEvent ? t("controlReplay.evFlowPostCoolDone") : t("controlReplay.evFlowPostDone")
          : isFlowFault ? t("controlReplay.evFlowRecovered")
          : isCoolingModeEvent ? t("controlReplay.evFlowPreCoolDone") : t("controlReplay.evFlowPreDone"),
        reasonLabel: reasonCode === "flow_postflow"
          ? isCoolingModeEvent ? t("controlReplay.evFlowPostCoolLabelDone") : t("controlReplay.evFlowPostLabelActive")
          : isFlowFault ? t("controlReplay.evFlowRecoveredLabel")
          : isCoolingModeEvent ? t("controlReplay.evCoolReleasedLabel") : t("controlReplay.evFlowPreDoneLabel"),
        reasonSummary: reasonCode === "flow_postflow"
          ? isCoolingModeEvent ? t("controlReplay.evFlowPostCoolDoneCopy") : t("controlReplay.evFlowPostDoneCopy")
          : isFlowFault
          ? t("controlReplay.evFlowRecoveredCopy")
          : isCoolingModeEvent
          ? t("controlReplay.evFlowPreCoolDoneCopy")
          : t("controlReplay.evFlowPreDoneCopy"),
        summary: isCoolingModeEvent
          ? reasonCode === "flow_postflow"
            ? t("controlReplay.evPumpPostCoolDone")
            : t("controlReplay.evFlowPreCoolDoneCopy")
          : reasonCode === "flow_postflow"
          ? t("controlReplay.evPumpPostDone")
          : t("controlReplay.evFlowPhaseDone"),
        detail: isCoolingModeEvent
          ? t("controlReplay.evFlowCoolTrajectory")
          : reasonCode === "flow_postflow"
          ? t("controlReplay.evHpStoppedPostDone")
          : t("controlReplay.evFlowBuiltUp"),
        next: isCoolingModeEvent
          ? reasonCode === "flow_postflow"
            ? t("controlReplay.evStandbyNewCoolDemand")
            : t("controlReplay.evContinueCoolMonitor")
          : t("controlReplay.evContinueAllModes"),
        checks: reasonCode === "flow_postflow"
          ? [t("controlReplay.evPostChecks"), t("controlReplay.evHpStoppedCheck"), t("controlReplay.evToStandbyCheck")]
          : isFlowFault
          ? [t("controlReplay.evRecoveredCheck"), t("controlReplay.evBlockLiftedCheck"), t("controlReplay.evContinueCheck")]
          : [t("controlReplay.evFlowEnoughCheck"), t("controlReplay.evHpReleasedCheck"), t("controlReplay.evContinueCheck")],
      };
      case "startup_inhibit_start":
        return {
        title: Number(event?.value_a) === 1 ? t("controlReplay.evInhibitCoolTitle") : t("controlReplay.evInhibitHeatTitle"),
        reasonLabel: t("controlReplay.evInhibitWaitLabel"),
        reasonSummary: t("controlReplay.evInhibitWaitCopy"),
        summary: Number(event?.value_a) === 1
          ? t("controlReplay.evInhibitCoolSummary")
          : t("controlReplay.evInhibitHeatSummary"),
        detail: t("controlReplay.evInhibitRebootCopy"),
        next: Number(event?.value_a) === 1
          ? t("controlReplay.evInhibitCoolNext")
          : t("controlReplay.evInhibitHeatNext"),
        checks: [t("controlReplay.evInhibitChecks"), t("controlReplay.evInhibitCompressorWaits"), t("controlReplay.evInhibitAutoStart")],
      };
      case "startup_inhibit_clear":
        return {
        title: t("controlReplay.evInhibitDoneTitle"),
        reasonLabel: t("controlReplay.evInhibitDoneLabel"),
        reasonSummary: t("controlReplay.evInhibitDoneCopy"),
        summary: t("controlReplay.evInhibitDoneSummary"),
        detail: t("controlReplay.evInhibitDoneDetail"),
        next: t("controlReplay.evInhibitDoneNext"),
        checks: [t("controlReplay.evInhibitElapsed"), t("controlReplay.evInhibitStartAllowed"), t("controlReplay.evContinueCheck")],
      };
      case "startup_inhibit_refresh":
        return {
        title: Number(event?.value_a) === 1 ? t("controlReplay.evInhibitRefreshCoolTitle") : t("controlReplay.evInhibitRefreshHeatTitle"),
        reasonLabel: t("controlReplay.evInhibitStaysLabel"),
        reasonSummary: t("controlReplay.evInhibitRefreshCopy"),
        summary: t("controlReplay.evInhibitRefreshSummary"),
        detail: t("controlReplay.evInhibitRefreshDetail"),
        next: t("controlReplay.evInhibitRefreshNext"),
        checks: [t("controlReplay.evInhibitReassessed"), t("controlReplay.evInhibitStaysLabel"), t("controlReplay.evInhibitAutoStart")],
      };
      case "defrost_seen_start":
        return {
        title: t("controlReplay.evDefrostStarted", { subject }),
        summary: t("controlReplay.evDefrostBrief", { subject }),
        detail: t("controlReplay.evDefrostSelf"),
        next: t("controlReplay.evDefrostAutoResume"),
      };
      case "defrost_seen_clear":
        return {
        title: t("controlReplay.evDefrostDone", { subject }),
        summary: t("controlReplay.evDefrostDoneCopy", { subject }),
        detail: t("controlReplay.evDefrostSeesEnd"),
        next: t("controlReplay.evDefrostDuoNext"),
      };
      case "cooling_limited":
        return {
        title: reasonCode === "dew_stop"
          ? t("controlReplay.evCoolLimitedDewTitle")
          : reasonCode === "restart_wait"
          ? t("controlReplay.evCoolLimitedRestartTitle")
          : reasonCode === "buffer_stop"
          ? t("controlReplay.evCoolLimitedBufferTitle")
          : coolingProtectionReason ? t("controlReplay.evCoolLimitedCappedTitle") : t("controlReplay.evCoolLimitedMaxTitle"),
        summary: reasonCode === "dew_stop"
          ? t("controlReplay.evCoolLimitedDewSummary", { source: activeCoolingSource })
          : reasonCode === "restart_wait"
          ? t("controlReplay.evCoolLimitedRestartSummary")
          : reasonCode === "buffer_stop"
          ? t("controlReplay.evCoolLimitedBufferSummary")
          : coolingProtectionReason
          ? t("controlReplay.evCoolLimitedCappedSummary")
          : t("controlReplay.evCoolLimitedMaxSummary"),
        detail: reason.summary,
        next: reasonCode === "restart_wait"
          ? t("controlReplay.curRestartExpect")
          : reasonCode === "buffer_stop"
          ? t("controlReplay.curBufferExpect")
          : coolingProtectionReason
          ? t("controlReplay.evCoolingReleasedNext")
          : t("controlReplay.evCoolingMaxNext"),
      };
      case "cooling_released":
        return {
        title: t("controlReplay.evCoolReleasedTitle"),
        summary: t("controlReplay.evCoolReleasedCopy"),
        detail: t("controlReplay.evCoolReleasedDetail"),
        next: t("controlReplay.evCoolReleasedNext"),
      };
      case "sticky_pump_run":
        return {
        title: t("controlReplay.evStickyDoneTitle"),
        summary: t("controlReplay.evStickyDoneCopy"),
        detail: t("controlReplay.evStickyDoneDetail"),
        next: t("controlReplay.evStickyDoneNext"),
      };
      case "frost_protection_start":
        return {
        title: t("controlReplay.evFrostOnTitle"),
        summary: t("controlReplay.evFrostOnCopy"),
        detail: t("controlReplay.evFrostOnDetail"),
        next: t("controlReplay.evFrostOnNext"),
      };
      case "frost_protection_clear":
        return {
        title: t("controlReplay.evFrostOffTitle"),
        summary: t("controlReplay.evFrostOffCopy"),
        detail: t("controlReplay.evFrostOffDetail"),
        next: t("controlReplay.evFrostOffNext"),
      };
      case "boiler_assist_start":
        return {
        title: t("controlReplay.evBoilerStartTitle"),
        summary: t("controlReplay.evBoilerStartCopy"),
        detail: t("controlReplay.evBoilerStartDetail"),
        next: t("controlReplay.evBoilerStartNext"),
      };
      case "boiler_assist_stop":
        return boilerStopBlocked
        ? {
          title: reasonCode === "sensor_fallback"
            ? t("controlReplay.evBoilerStopNoMeasure")
            : reasonCode === "no_candidate"
            ? t("controlReplay.evBoilerStopUnavailable")
            : reasonCode === "flow_preflow"
            ? t("controlReplay.evBoilerStopPreflow")
            : t("controlReplay.evBoilerStopSafe"),
          summary: reasonCode === "sensor_fallback"
            ? t("controlReplay.evBoilerStopNoMeasureCopy")
            : reasonCode === "no_candidate"
            ? t("controlReplay.evBoilerStopUnavailableCopy")
            : reasonCode === "flow_preflow"
            ? t("controlReplay.evBoilerStopPreflowCopy")
            : t("controlReplay.evBoilerStopSafeCopy"),
          detail: t("controlReplay.evBoilerStopDetail"),
          next: t("controlReplay.evBoilerStopNext"),
        }
        : {
          title: t("controlReplay.evBoilerStoppedTitle"),
          summary: t("controlReplay.evBoilerStoppedCopy"),
          detail: t("controlReplay.evBoilerStoppedDetail"),
          next: t("controlReplay.evBoilerStoppedNext"),
        };
      case "attention_pattern":
        return {
        title: t("controlReplay.evAttentionTitle"),
        summary: reasonCode === "start_stop_rate_high"
          ? t("controlReplay.evAttentionManyCopy")
          : t("controlReplay.evAttentionPatternCopy"),
        detail: reason.summary,
        next: t("controlReplay.evAttentionNext"),
      };
      default:
        return fallback;
    }
  }

  function getDecisionEventGraphEndMinute(startMinute, event, selectedWindow) {
    const durationS = Number(event?.duration_s);
    if (!Number.isFinite(durationS) || durationS <= 0) {
      return startMinute;
    }
    const minutes = getControlWorkingEventDurationChartMinutes(event, selectedWindow);
    return Math.max(startMinute, Math.min(1440, startMinute + Math.max(5, minutes)));
  }

  function getDecisionEventDisplaySeverity(event) {
    const eventType = String(event?.event_type || "");
    const reason = String(event?.reason || "");
    const incidentSeverity = getControlReplayIncidentDisplaySeverity(event);
    if (incidentSeverity) {
      return incidentSeverity;
    }
    if (isDecisionCoolingAdjustmentEvent(event)) {
      return "normal";
    }
    if (reason === "buffer_stop") {
      return "normal";
    }
    if (isControlWorkingCoolingProtectionReason(reason)) {
      return "limited";
    }
    if (eventType === "flow_hold_start" || eventType === "flow_hold_clear") {
      if (reason === "flow_preflow" || reason === "flow_postflow") {
        return "normal";
      }
      if (reason === "flow_too_low") {
        return eventType === "flow_hold_start" ? "limited" : "normal";
      }
    }
    return String(event?.severity || "normal");
  }

  function isDecisionCoolingAdjustmentEvent(event) {
    if (String(event?.event_type || "") !== "cooling_limited") {
      return false;
    }
    const reason = String(event?.reason || "");
    if (["capacity_cap", "room_cap", "cooling_limiter", "simmer", "falling_gap", "level1_hold"].includes(reason)) {
      return true;
    }
    return reason === "projected_floor" && Number(event?.value_a) > 0;
  }

  function mapDecisionEventToControlWorkingItem(event, selectedWindow, nowMs) {
    const eventType = String(event?.event_type || "");
    const reasonCode = String(event?.reason || "unknown");
    if (!eventType || eventType === "boot_marker" || event?._oq_hidden) {
      return null;
    }
    if ((eventType === "defrost_seen_start" || eventType === "defrost_seen_clear") && Number(event?._oq_context_cm ?? event?.cm) === 5) {
      return null;
    }
    if (isDecisionCoolingAdjustmentEvent(event) || eventType === "cooling_released") {
      return null;
    }
    const graphStart = getDecisionEventWindowMinute(event, selectedWindow, nowMs);
    if (!Number.isFinite(graphStart)) {
      return null;
    }
    const copy = getDecisionEventCopy(event);
    const contextCm = Number(event?._oq_context_cm ?? event?.cm);
    const source = eventType === "cooling_limited" || eventType === "cooling_released"
      ? getDecisionCoolingSourceLabel(event)
      : eventType === "source_start" || eventType === "source_stop" || eventType === "topology_change"
      ? getDecisionModeSubjectLabel(event?.subject, contextCm)
      : getDecisionSubjectLabel(event?.subject);
    const duration = formatDecisionDuration(event?.duration_s);
    const displaySeverity = getDecisionEventDisplaySeverity(event);
    return {
      id: `fw-${event.seq || event.uptime_s || eventType}`,
      kind: "event",
      severity: displaySeverity,
      time: formatDecisionLogTimeLabel(event, selectedWindow, nowMs),
      title: copy.title,
      summary: copy.summary,
      detailTitle: t("controlReplay.evDetailWhy"),
      detail: copy.detail,
      next: copy.next,
      source,
      reasonLabel: copy.reasonLabel || "",
      reasonSummary: copy.reasonSummary || "",
      reasonCode,
      modeLabel: Number(event?.cm) > 0 ? `CM${Number(event.cm)}` : "CM?",
      modeTransitionLabel: event?._oq_mode_transition || "",
      duration,
      graphStart: Math.max(0, Math.min(1440, graphStart)),
      graphEnd: getDecisionEventGraphEndMinute(graphStart, event, selectedWindow),
      realEventType: eventType,
      rawDecisionEvent: event,
      checks: Array.isArray(copy.checks) ? copy.checks : null,
      timelineHidden: ((eventType === "source_start" || eventType === "topology_change") && contextCm === 5) ||
        (eventType === "source_stop" && (event?._oq_cooling_stop_reason === "dew_stop" || reasonCode === "dew_stop")) ||
        eventType === "startup_inhibit_start" || eventType === "startup_inhibit_refresh" || eventType === "startup_inhibit_clear",
    };
  }

  function getControlWorkingVisibleEpochRange(startEpochMs, endEpochMs, selectedWindow, nowMs) {
    if (!Number.isFinite(startEpochMs) || !Number.isFinite(endEpochMs) || endEpochMs <= startEpochMs) {
      return null;
    }
    const windowBounds = getControlWorkingWindowBounds(selectedWindow, nowMs);
    const visibleStart = Math.max(startEpochMs, windowBounds.start);
    const visibleEnd = Math.min(endEpochMs, windowBounds.end);
    if (visibleEnd <= visibleStart) {
      return null;
    }
    const windowMs = Math.max(1, windowBounds.end - windowBounds.start);
    return {
      start: ((visibleStart - windowBounds.start) / windowMs) * 1440,
      end: ((visibleEnd - windowBounds.start) / windowMs) * 1440,
      durationS: Math.max(0, Math.round((visibleEnd - visibleStart) / 1000)),
    };
  }

  function getControlWorkingDerivedModeLabel(event) {
    const cm = Number(event?._oq_context_cm ?? event?.cm);
    return Number.isFinite(cm) && cm > 0 ? `CM${cm}` : "CM?";
  }

  function createControlWorkingDerivedSpan(config, selectedWindow, nowMs) {
    const range = getControlWorkingVisibleEpochRange(config.startEpochMs, config.endEpochMs, selectedWindow, nowMs);
    if (!range || range.durationS < Number(config.minDurationS || 60)) {
      return null;
    }
    return {
      id: config.id,
      kind: "span",
      severity: config.severity || "normal",
      time: getControlWorkingIntervalTimeLabel(range.start, range.end, Boolean(config.isOpen)),
      duration: formatDecisionDuration(range.durationS),
      title: config.title,
      summary: config.summary,
      detailTitle: config.detailTitle || t("controlReplay.spanWhy"),
      detail: config.detail,
      next: config.next,
      source: config.source || t("controlReplay.subjSystem"),
      reasonCode: config.reasonCode || "keep_current",
      reasonLabel: config.reasonLabel || "",
      reasonSummary: config.reasonSummary || "",
      modeLabel: config.modeLabel || getControlWorkingDerivedModeLabel(config.startEvent),
      modeTransitionLabel: "",
      graphStart: Math.max(0, Math.min(1440, range.start)),
      graphEnd: Math.max(0, Math.min(1440, range.end)),
      derivedFromDecisionLog: true,
    };
  }

  function buildControlWorkingDerivedItems(events, selectedWindow, nowMs) {
    const windowBounds = getControlWorkingWindowBounds(selectedWindow, nowMs);
    const intervals = { HP1: [], HP2: [], cooling: [], boiler: [], frost: [], startupInhibit: [] };
    const open = { HP1: null, HP2: null, cooling: null, boiler: null, frost: null, startupInhibit: null };
    const sourceKeys = (subject) => {
      const normalized = String(subject || "").toUpperCase();
      if (normalized === "BOTH") {
        return ["HP1", "HP2"];
      }
      return normalized === "HP1" || normalized === "HP2" ? [normalized] : [];
    };
    const eventEpoch = (event) => getDecisionEventEpochMs(event);
    const openInterval = (key, event) => {
      const startEpochMs = eventEpoch(event);
      if (!Number.isFinite(startEpochMs) || open[key]) {
        return;
      }
      open[key] = { key, startEvent: event, startEpochMs };
    };
    const closeInterval = (key, event) => {
      const active = open[key];
      const endEpochMs = eventEpoch(event);
      if (!active || !Number.isFinite(endEpochMs)) {
        return;
      }
      if (endEpochMs > active.startEpochMs) {
        intervals[key].push({ ...active, endEvent: event, endEpochMs });
      }
      open[key] = null;
    };
    const closeCoolingIfNoActiveCoolingSource = (event) => {
      const hpCoolingActive = ["HP1", "HP2"].some((key) => open[key] && Number(open[key].startEvent?._oq_context_cm ?? open[key].startEvent?.cm) === 5);
      if (!hpCoolingActive) {
        closeInterval("cooling", event);
      }
    };

    events
      .filter((event) => event && !event._oq_hidden)
      .sort(compareDecisionEvents)
      .forEach((event) => {
        const eventType = String(event?.event_type || "");
        const contextCm = Number(event?._oq_context_cm ?? event?.cm);
        if (eventType === "boot_marker") {
          Object.keys(open).forEach((key) => closeInterval(key, event));
        } else if (eventType === "source_start") {
          sourceKeys(event.subject).forEach((key) => openInterval(key, event));
          if (contextCm === 5) {
            openInterval("cooling", event);
          }
        } else if (eventType === "source_stop") {
          sourceKeys(event.subject).forEach((key) => closeInterval(key, event));
          if (contextCm === 5 || open.cooling) {
            closeCoolingIfNoActiveCoolingSource(event);
          }
        } else if (eventType === "topology_change") {
          if (event.to === "duo") {
            openInterval("HP1", event);
            openInterval("HP2", event);
          } else if (event.to === "single") {
            const activeSource = getControlWorkingSingleTopologySource(event);
            if (activeSource) {
              openInterval(activeSource, event);
              closeInterval(activeSource === "HP1" ? "HP2" : "HP1", event);
            } else {
              closeInterval("HP2", event);
            }
            closeCoolingIfNoActiveCoolingSource(event);
          } else if (event.to === "idle") {
            closeInterval("HP1", event);
            closeInterval("HP2", event);
            closeInterval("cooling", event);
          }
        } else if (eventType === "boiler_assist_start"
          || eventType === "boiler_fallback_start") {
          openInterval("boiler", event);
        } else if (eventType === "boiler_assist_stop"
          || eventType === "boiler_fallback_stop") {
          closeInterval("boiler", event);
        } else if (eventType === "frost_protection_start") {
          openInterval("frost", event);
        } else if (eventType === "frost_protection_clear") {
          closeInterval("frost", event);
        } else if (eventType === "startup_inhibit_start") {
          openInterval("startupInhibit", event);
        } else if (eventType === "startup_inhibit_refresh") {
          closeInterval("startupInhibit", event);
          openInterval("startupInhibit", event);
        } else if (eventType === "startup_inhibit_clear") {
          closeInterval("startupInhibit", event);
        } else if (eventType === "flow_hold_clear" && event.reason === "flow_postflow") {
          closeInterval("cooling", event);
        }
      });

    Object.keys(open).forEach((key) => {
      if (open[key]) {
        const openEndEpochMs = selectedWindow === "today"
          ? Math.min(windowBounds.end, nowMs)
          : windowBounds.end;
        intervals[key].push({ ...open[key], endEvent: null, endEpochMs: openEndEpochMs, isOpen: true });
      }
    });

    const items = [];
    const addItem = (item) => {
      if (item) {
        items.push(item);
      }
    };
    const intervalsOverlap = (left, right) =>
      left.startEpochMs < right.endEpochMs && right.startEpochMs < left.endEpochMs;
    const getCoolingIntervalSource = (coolingInterval) => {
      const coolingSources = ["HP1", "HP2"].filter((key) =>
        intervals[key].some((interval) =>
          Number(interval.startEvent?._oq_context_cm ?? interval.startEvent?.cm) === 5 &&
          intervalsOverlap(interval, coolingInterval)));
      if (coolingSources.length === 2) {
        return getDecisionModeSubjectLabel("BOTH", 5);
      }
      if (coolingSources.length === 1) {
        return getDecisionModeSubjectLabel(coolingSources[0], 5);
      }
      return getDecisionModeSubjectLabel(coolingInterval.startEvent?.subject, 5);
    };

    intervals.startupInhibit.forEach((interval, index) => {
      const targetMode = Number(interval.startEvent?.value_a) || 0;
      const coolingWait = targetMode === 1;
      const contextRefreshed = String(interval.endEvent?.event_type || "") === "startup_inhibit_refresh";
      addItem(createControlWorkingDerivedSpan({
        id: `fw-span-startup-inhibit-${index}-${interval.startEvent?.seq || interval.startEpochMs}`,
        startEpochMs: interval.startEpochMs,
        endEpochMs: interval.endEpochMs,
        isOpen: Boolean(interval.isOpen),
        startEvent: interval.startEvent,
        severity: "normal",
        title: interval.isOpen ? t("controlReplay.spanInhibitOpen") : t("controlReplay.spanInhibitClosed"),
        summary: coolingWait
          ? t("controlReplay.spanInhibitCoolOpen")
          : t("controlReplay.spanInhibitHeatOpen"),
        detail: t("controlReplay.spanInhibitDetail"),
        next: interval.isOpen
          ? coolingWait
            ? t("controlReplay.spanInhibitOpenCoolNext")
            : t("controlReplay.spanInhibitOpenHeatNext")
          : contextRefreshed
          ? t("controlReplay.spanInhibitRefreshedNext")
          : t("controlReplay.spanInhibitClosedNext"),
        source: getDecisionModeSubjectLabel(interval.startEvent?.subject, coolingWait ? 5 : 2),
        reasonCode: "startup_inhibit",
        reasonLabel: t("controlReplay.spanInhibitLabel"),
        reasonSummary: t("controlReplay.spanInhibitReasonCopy"),
        modeLabel: coolingWait ? "CM5" : "CM2",
        minDurationS: 1,
      }, selectedWindow, nowMs));
    });

    intervals.boiler.forEach((interval, index) => {
      const isFallback = String(interval.startEvent?.event_type || "") === "boiler_fallback_start";
      addItem(createControlWorkingDerivedSpan({
        id: `fw-span-boiler-${index}-${interval.startEvent?.seq || interval.startEpochMs}`,
        startEpochMs: interval.startEpochMs,
        endEpochMs: interval.endEpochMs,
        isOpen: Boolean(interval.isOpen),
        startEvent: interval.startEvent,
        severity: isFallback ? "limited" : "normal",
        title: isFallback ? t("controlReplay.spanBoilerFallbackTitle") : t("controlReplay.spanBoilerSupportTitle"),
        summary: isFallback
          ? t("controlReplay.spanBoilerFallbackCopy")
          : t("controlReplay.spanBoilerSupportCopy"),
        detail: isFallback
          ? t("controlReplay.spanBoilerFallbackDetail")
          : t("controlReplay.spanBoilerSupportDetail"),
        next: isFallback
          ? t("controlReplay.spanBoilerFallbackNext")
          : t("controlReplay.spanBoilerSupportNext"),
        source: t("controlReplay.subjBoiler"),
        reasonCode: isFallback ? "boiler_fallback" : "boiler_assist",
        modeLabel: isFallback ? "CM4" : "CM3",
        minDurationS: isFallback ? 1 : 120,
      }, selectedWindow, nowMs));
    });

    intervals.cooling.forEach((interval, index) => {
      addItem(createControlWorkingDerivedSpan({
        id: `fw-span-cooling-${index}-${interval.startEvent?.seq || interval.startEpochMs}`,
        startEpochMs: interval.startEpochMs,
        endEpochMs: interval.endEpochMs,
        isOpen: Boolean(interval.isOpen),
        startEvent: interval.startEvent,
        severity: "normal",
        title: t("controlReplay.spanCoolingTitle"),
        summary: t("controlReplay.spanCoolingCopy"),
        detail: t("controlReplay.spanCoolingDetail"),
        next: t("controlReplay.spanCoolingNext"),
        source: getCoolingIntervalSource(interval),
        reasonCode: "keep_current",
        reasonLabel: t("controlReplay.spanCoolingLabel"),
        reasonSummary: t("controlReplay.spanCoolingRunCopy"),
        modeLabel: "CM5",
        // An active run must be visible immediately; only completed micro-runs
        // are suppressed to keep historical timelines calm.
        minDurationS: interval.isOpen ? 1 : 120,
      }, selectedWindow, nowMs));
    });

    intervals.frost.forEach((interval, index) => {
      addItem(createControlWorkingDerivedSpan({
        id: `fw-span-frost-${index}-${interval.startEvent?.seq || interval.startEpochMs}`,
        startEpochMs: interval.startEpochMs,
        endEpochMs: interval.endEpochMs,
        isOpen: Boolean(interval.isOpen),
        startEvent: interval.startEvent,
        severity: "limited",
        title: t("controlReplay.spanFrostTitle"),
        summary: t("controlReplay.spanFrostCopy"),
        detail: t("controlReplay.spanFrostDetail"),
        next: t("controlReplay.spanFrostNext"),
        source: t("controlReplay.subjSystem"),
        reasonCode: "frost_protection",
        modeLabel: "CM98",
        minDurationS: 60,
      }, selectedWindow, nowMs));
    });

    intervals.HP1.forEach((hp1, index) => {
      intervals.HP2.forEach((hp2) => {
        const startEpochMs = Math.max(hp1.startEpochMs, hp2.startEpochMs);
        const endEpochMs = Math.min(hp1.endEpochMs, hp2.endEpochMs);
        const startEvent = hp1.startEpochMs >= hp2.startEpochMs ? hp1.startEvent : hp2.startEvent;
        const hp1ContextCm = Number(hp1.startEvent?._oq_context_cm ?? hp1.startEvent?.cm);
        const hp2ContextCm = Number(hp2.startEvent?._oq_context_cm ?? hp2.startEvent?.cm);
        const contextCm = Number(startEvent?._oq_context_cm ?? startEvent?.cm);
        if (contextCm === 5 || hp1ContextCm === 5 || hp2ContextCm === 5) {
          return;
        }
        const isOpen = Boolean(hp1.isOpen && hp2.isOpen);
        addItem(createControlWorkingDerivedSpan({
          id: `fw-span-duo-${index}-${hp1.startEvent?.seq || hp1.startEpochMs}-${hp2.startEvent?.seq || hp2.startEpochMs}`,
          startEpochMs,
          endEpochMs,
          isOpen,
          startEvent,
          severity: "normal",
          title: t("controlReplay.spanDuoTitle"),
          summary: t("controlReplay.spanDuoCopy"),
          detail: t("controlReplay.spanDuoDetail"),
          next: t("controlReplay.spanDuoNext"),
          source: getDecisionModeSubjectLabel("BOTH", 2),
          reasonCode: "better_heat",
          modeLabel: "CM2",
          minDurationS: 300,
        }, selectedWindow, nowMs));
      });
    });

    return items;
  }

  function enrichControlWorkingDecisionLogEvents(events) {
    const sorted = [...events].sort(compareDecisionEvents);
    const activeSourceCm = { HP1: 0, HP2: 0 };
    const defrostOpen = { HP1: false, HP2: false };
    let activeTopologyCm = 0;
    let activeFlowCm = 0;
    let previousModeCm = 0;
    let pendingCoolingStopReason = "";

    const sourceKeys = (subject) => {
      const normalized = String(subject || "").toUpperCase();
      if (normalized === "BOTH") {
        return ["HP1", "HP2"];
      }
      return normalized === "HP1" || normalized === "HP2" ? [normalized] : [];
    };
    const upcomingFlowContextCm = (index) => {
      const currentTime = getDecisionEventSortValue(sorted[index]);
      for (let offset = 1; offset <= 6 && index + offset < sorted.length; offset += 1) {
        const next = sorted[index + offset];
        const nextTime = getDecisionEventSortValue(next);
        if (Number.isFinite(currentTime) && Number.isFinite(nextTime) && nextTime - currentTime > 300) {
          break;
        }
        const nextType = String(next?.event_type || "");
        if (nextType === "flow_hold_clear" && Number(next?.value_a) === 5) {
          return 5;
        }
        if ((nextType === "source_start" || nextType === "topology_change" || nextType === "cooling_limited") && Number(next?.cm) === 5) {
          return 5;
        }
        if (nextType === "flow_hold_start") {
          break;
        }
      }
      return 0;
    };

    return sorted.map((event, index) => {
      const enriched = { ...event };
      const eventType = String(event?.event_type || "");
      const subject = String(event?.subject || "").toUpperCase();
      const reason = String(event?.reason || "");
      const cm = Number(event?.cm) || 0;
      if (eventType === "boot_marker") {
        activeSourceCm.HP1 = 0;
        activeSourceCm.HP2 = 0;
        defrostOpen.HP1 = false;
        defrostOpen.HP2 = false;
        activeTopologyCm = 0;
        activeFlowCm = 0;
        previousModeCm = 0;
        pendingCoolingStopReason = "";
      }
      let contextCm = cm;
      let hidden = false;
      let activeCoolingSource = "";
      let activeCoolingSubject = "";
      let coolingRuntimeHold = false;
      let activeHeatingSource = "";
      let heatingRuntimeHold = false;
      let coolingStopReason = "";
      const previousCm = previousModeCm;
      const coolingSources = () => ["HP1", "HP2"].filter((key) => activeSourceCm[key] === 5);
      const heatingSources = () => ["HP1", "HP2"].filter((key) => activeSourceCm[key] > 0 && activeSourceCm[key] !== 5);

      if (eventType === "source_start") {
        contextCm = cm || contextCm;
        sourceKeys(subject).forEach((key) => {
          activeSourceCm[key] = contextCm;
        });
      } else if (eventType === "source_stop") {
        const sourceCm = sourceKeys(subject).map((key) => activeSourceCm[key]).find((value) => value > 0);
        contextCm = sourceCm || contextCm;
        if (contextCm === 5 && pendingCoolingStopReason) {
          coolingStopReason = pendingCoolingStopReason;
          pendingCoolingStopReason = "";
        }
        sourceKeys(subject).forEach((key) => {
          activeSourceCm[key] = 0;
        });
      } else if (eventType === "topology_change") {
        if (event?.to === "idle") {
          contextCm = activeTopologyCm || contextCm;
          activeTopologyCm = 0;
        } else if (event?.to === "single" || event?.to === "duo") {
          contextCm = cm || activeTopologyCm || contextCm;
          activeTopologyCm = contextCm;
        }
      } else if (eventType === "flow_hold_start") {
        const activeCoolingSources = coolingSources();
        const activeHeatingSources = heatingSources();
        const nextAfterCm = Number(event?.value_a);
        contextCm = reason === "flow_postflow"
          ? activeTopologyCm || contextCm
          : nextAfterCm || upcomingFlowContextCm(index) || contextCm;
        if (reason === "flow_postflow" && contextCm === 5 && activeCoolingSources.length) {
          activeCoolingSource = activeCoolingSources.join(" + ");
          coolingRuntimeHold = true;
        }
        if (reason === "flow_postflow" && contextCm !== 5 && activeHeatingSources.length) {
          activeHeatingSource = activeHeatingSources.join(" + ");
          heatingRuntimeHold = true;
        }
        activeFlowCm = contextCm;
      } else if (eventType === "flow_hold_clear") {
        contextCm = Number(event?.value_a) || activeFlowCm || activeTopologyCm || contextCm;
        activeFlowCm = 0;
      } else if (eventType === "cooling_limited" || eventType === "cooling_released") {
        contextCm = 5;
        const activeCoolingSources = coolingSources();
        if (activeCoolingSources.length) {
          activeCoolingSource = activeCoolingSources.join(" + ");
          activeCoolingSubject = activeCoolingSources.length === 2 ? "BOTH" : activeCoolingSources[0];
        }
        if (eventType === "cooling_limited" && reason === "dew_stop") {
          pendingCoolingStopReason = "dew_stop";
        }
      }

      if (eventType === "defrost_seen_start" || eventType === "defrost_seen_clear") {
        const key = subject === "HP1" || subject === "HP2" ? subject : "HP1";
        if (contextCm === 5 || cm === 5) {
          hidden = true;
        } else if (eventType === "defrost_seen_start") {
          defrostOpen[key] = true;
        } else if (!defrostOpen[key]) {
          hidden = true;
        } else {
          defrostOpen[key] = false;
        }
      }

      enriched._oq_context_cm = contextCm;
      enriched._oq_hidden = hidden;
      enriched._oq_active_cooling_source = activeCoolingSource;
      enriched._oq_active_cooling_subject = activeCoolingSubject;
      enriched._oq_cooling_runtime_hold = coolingRuntimeHold;
      enriched._oq_active_heating_source = activeHeatingSource;
      enriched._oq_heating_runtime_hold = heatingRuntimeHold;
      enriched._oq_cooling_stop_reason = coolingStopReason;
      enriched._oq_previous_cm = previousCm;
      enriched._oq_mode_transition = deriveControlWorkingModeTransition(event, previousCm);
      const nextModeCm = getControlWorkingModeAfterEvent(event);
      if (Number.isFinite(nextModeCm)) {
        previousModeCm = nextModeCm;
      }
      return enriched;
    });
  }

  function getControlWorkingDecisionLogItems() {
    const events = getDecisionLogEvents();
    const selectedWindow = getControlWorkingSelectedWindow();
    const nowMs = Date.now();
    const enrichedEvents = enrichControlWorkingDecisionLogEvents(events);
    const eventItems = enrichedEvents
      .map((event) => mapDecisionEventToControlWorkingItem(event, selectedWindow, nowMs))
      .filter(Boolean);
    const derivedItems = buildControlWorkingDerivedItems(enrichedEvents, selectedWindow, nowMs);
    return [...eventItems, ...derivedItems]
      .sort((left, right) => {
        const startDelta = getControlWorkingItemMinuteRange(right).start - getControlWorkingItemMinuteRange(left).start;
        if (startDelta !== 0) {
          return startDelta;
        }
        const weights = { event: 0, span: 1, aggregate: 2 };
        return (weights[left.kind] ?? 3) - (weights[right.kind] ?? 3);
      });
  }

  function getControlWorkingSelectedItem(items) {
    const visibleItems = items.filter((item) => !item.timelineHidden);
    if (visibleItems.some((item) => item.id === state.controlReplaySelectedEpisode)) {
      return visibleItems.find((item) => item.id === state.controlReplaySelectedEpisode);
    }
    return visibleItems.find((item) => item.kind === "span" && item.reasonCode === "better_heat")
      || visibleItems.find((item) => item.kind === "span")
      || visibleItems[0]
      || null;
  }

  function parseControlWorkingClockMinute(value) {
    const match = String(value || "").match(/(\d{1,2}):(\d{2})/);
    if (!match) {
      return Number.NaN;
    }
    const hours = Number.parseInt(match[1], 10);
    const minutes = Number.parseInt(match[2], 10);
    if (!Number.isFinite(hours) || !Number.isFinite(minutes)) {
      return Number.NaN;
    }
    return Math.max(0, Math.min(1440, (hours * 60) + minutes));
  }

  function getControlWorkingItemMinuteRange(item) {
    if (Number.isFinite(Number(item?.graphStart))) {
      const start = Math.max(0, Math.min(1440, Number(item.graphStart)));
      const end = Number.isFinite(Number(item?.graphEnd))
        ? Math.max(start, Math.min(1440, Number(item.graphEnd)))
        : start;
      return { start, end };
    }
    const matches = String(item?.time || "").match(/\d{1,2}:\d{2}/g) || [];
    const start = parseControlWorkingClockMinute(matches[0]);
    const end = parseControlWorkingClockMinute(matches[1]);
    if (!Number.isNaN(start) && !Number.isNaN(end)) {
      return { start, end: Math.max(start, end) };
    }
    if (!Number.isNaN(start)) {
      return { start, end: start };
    }
    return { start: 430, end: 430 };
  }

  function getControlWorkingGraphMinute() {
    const minute = Number(state.controlReplayGraphMinute);
    return Number.isFinite(minute) ? Math.max(0, Math.min(1440, Math.round(minute / 5) * 5)) : 430;
  }

  function formatControlWorkingRelativeOffset(minutesBeforeNow) {
    const normalized = Math.max(0, Math.round(Number(minutesBeforeNow) || 0));
    if (normalized <= 5) {
      return t("controlReplay.relNow");
    }
    const days = Math.floor(normalized / 1440);
    const hours = Math.floor((normalized % 1440) / 60);
    const minutes = normalized % 60;
    if (days > 0) {
      return hours > 0 ? t("controlReplay.relDaysHours", { days, hours }) : t("controlReplay.relDays", { days });
    }
    if (hours > 0) {
      return minutes > 0 ? t("controlReplay.relHoursMinutes", { hours, minutes }) : t("controlReplay.relHours", { hours });
    }
    return t("controlReplay.relMinutes", { minutes });
  }

  function formatControlWorkingGraphCursorLabel(minute, windowModel = getControlWorkingWindowModel()) {
    const normalized = Math.max(0, Math.min(1440, Number(minute) || 0));
    if (windowModel.calendarDay === "today") {
      return formatControlWorkingAbsoluteTimeLabel(
        getControlWorkingWindowEpochForMinute(normalized, "today"),
        Date.now(),
        "time",
      );
    }
    if (windowModel.calendarDay === "yesterday") {
      return formatControlWorkingAbsoluteTimeLabel(
        getControlWorkingWindowEpochForMinute(normalized, "yesterday"),
        Date.now(),
        "time",
      );
    }
    if (windowModel.id === "week" || windowModel.id === "last48" || windowModel.id === "last3d" || windowModel.id === "custom") {
      return formatControlWorkingAbsoluteTimeLabel(
        getControlWorkingWindowEpochForMinute(normalized, windowModel.id),
        Date.now(),
        "weekday",
      );
    }
    return formatControlWorkingAbsoluteTimeLabel(
      getControlWorkingWindowEpochForMinute(normalized, windowModel.id),
      Date.now(),
      "auto",
    );
  }

  function getControlWorkingItemForMinute(items, minute) {
    const normalizedMinute = Math.max(0, Math.min(1440, Number(minute) || 0));
    const weights = { span: 0, aggregate: 1, event: 2 };
    const selectedItem = items
      .filter((item) => !item.timelineHidden)
      .map((item) => {
        const range = getControlWorkingGraphHitRange(item);
        if (normalizedMinute < range.start || normalizedMinute > range.end) {
          return null;
        }
        const span = Math.max(1, range.end - range.start);
        return { item, score: span + ((weights[item.kind] ?? 3) * 0.1) };
      })
      .filter(Boolean)
      .sort((a, b) => a.score - b.score)[0]?.item || null;
    return selectedItem || getControlWorkingActiveGraphContextForMinute(items, normalizedMinute);
  }

  function getControlWorkingEventDurationChartMinutes(event, selectedWindow = getControlWorkingSelectedWindow()) {
    const durationS = Number(event?.duration_s);
    if (!Number.isFinite(durationS) || durationS <= 0) {
      return 0;
    }
    return (durationS / 60) * (1440 / getControlWorkingWindowDurationMinutes(selectedWindow));
  }

  function getControlWorkingGraphHitRange(item) {
    const range = getControlWorkingItemMinuteRange(item);
    const eventType = String(item?.realEventType || "");
    const durationMinutes = getControlWorkingEventDurationChartMinutes(item?.rawDecisionEvent);
    if (eventType === "defrost_seen_clear" && durationMinutes > 0) {
      const width = Math.max(5, durationMinutes);
      return { start: Math.max(0, range.start - width), end: range.start };
    }
    if ((eventType === "flow_hold_clear" || eventType === "frost_protection_clear") && durationMinutes > 0) {
      const width = Math.max(1, durationMinutes);
      return { start: Math.max(0, range.start - width), end: range.start };
    }
    if (range.end > range.start) {
      return range;
    }
    if (item?.kind === "event") {
      return { start: range.start, end: Math.min(1440, range.start + 12) };
    }
    return range;
  }

  function getControlWorkingIntervalTimeLabel(startMinute, endMinute, isOpen = false) {
    const windowModel = getControlWorkingWindowModel();
    const start = formatControlWorkingGraphCursorLabel(startMinute, windowModel);
    const end = isOpen || endMinute >= 1440
      ? t("controlReplay.intervalOpenEnd")
      : formatControlWorkingGraphCursorLabel(endMinute, windowModel);
    return `${start}-${end}`;
  }

  function getControlWorkingOpenEndMinute(selectedWindow = getControlWorkingSelectedWindow(), nowMs = Date.now()) {
    if (selectedWindow !== "today") {
      return 1440;
    }
    const now = new Date(nowMs);
    return Math.max(0, Math.min(1440, Math.round((now.getHours() * 60) + now.getMinutes() + (now.getSeconds() / 60))));
  }

  function getControlWorkingActiveGraphContextForMinute(items, minute) {
    const intervals = [];
    const open = new Map();
    const sortedItems = [...items]
      .filter((item) => item.rawDecisionEvent)
      .sort((left, right) => getControlWorkingItemMinuteRange(left).start - getControlWorkingItemMinuteRange(right).start);
    const openInterval = (label, item, startMinute) => {
      if (!open.has(label)) {
        open.set(label, { label, item, start: startMinute });
      }
    };
    const closeInterval = (label, endMinute) => {
      const active = open.get(label);
      if (!active) {
        return;
      }
      intervals.push({ ...active, end: Math.max(active.start, endMinute) });
      open.delete(label);
    };
    const closeCoolingIfNoHeatPumpSource = (endMinute) => {
      if (open.has("Koeling") && !open.has("HP1") && !open.has("HP2")) {
        closeInterval("Koeling", endMinute);
      }
    };
    const sourceLabels = (subject) => {
      const normalized = String(subject || "").toUpperCase();
      const labels = [];
      if (normalized === "HP1" || normalized === "BOTH") {
        labels.push("HP1");
      }
      if (normalized === "HP2" || normalized === "BOTH") {
        labels.push("HP2");
      }
      return labels;
    };

    const activeAtWindowStart = getControlWorkingChartSourceStateAtWindowStart();
    const windowStartItem = {
      reasonCode: "keep_current",
      severity: "normal",
      modeLabel: activeAtWindowStart.sourceModes.HP1 || activeAtWindowStart.sourceModes.HP2
        ? `CM${activeAtWindowStart.sourceModes.HP1 || activeAtWindowStart.sourceModes.HP2}`
        : "CM?",
    };
    if (activeAtWindowStart.HP1) {
      openInterval("HP1", windowStartItem, 0);
    }
    if (activeAtWindowStart.HP2) {
      openInterval("HP2", windowStartItem, 0);
    }
    if (activeAtWindowStart.boiler) {
      openInterval("CV-ketel", windowStartItem, 0);
    }
    if (activeAtWindowStart.cooling) {
      openInterval("Koeling", windowStartItem, 0);
    }

    sortedItems.forEach((item) => {
      const range = getControlWorkingItemMinuteRange(item);
      const eventType = String(item.realEventType || "");
      const event = item.rawDecisionEvent || {};
      const contextCm = Number(event._oq_context_cm ?? event.cm);
      const labels = sourceLabels(event.subject);
      if (eventType === "source_start") {
        labels.forEach((label) => openInterval(label, item, range.start));
        if (contextCm === 5) {
          openInterval("Koeling", item, range.start);
        }
      } else if (eventType === "source_stop") {
        labels.forEach((label) => closeInterval(label, range.start));
        if (contextCm === 5 || open.has("Koeling")) {
          closeCoolingIfNoHeatPumpSource(range.start);
        }
      } else if (eventType === "topology_change") {
        if (event.to === "duo") {
          openInterval("HP1", item, range.start);
          openInterval("HP2", item, range.start);
        } else if (event.to === "single") {
          const activeSource = getControlWorkingSingleTopologySource(event);
          if (activeSource) {
            openInterval(activeSource, item, range.start);
            closeInterval(activeSource === "HP1" ? "HP2" : "HP1", range.start);
          } else {
            closeInterval("HP2", range.start);
          }
          closeCoolingIfNoHeatPumpSource(range.start);
        } else if (event.to === "idle") {
          closeInterval("HP1", range.start);
          closeInterval("HP2", range.start);
          closeInterval("Koeling", range.start);
        }
      } else if (eventType === "boiler_assist_start"
        || eventType === "boiler_fallback_start") {
        openInterval("CV-ketel", item, range.start);
      } else if (eventType === "boiler_assist_stop"
        || eventType === "boiler_fallback_stop") {
        closeInterval("CV-ketel", range.start);
      } else if (eventType === "flow_hold_clear" && event.reason === "flow_postflow") {
        closeInterval("Koeling", range.start);
      }
    });
    const openEndMinute = getControlWorkingOpenEndMinute();
    open.forEach((active) => {
      if (active.start <= openEndMinute) {
        intervals.push({ ...active, end: openEndMinute });
      }
    });

    const activeIntervals = intervals.filter((interval) => minute >= interval.start && minute <= interval.end);
    if (!activeIntervals.length) {
      return null;
    }
    const labels = new Set(activeIntervals.map((interval) => interval.label));
    const hpLabels = ["HP1", "HP2"].filter((label) => labels.has(label));
    const cvActive = labels.has("CV-ketel");
    const coolingActive = labels.has("Koeling");
    const primaryInterval = activeIntervals
      .filter((interval) => hpLabels.includes(interval.label) || interval.label === "CV-ketel" || interval.label === "Koeling")
      .sort((left, right) => left.start - right.start)[0] || activeIntervals[0];
    const startMinute = Math.max(...activeIntervals.map((interval) => interval.start));
    const endMinute = Math.min(...activeIntervals.map((interval) => interval.end));
    let source = [
      ...hpLabels,
      cvActive ? t("controlReplay.subjBoiler") : "",
      coolingActive ? t("controlReplay.subjCooling") : "",
    ].filter(Boolean).join(" + ");
    let title = t("controlReplay.graphCtxActive");
    let summary = t("controlReplay.graphCtxActiveCopy");
    let detail = t("controlReplay.graphCtxActiveDetail");
    let next = t("controlReplay.graphCtxActiveNext");
    let reasonCode = primaryInterval.item?.reasonCode || "keep_current";
    let severity = "normal";

    if (coolingActive) {
      title = t("controlReplay.spanCoolingTitle");
      summary = hpLabels.length === 2
        ? t("controlReplay.graphCtxCoolingRunMany", { sources: hpLabels.join(" + ") })
        : hpLabels.length === 1
        ? t("controlReplay.graphCtxCoolingRunOne", { source: hpLabels[0] })
        : t("controlReplay.graphCtxCoolingNone");
      detail = t("controlReplay.graphCtxCoolingDetail");
      next = t("controlReplay.graphCtxCoolingNext");
      source = hpLabels.length === 2
        ? getDecisionModeSubjectLabel("BOTH", 5)
        : hpLabels.length === 1
        ? getDecisionModeSubjectLabel(hpLabels[0], 5)
        : t("controlReplay.subjCooling");
      reasonCode = primaryInterval.item?.reasonCode || "keep_current";
      severity = primaryInterval.item?.severity || "normal";
    } else if (hpLabels.length === 2 && cvActive) {
      title = t("controlReplay.graphCtxDuoBoilerTitle");
      summary = t("controlReplay.graphCtxDuoBoilerCopy");
      detail = t("controlReplay.graphCtxDuoBoilerDetail");
      next = t("controlReplay.graphCtxDuoBoilerNext");
      reasonCode = "boiler_assist";
      severity = "limited";
    } else if (hpLabels.length === 2) {
      title = t("controlReplay.spanDuoTitle");
      summary = t("controlReplay.graphCtxDuoCopy");
      detail = t("controlReplay.graphCtxDuoDetail");
      next = t("controlReplay.graphCtxDuoNext");
      source = getDecisionModeSubjectLabel("BOTH", 2);
      reasonCode = "better_heat";
    } else if (hpLabels.length === 1 && cvActive) {
      title = t("controlReplay.graphCtxSingleBoilerTitle", { source: hpLabels[0], boiler: t("controlReplay.subjBoiler") });
      summary = t("controlReplay.graphCtxSingleBoilerCopy");
      detail = t("controlReplay.graphCtxSingleBoilerDetail");
      next = t("controlReplay.graphCtxSingleBoilerNext");
      reasonCode = "boiler_assist";
      severity = "limited";
    } else if (hpLabels.length === 1) {
      title = t("controlReplay.graphCtxSingleTitle", { source: hpLabels[0] });
      summary = t("controlReplay.graphCtxSingleCopy", { source: hpLabels[0] });
      detail = t("controlReplay.graphCtxSingleDetail");
      next = t("controlReplay.graphCtxSingleNext");
      source = getDecisionModeSubjectLabel(hpLabels[0], 2);
      reasonCode = primaryInterval.item?.reasonCode || "runtime_lead";
    } else if (cvActive) {
      title = t("controlReplay.graphCtxBoilerTitle");
      summary = t("controlReplay.graphCtxBoilerCopy");
      detail = t("controlReplay.graphCtxBoilerDetail");
      next = t("controlReplay.graphCtxBoilerNext");
      reasonCode = "boiler_assist";
      severity = "limited";
    }

    return {
      id: `graph-context-${Math.round(minute)}-${Array.from(labels).join("-")}`,
      kind: "span",
      severity,
      time: getControlWorkingIntervalTimeLabel(startMinute, endMinute),
      duration: "",
      title,
      summary,
      detailTitle: t("controlReplay.graphCtxWhat"),
      detail,
      next,
      source: source || t("controlReplay.subjSystem"),
      reasonCode,
      modeLabel: primaryInterval.item?.modeLabel || "CM?",
      graphStart: startMinute,
      graphEnd: endMinute,
    };
  }

  function renderControlWorkingTabs() {
    const selectedTab = getControlWorkingSelectedTab();
    return `
      <div class="oq-working-control-group">
        <span class="oq-working-control-label">${t("controlReplay.chromeView")}</span>
        <div class="oq-working-tabs" role="tablist" aria-label="${t("controlReplay.chromeDecisionsView")}">
          ${getControlWorkingTabs().map((tab) => `
            <button
              class="oq-working-tab${selectedTab === tab.id ? " is-active" : ""}"
              type="button"
              role="tab"
              aria-selected="${selectedTab === tab.id ? "true" : "false"}"
              data-oq-action="select-control-replay-tab"
              data-replay-tab="${escapeHtml(tab.id)}"
            >
              ${renderOqIcon(tab.icon, "oq-working-tab-icon")}
              <span>${escapeHtml(tab.label)}</span>
            </button>
          `).join("")}
        </div>
      </div>
    `;
  }

  function renderControlWorkingWindowChoices() {
    const selectedWindow = getControlWorkingSelectedWindow();
    const selectedModel = getControlWorkingWindowModel();
    const quickOptions = getControlWorkingQuickWindowOptions();
    const moreOptions = getControlWorkingWindowOptions().filter((option) => !option.quick && !option.custom);
    const customDraft = getControlWorkingCustomDraft();
    const customInputBounds = getControlWorkingCustomInputBounds(customDraft);
    const customStart = getControlWorkingCustomDateTimeParts(customDraft.start);
    const customEnd = getControlWorkingCustomDateTimeParts(customDraft.end);
    const menuOpen = state.controlReplayPeriodMenuOpen;
    const menuLabel = selectedWindow === "custom"
      ? t("controlReplay.chromeCustomPeriod")
      : quickOptions.some((option) => option.id === selectedWindow)
      ? t("controlReplay.chromeChoosePeriod")
      : selectedModel.shortLabel;
    return `
      <div class="oq-working-control-group oq-working-control-group--period">
        <span class="oq-working-control-label">${t("controlReplay.chromePeriod")}</span>
        <div class="oq-working-window-controls" role="group" aria-label="${t("controlReplay.chromePeriod")}">
          <div class="oq-working-window-choices" aria-label="${t("controlReplay.chromeQuickChoices")}">
          ${quickOptions.map((option) => `
            <button
              class="oq-working-window-choice${selectedWindow === option.id ? " is-active" : ""}"
              type="button"
              data-oq-action="select-control-replay-window"
              data-replay-window="${escapeHtml(option.id)}"
              aria-pressed="${selectedWindow === option.id ? "true" : "false"}"
              aria-label="${escapeHtml(option.label)}"
            >
              ${escapeHtml(option.shortLabel)}
            </button>
          `).join("")}
          </div>
          <div class="oq-working-period-menu" data-oq-control-replay-period-menu>
            <button
              class="oq-working-period-menu-toggle${menuOpen || !quickOptions.some((option) => option.id === selectedWindow) ? " is-active" : ""}"
              type="button"
              aria-expanded="${menuOpen ? "true" : "false"}"
              aria-haspopup="dialog"
              data-oq-action="toggle-control-replay-period-menu"
            >
              <span>${escapeHtml(menuLabel)}</span>
              <span class="oq-working-period-menu-chevron" aria-hidden="true"></span>
            </button>
            ${menuOpen ? `
              <section class="oq-working-period-popover" role="dialog" aria-label="${t("controlReplay.chromeChoosePeriod")}">
                <div class="oq-working-period-popover-head">
                  <strong>${t("controlReplay.chromeOtherWindow")}</strong>
                </div>
                <div class="oq-working-period-option-grid">
                  ${moreOptions.map((option) => `
                    <button
                      class="oq-working-period-option${selectedWindow === option.id ? " is-active" : ""}"
                      type="button"
                      data-oq-action="select-control-replay-window"
                      data-replay-window="${escapeHtml(option.id)}"
                      aria-pressed="${selectedWindow === option.id ? "true" : "false"}"
                    >${escapeHtml(option.shortLabel)}</button>
                  `).join("")}
                </div>
                <div class="oq-working-period-custom">
                  <button
                    class="oq-working-period-custom-toggle${state.controlReplayCustomPeriodOpen || selectedWindow === "custom" ? " is-active" : ""}"
                    type="button"
                    aria-expanded="${state.controlReplayCustomPeriodOpen ? "true" : "false"}"
                    data-oq-action="toggle-control-replay-custom-period"
                  >
                    <span>${t("controlReplay.chromeCustomPeriod")}</span>
                    <span class="oq-working-period-custom-toggle-copy">${t("controlReplay.chromeDateHour")}</span>
                  </button>
                  ${state.controlReplayCustomPeriodOpen ? `
                    <div class="oq-working-period-custom-fields">
                      <label>
                        <span>${t("controlReplay.chromeFrom")}</span>
                        <div class="oq-working-period-date-hour">
                          <input type="date" min="${escapeHtml(customInputBounds.earliestDate)}" max="${escapeHtml(customInputBounds.startMaxDate)}" value="${escapeHtml(customStart.date)}" data-oq-control-replay-custom-start-date data-oq-control-replay-custom-input>
                          <select aria-label="${t("controlReplay.chromeHourFrom")}" data-oq-control-replay-custom-start-hour data-oq-control-replay-custom-input>
                            ${renderControlWorkingHourOptions(customStart.hour)}
                          </select>
                        </div>
                      </label>
                      <label>
                        <span>${t("controlReplay.chromeUntil")}</span>
                        <div class="oq-working-period-date-hour">
                          <input type="date" min="${escapeHtml(customInputBounds.endMinDate)}" max="${escapeHtml(customInputBounds.endMaxDate)}" value="${escapeHtml(customEnd.date)}" data-oq-control-replay-custom-end-date data-oq-control-replay-custom-input>
                          <select aria-label="${t("controlReplay.chromeHourUntil")}" data-oq-control-replay-custom-end-hour data-oq-control-replay-custom-input>
                            ${renderControlWorkingHourOptions(customEnd.hour)}
                          </select>
                        </div>
                      </label>
                    </div>
                    <div class="oq-working-period-custom-actions">
                      <span>${t("controlReplay.chromeMaxRange")}</span>
                      <button class="oq-working-period-apply" type="button" data-oq-action="apply-control-replay-custom-period">${t("controlReplay.chromeApply")}</button>
                    </div>
                    ${state.controlReplayCustomPeriodError ? `<p class="oq-working-period-error" role="alert">${escapeHtml(state.controlReplayCustomPeriodError)}</p>` : ""}
                  ` : ""}
                </div>
              </section>
            ` : ""}
          </div>
        </div>
      </div>
    `;
  }

  function renderControlWorkingNowCard(current) {
    const status = getControlWorkingSeverityMeta(current.severity);
    return `
      <section class="oq-working-now oq-working-now--${escapeHtml(status.tone)}">
        <div class="oq-working-now-main">
          <span class="oq-working-eyebrow">${t("controlReplay.nowTitle")}</span>
          <h2>${escapeHtml(current.title)}${renderControlWorkingModeBadge(current)}</h2>
          <p>${escapeHtml(current.copy)}</p>
          <div class="oq-working-pill-row">
            ${renderControlWorkingPill(status.label, status.tone, "shield")}
            ${renderControlWorkingPill(current.reasonLabel, "info", "target")}
            ${renderControlWorkingPill(current.sinceLabel, "context")}
          </div>
        </div>
        <div class="oq-working-now-next">
          <span>${t("controlReplay.nowNext")}</span>
          <strong>${escapeHtml(current.expectation)}</strong>
          <div class="oq-working-source-strip">
            <span>HP1 · ${escapeHtml(current.hp1Status)}</span>
            <span>HP2 · ${escapeHtml(current.hp2Status)}</span>
            <span>CV · ${escapeHtml(current.cvStatus)}</span>
          </div>
        </div>
      </section>
    `;
  }

  function renderControlWorkingTimelineItem(item, selectedItem) {
    const status = getControlWorkingSeverityMeta(item.severity);
    const selected = selectedItem && selectedItem.id === item.id;
    const kindLabel = getControlWorkingKindLabel(item.kind);
    const modeMetaLabel = getControlWorkingModeMetaLabel(item);
    return `
      <button
        class="oq-working-entry oq-working-entry--${escapeHtml(item.kind)} oq-working-entry--${escapeHtml(status.tone)}${selected ? " is-active" : ""}"
        type="button"
        data-oq-action="select-control-replay-episode"
        data-replay-episode="${escapeHtml(item.id)}"
      >
        <span class="oq-working-entry-time">
          <strong>${escapeHtml(item.time)}</strong>
          <small>${escapeHtml(kindLabel)}</small>
        </span>
        <span class="oq-working-entry-rail" aria-hidden="true"></span>
        <span class="oq-working-entry-body">
          <span class="oq-working-entry-title">
            <strong>${escapeHtml(item.title)}</strong>
            ${renderControlWorkingModeBadge(item)}
            ${item.count ? `<em>${escapeHtml(item.count)}</em>` : ""}
          </span>
          <span class="oq-working-entry-summary">${escapeHtml(item.summary)}</span>
          <span class="oq-working-entry-meta">
            <span>${escapeHtml(item.source)}</span>
            ${modeMetaLabel ? `<span class="oq-working-entry-meta-mode">${escapeHtml(modeMetaLabel)}</span>` : ""}
            <span>${escapeHtml(item.reasonLabel || getControlWorkingReasonLabel(item.reasonCode))}</span>
            ${item.duration ? `<span>${escapeHtml(t("controlReplay.entryDuration", { value: item.duration }))}</span>` : ""}
          </span>
        </span>
        <span class="oq-working-entry-status">${escapeHtml(status.label)}</span>
      </button>
    `;
  }

  function renderControlWorkingDetails(item) {
    if (!item) {
      return "";
    }
    const status = getControlWorkingSeverityMeta(item.severity);
    const reason = getControlWorkingReasonMeta(item.reasonCode);
    const reasonLabel = item.reasonLabel || reason.label;
    const reasonSummary = item.reasonSummary || reason.summary;
    const optimizer = getControlWorkingOptimizerModel(item);
    const modeMetaLabel = getControlWorkingModeMetaLabel(item);
    const checks = Array.isArray(item.checks) ? item.checks : reason.checks;
    return `
      <aside class="oq-working-detail oq-working-detail--${escapeHtml(status.tone)}">
        <div>
          <span class="oq-working-eyebrow">${t("controlReplay.detailSelected")}</span>
          <h3>${escapeHtml(item.title)}${renderControlWorkingModeBadge(item)}</h3>
          <p>${escapeHtml(item.summary)}</p>
        </div>
        <div class="oq-working-detail-block">
          <strong>${t("controlReplay.detailWhy")}</strong>
          <span>${escapeHtml(item.detail)}</span>
        </div>
        <div class="oq-working-detail-block">
          <strong>${t("controlReplay.detailNormal")}</strong>
          <span>${escapeHtml(reasonSummary)}</span>
        </div>
        <div class="oq-working-detail-block">
          <strong>${t("controlReplay.detailNext")}</strong>
          <span>${escapeHtml(item.next)}</span>
        </div>
        ${renderControlWorkingOptimizer(optimizer)}
        ${checks.length ? `
          <div class="oq-working-checks" aria-label="${t("controlReplay.detailFactors")}">
            ${checks.map((check) => `<span>${renderOqIcon("shield", "oq-working-reason-icon")} ${escapeHtml(check)}</span>`).join("")}
          </div>
        ` : ""}
        <div class="oq-working-pill-row">
          ${renderControlWorkingPill(status.label, status.tone, "shield")}
          ${renderControlWorkingPill(reasonLabel, "info", "target")}
          ${renderControlWorkingPill(item.source, "context")}
        </div>
        <details class="oq-working-support" data-replay-support-item="${escapeHtml(item.id)}"${state.controlReplaySupportDetailsItemId === item.id ? " open" : ""}>
          <summary data-oq-action="toggle-control-replay-support-details">${t("controlReplay.detailSupport")}</summary>
          <dl>
            <div><dt>${t("controlReplay.detailRecord")}</dt><dd>${escapeHtml(getControlWorkingKindLabel(item.kind))}</dd></div>
            <div><dt>${t("controlReplay.detailSource")}</dt><dd>${escapeHtml(item.source)}</dd></div>
            <div><dt>${t("controlReplay.detailControlMode")}</dt><dd>${escapeHtml(item.modeLabel)}</dd></div>
            ${modeMetaLabel ? `<div><dt>${t("controlReplay.detailCmChange")}</dt><dd>${escapeHtml(modeMetaLabel)}</dd></div>` : ""}
            <div><dt>${t("controlReplay.detailReasonCode")}</dt><dd>${escapeHtml(item.reasonCode)}</dd></div>
          </dl>
        </details>
      </aside>
    `;
  }

  function renderControlWorkingGraphEmptyDetails(timeLabel) {
    return `
      <aside class="oq-working-detail">
        <div>
          <span class="oq-working-eyebrow">${t("controlReplay.graphGapEyebrow")}</span>
          <h3>${escapeHtml(t("controlReplay.graphGapTitle", { time: timeLabel }))}</h3>
          <p>${t("controlReplay.graphGapCopy")}</p>
        </div>
        <div class="oq-working-detail-block">
          <strong>${t("controlReplay.graphGapWhat")}</strong>
          <span>${t("controlReplay.graphGapWhatCopy")}</span>
        </div>
      </aside>
    `;
  }

  function renderControlWorkingEmptyState(title, copy) {
    return `
      <div class="oq-working-empty">
        <strong>${escapeHtml(title)}</strong>
        <span>${escapeHtml(copy)}</span>
      </div>
    `;
  }

  function renderControlWorkingTimelineTab(items, selectedItem) {
    const windowModel = getControlWorkingWindowModel();
    const visibleItems = items.filter((item) => !item.timelineHidden);
    const timelineItems = visibleItems.slice(0, 80);
    const decisionLogError = String(state.decisionLogError || "").trim();
    const waitingForDecisionLog = !timelineItems.length && !state.decisionLog && !decisionLogError;
    return `
      <div class="oq-working-split">
        <section class="oq-working-list" aria-label="${escapeHtml(windowModel.eyebrow)}">
          <div class="oq-working-list-head">
            <div>
              <span class="oq-working-eyebrow">${escapeHtml(windowModel.eyebrow)}</span>
              <h3>${escapeHtml(windowModel.title)}</h3>
            </div>
            <p>${escapeHtml(windowModel.copy)}</p>
          </div>
          ${timelineItems.length
            ? `<div class="oq-working-timeline">
                ${timelineItems.map((item) => renderControlWorkingTimelineItem(item, selectedItem)).join("")}
              </div>`
            : decisionLogError
            ? renderControlWorkingEmptyState(t("controlReplay.emptyLogUnavailable"), t("controlReplay.emptyLogUnavailableCopy", { error: decisionLogError }))
            : waitingForDecisionLog
            ? renderControlWorkingEmptyState(t("controlReplay.emptyLogLoading"), t("controlReplay.emptyLogLoadingCopy"))
            : renderControlWorkingEmptyState(t("controlReplay.emptyLogEmpty"), t("controlReplay.emptyLogEmptyCopy"))}
        </section>
        ${selectedItem ? renderControlWorkingDetails(selectedItem) : ""}
      </div>
    `;
  }

  function renderControlWorkingSourceCard(title, status, starts, hours, active, note = "") {
    return `
      <article class="oq-working-source-card${active ? " is-active" : ""}">
        <div>
          <span>${escapeHtml(title)}</span>
          <strong>${escapeHtml(status)}</strong>
        </div>
        ${note ? `<p class="oq-working-source-card-note">${escapeHtml(note)}</p>` : `<dl>
          <div><dt>${t("controlReplay.cardStarts24h")}</dt><dd>${escapeHtml(starts)}</dd></div>
          <div><dt>${t("controlReplay.cardRuntimeHours")}</dt><dd>${escapeHtml(hours)}</dd></div>
        </dl>`}
      </article>
    `;
  }

  function renderControlWorkingStatusTab(current) {
    const reason = getControlWorkingReasonMeta(current.primaryReason);
    const optimizer = getControlWorkingOptimizerModel({
      primaryReason: current.primaryReason,
      source: current.hp1Running && current.hp2Running ? "HP1 + HP2" : current.hp1Running ? "HP1" : current.hp2Running ? "HP2" : t("controlReplay.statusNoSource"),
    });
    const isCoolingGuard = Boolean(current.coolingProtection);
    const isCoolingCap = Boolean(current.coolingCapped);
    const isCoolingRestartWait = current.primaryReason === "restart_wait";
    const isCoolingWaterSatisfied = current.primaryReason === "buffer_stop";
    const isStartupInhibit = current.primaryReason === "startup_inhibit";
    const isSticky = current.primaryReason === "sticky_protection";
    const guardEyebrow = isStartupInhibit ? t("controlReplay.guardStartCond") : isCoolingWaterSatisfied ? t("controlReplay.guardCooling") : t("controlReplay.guardProtection");
    const guardTitle = isStartupInhibit
      ? t("controlReplay.guardInhibitTitle")
      : isCoolingWaterSatisfied
      ? t("controlReplay.guardWaterColdTitle")
      : isCoolingGuard
      ? isCoolingRestartWait ? t("controlReplay.guardRestartWaitTitle") : t("controlReplay.guardCoolLimitedTitle")
      : isCoolingCap
      ? t("controlReplay.guardCoolCappedTitle")
      : isSticky
      ? t("controlReplay.guardStickyTitle")
      : t("controlReplay.guardNoneTitle");
    const guardCopy = isStartupInhibit
      ? t("controlReplay.guardInhibitCopy")
      : isCoolingWaterSatisfied
      ? t("controlReplay.guardWaterColdCopy")
      : isCoolingGuard
      ? isCoolingRestartWait
        ? t("controlReplay.guardRestartWaitCopy")
        : t("controlReplay.guardCoolLimitedCopy")
      : isCoolingCap
      ? t("controlReplay.guardCoolCappedCopy")
      : isSticky
      ? t("controlReplay.guardStickyCopy")
      : t("controlReplay.guardNoneCopy");
    const guardPills = isStartupInhibit
      ? [
        [t("controlReplay.pillDemandActive"), "info", "activity"],
        [current.startupInhibit?.remainingLabel || t("controlReplay.curWaitActive"), "normal", "clock"],
        [t("controlReplay.pillAutoStart"), "context", "play"],
      ]
      : isCoolingWaterSatisfied
      ? [
        [t("controlReplay.pillCoolDemand"), "info", "snowflake"],
        [t("controlReplay.pillWaterCold"), "normal", "droplet"],
        [t("controlReplay.pillAutoRestart"), "context", "activity"],
      ]
      : isCoolingGuard
      ? [
        [t("controlReplay.pillDewMonitored"), "limited", "droplet"],
        [t("controlReplay.pillMaxLevel", { value: current.cooling.allowedMax }), "info", "target"],
        [t("controlReplay.pillNowLevel", { value: current.cooling.limitedDemand }), "context", "bar-chart"],
      ]
      : isCoolingCap
      ? [
        [t("controlReplay.pillSetMax", { value: current.cooling.allowedMax }), "info", "target"],
        [t("controlReplay.pillNowLevel", { value: current.cooling.limitedDemand }), "normal", "bar-chart"],
        [t("controlReplay.pillMarginMonitored"), "context", "shield"],
      ]
      : isSticky
      ? [
        [t("controlReplay.pillShortPumpRun"), "normal", "shield"],
        [t("controlReplay.pillNoCoolDemand"), "context", "snowflake"],
        [t("controlReplay.pillNoHpStart"), "info", "activity"],
      ]
      : [
        [t("controlReplay.pillDefrostFree"), "normal", "snowflake"],
        [t("controlReplay.pillRestFree"), "normal", "activity"],
        [t("controlReplay.pillFlowMonitored"), "info", "waves"],
      ];
    const coolingContextActive = current.cooling.requestActive || isCoolingGuard || isCoolingCap || current.strategyLabel === t("overview.strategyCooling");
    const telemetryRows = [
      [t("controlReplay.telSupply"), current.supplyTemp],
      [t("controlReplay.telOutside"), current.outsideTemp],
      [t("controlReplay.telFlow"), current.flow],
    ];
    if (!coolingContextActive) {
      telemetryRows.push([t("controlReplay.telStrategy"), current.strategyLabel]);
    }
    if (coolingContextActive) {
      telemetryRows.push([t("controlReplay.telDewPoint"), current.cooling.dewPoint]);
      telemetryRows.push([t("controlReplay.telSafeMin"), current.cooling.safeSupply]);
    }
    return `
      <div class="oq-working-status">
        ${renderControlWorkingNowCard(current)}
        <div class="oq-working-status-grid">
          <section class="oq-working-status-main${optimizer ? "" : " oq-working-status-main--wide"}">
            <span class="oq-working-eyebrow">${t("controlReplay.statusWhy")}</span>
            <h3>${escapeHtml(reason.label)}</h3>
            <p>${escapeHtml(reason.summary)}</p>
            <div class="oq-working-reason-list">
              ${reason.checks.map((check) => `<span>${renderOqIcon("target", "oq-working-reason-icon")} ${escapeHtml(check)}</span>`).join("")}
            </div>
          </section>
          ${optimizer ? `
            <section class="oq-working-optimizer-panel">
              ${renderControlWorkingOptimizer(optimizer)}
            </section>
          ` : ""}
          <section class="oq-working-source-grid" aria-label="${t("controlReplay.statusSources")}">
            ${renderControlWorkingSourceCard("HP1", current.hp1Status, current.hp1Starts, current.hp1Hours, current.hp1Running)}
            ${renderControlWorkingSourceCard("HP2", current.hp2Status, current.hp2Starts, current.hp2Hours, current.hp2Running)}
            ${renderControlWorkingSourceCard("CV", current.cvStatus, "", "", current.cvStatus === t("controlReplay.curCvActive"), coolingContextActive ? t("controlReplay.statusCvNoCooling") : t("controlReplay.statusCvSupport"))}
          </section>
          <section class="oq-working-guard-panel">
            <span class="oq-working-eyebrow">${escapeHtml(guardEyebrow)}</span>
            <h3>${escapeHtml(guardTitle)}</h3>
            <p>${escapeHtml(guardCopy)}</p>
            <div class="oq-working-pill-row">
              ${guardPills.map(([label, tone, icon]) => renderControlWorkingPill(label, tone, icon)).join("")}
            </div>
          </section>
          <section class="oq-working-telemetry">
            <span class="oq-working-eyebrow">${t("controlReplay.statusContext")}</span>
            <dl>
              ${telemetryRows.map(([label, value]) => `<div><dt>${escapeHtml(label)}</dt><dd>${escapeHtml(value)}</dd></div>`).join("")}
            </dl>
          </section>
        </div>
      </div>
    `;
  }

  function getControlWorkingChartLaneDisplayLabel(label) {
    if (label === "CV-ketel") return t("controlReplay.subjBoiler");
    if (label === "Koeling") return t("controlReplay.subjCooling");
    if (label === "Ontdooien") return t("controlReplay.laneDefrost");
    if (label === "Bescherming") return t("controlReplay.laneProtection");
    return label;
  }

  function renderControlWorkingChartLane(label, tone, segments) {
    return `
      <div class="oq-working-chart-lane">
        <span>${escapeHtml(getControlWorkingChartLaneDisplayLabel(label))}</span>
        <div class="oq-working-chart-track">
          ${segments.map((segment) => `
            <i class="oq-working-chart-segment oq-working-chart-segment--${escapeHtml(segment.tone || tone)}" style="--oq-chart-left:${clampControlReplayPercent(segment.start)}%;--oq-chart-width:${clampControlReplayPercent(segment.width)}%;"></i>
          `).join("")}
        </div>
      </div>
    `;
  }

  function getControlWorkingChartSourceStateAtWindowStart() {
    const windowBounds = getControlWorkingWindowBounds();
    const active = { HP1: false, HP2: false, boiler: false, cooling: false };
    const sourceModes = { HP1: 0, HP2: 0 };
    const sourceKeys = (subject) => {
      const normalized = String(subject || "").toUpperCase();
      if (normalized === "BOTH") {
        return ["HP1", "HP2"];
      }
      return normalized === "HP1" || normalized === "HP2" ? [normalized] : [];
    };
    const events = enrichControlWorkingDecisionLogEvents(getDecisionLogEvents())
      .filter((event) => event && !event._oq_hidden)
      .sort((left, right) => {
        const leftEpochMs = getDecisionEventEpochMs(left);
        const rightEpochMs = getDecisionEventEpochMs(right);
        return (Number.isFinite(leftEpochMs) ? leftEpochMs : Number.POSITIVE_INFINITY) -
          (Number.isFinite(rightEpochMs) ? rightEpochMs : Number.POSITIVE_INFINITY);
      });

    events.forEach((event) => {
      const epochMs = getDecisionEventEpochMs(event);
      if (!Number.isFinite(epochMs) || epochMs > windowBounds.start) {
        return;
      }
      const eventType = String(event.event_type || "");
      const contextCm = Number(event._oq_context_cm ?? event.cm);
      if (eventType === "source_start") {
        sourceKeys(event.subject).forEach((key) => {
          active[key] = true;
          sourceModes[key] = contextCm;
        });
      } else if (eventType === "source_stop") {
        sourceKeys(event.subject).forEach((key) => {
          active[key] = false;
          sourceModes[key] = 0;
        });
      } else if (eventType === "boiler_assist_start"
        || eventType === "boiler_fallback_start") {
        active.boiler = true;
      } else if (eventType === "boiler_assist_stop"
        || eventType === "boiler_fallback_stop") {
        active.boiler = false;
      }
    });
    active.cooling = ["HP1", "HP2"].some((key) => active[key] && sourceModes[key] === 5);
    return { ...active, sourceModes };
  }

  function getControlWorkingDecisionLogChartLanes(items) {
    if (!items.some((item) => item.rawDecisionEvent)) {
      return null;
    }
    const lanes = [
      { label: "HP1", tone: "running", segments: [] },
      { label: "HP2", tone: "running", segments: [] },
      { label: "CV-ketel", tone: "assist", segments: [] },
      { label: "Koeling", tone: "cooling", segments: [] },
      { label: "Ontdooien", tone: "defrost", segments: [] },
      { label: "Bescherming", tone: "limited", segments: [] },
    ];
    const byLabel = Object.fromEntries(lanes.map((lane) => [lane.label, lane]));
    const addMinuteSegment = (label, startMinute, endMinute, tone, minWidth = 0.5) => {
      if (!byLabel[label] || !Number.isFinite(startMinute)) {
        return;
      }
      const start = Math.max(0, Math.min(1440, Number(startMinute)));
      const end = Number.isFinite(endMinute)
        ? Math.max(start, Math.min(1440, Number(endMinute)))
        : start;
      const width = Math.max(minWidth, ((end - start) / 1440) * 100);
      byLabel[label].segments.push({ start: (start / 1440) * 100, width, tone });
    };
    const addEventSegment = (label, item, tone, minWidth = 0.5) => {
      const range = getControlWorkingItemMinuteRange(item);
      addMinuteSegment(label, range.start, range.end, tone, minWidth);
    };
    const sortedItems = [...items]
      .filter((item) => item.rawDecisionEvent)
      .sort((left, right) => getControlWorkingItemMinuteRange(left).start - getControlWorkingItemMinuteRange(right).start);
    const openSource = { HP1: null, HP2: null, "CV-ketel": null, Koeling: null };
    const openLane = (label, startMinute) => {
      if (openSource[label] == null) {
        openSource[label] = startMinute;
      }
    };
    const closeLane = (label, endMinute, tone = "running", minWidth = 0.8) => {
      if (openSource[label] == null) {
        return false;
      }
      addMinuteSegment(label, openSource[label], endMinute, tone, minWidth);
      openSource[label] = null;
      return true;
    };
    const closeCoolingLaneIfNoHeatPumpSource = (endMinute) => {
      if (openSource.Koeling != null && openSource.HP1 == null && openSource.HP2 == null) {
        closeLane("Koeling", endMinute, "cooling", 0.8);
      }
    };
    const openDefrost = {};
    const activeAtWindowStart = getControlWorkingChartSourceStateAtWindowStart();
    if (activeAtWindowStart.HP1) {
      openLane("HP1", 0);
    }
    if (activeAtWindowStart.HP2) {
      openLane("HP2", 0);
    }
    if (activeAtWindowStart.boiler) {
      openLane("CV-ketel", 0);
    }
    if (activeAtWindowStart.cooling) {
      openLane("Koeling", 0);
    }

    sortedItems.forEach((item) => {
      const range = getControlWorkingItemMinuteRange(item);
      const eventType = String(item.realEventType || "");
      const subject = String(item.rawDecisionEvent?.subject || "").toUpperCase();
      const contextCm = Number(item.rawDecisionEvent?._oq_context_cm ?? item.rawDecisionEvent?.cm);
      const targetSources = [];
      if (subject === "HP1" || subject === "BOTH") {
        targetSources.push("HP1");
      }
      if (subject === "HP2" || subject === "BOTH") {
        targetSources.push("HP2");
      }

      if (eventType === "source_start") {
        targetSources.forEach((label) => openLane(label, range.start));
        if (contextCm === 5) {
          openLane("Koeling", range.start);
        }
      } else if (eventType === "source_stop") {
        targetSources.forEach((label) => {
          if (!closeLane(label, range.start, "running")) {
            addEventSegment(label, item, "standby", 0.55);
          }
        });
        if (contextCm === 5 || openSource.Koeling != null) {
          closeCoolingLaneIfNoHeatPumpSource(range.start);
        }
      } else if (eventType === "topology_change") {
        if (item.rawDecisionEvent?.to === "duo") {
          openLane("HP1", range.start);
          openLane("HP2", range.start);
        } else if (item.rawDecisionEvent?.to === "single") {
          const activeSource = getControlWorkingSingleTopologySource(item.rawDecisionEvent);
          if (activeSource) {
            openLane(activeSource, range.start);
            closeLane(activeSource === "HP1" ? "HP2" : "HP1", range.start, "running", 0.8);
          } else {
            closeLane("HP2", range.start, "running", 0.8);
          }
          closeCoolingLaneIfNoHeatPumpSource(range.start);
        } else if (item.rawDecisionEvent?.to === "idle") {
          closeLane("HP1", range.start, "running", 0.8);
          closeLane("HP2", range.start, "running", 0.8);
          closeLane("Koeling", range.start, "cooling", 0.8);
        }
      } else if (eventType === "boiler_assist_start"
        || eventType === "boiler_fallback_start") {
        openLane("CV-ketel", range.start);
      } else if (eventType === "boiler_assist_stop"
        || eventType === "boiler_fallback_stop") {
        if (!closeLane("CV-ketel", range.start, "assist", 0.65)) {
          addEventSegment("CV-ketel", item, "standby", 0.65);
        }
      } else if (eventType === "candidate_blocked" || eventType === "flow_hold_start") {
        addEventSegment("Bescherming", item, "limited", 0.7);
      } else if (eventType === "flow_hold_clear") {
        const durationMinutes = Math.max(1, getControlWorkingEventDurationChartMinutes(item.rawDecisionEvent));
        addMinuteSegment("Bescherming", Math.max(0, range.start - durationMinutes), range.start, "limited", 0.7);
        if (item.rawDecisionEvent?.reason === "flow_postflow") {
          closeLane("Koeling", range.start, "cooling", 0.8);
        }
      }

      if (eventType === "defrost_seen_start") {
        openDefrost[subject || "SYSTEM"] = range.start;
      } else if (eventType === "defrost_seen_clear" && openDefrost[subject || "SYSTEM"] != null) {
        addMinuteSegment("Ontdooien", openDefrost[subject || "SYSTEM"], range.start, "defrost", 0.7);
        openDefrost[subject || "SYSTEM"] = null;
      } else if (eventType === "defrost_seen_clear" && Number(item.rawDecisionEvent?.duration_s) > 0) {
        const durationMinutes = Math.max(5, getControlWorkingEventDurationChartMinutes(item.rawDecisionEvent));
        addMinuteSegment("Ontdooien", Math.max(0, range.start - durationMinutes), range.start, "defrost", 0.7);
      }
      const protectionAlreadyMapped = eventType === "candidate_blocked" ||
        eventType === "flow_hold_start" ||
        eventType === "flow_hold_clear";
      if (!protectionAlreadyMapped &&
          (item.severity === "limited" || item.severity === "attention" || eventType === "decision_blocked" || eventType === "decision_hold")) {
        addEventSegment("Bescherming", item, item.severity === "attention" ? "assist" : "limited", 0.7);
      }
      if (eventType === "sticky_pump_run") {
        addEventSegment("Bescherming", item, "safe", 0.6);
      }
      if (eventType === "frost_protection_start") {
        addEventSegment("Bescherming", item, "limited", 0.8);
      } else if (eventType === "frost_protection_clear") {
        const durationMinutes = Math.max(1, getControlWorkingEventDurationChartMinutes(item.rawDecisionEvent));
        addMinuteSegment("Bescherming", Math.max(0, range.start - durationMinutes), range.start, "limited", 0.8);
      }
    });
    const openEndMinute = getControlWorkingOpenEndMinute();
    Object.entries(openSource).forEach(([label, startMinute]) => {
      if (startMinute != null) {
        if (startMinute <= openEndMinute) {
          addMinuteSegment(label, startMinute, openEndMinute, label === "CV-ketel" ? "assist" : label === "Koeling" ? "cooling" : "running", 0.8);
        }
      }
    });
    Object.values(openDefrost).forEach((startMinute) => {
      if (startMinute != null) {
        addMinuteSegment("Ontdooien", startMinute, Math.min(1440, startMinute + 7), "defrost", 0.7);
      }
    });

    return lanes.filter((lane) => lane.segments.length);
  }

  function getControlWorkingChartLanes(items) {
    const decisionLogLanes = getControlWorkingDecisionLogChartLanes(items);
    if (decisionLogLanes) {
      return decisionLogLanes;
    }
    return [];
  }

  function renderControlWorkingGraphsTab(selectedItem, items) {
    const graphMinute = getControlWorkingGraphMinute();
    const graphPercent = (graphMinute / 1440) * 100;
    const windowModel = getControlWorkingWindowModel();
    const graphTimeLabel = formatControlWorkingGraphCursorLabel(graphMinute, windowModel);
    const lanes = getControlWorkingChartLanes(items);
    const chartBody = lanes.length
      ? lanes.map((lane) => renderControlWorkingChartLane(lane.label, lane.tone, lane.segments)).join("")
      : renderControlWorkingEmptyState(t("controlReplay.emptyGraphTitle"), t("controlReplay.emptyGraphCopy"));
    return `
      <div class="oq-working-graphs">
        <section class="oq-working-chart-panel">
          <div class="oq-working-chart-head">
            <div>
              <span class="oq-working-eyebrow">${escapeHtml(windowModel.eyebrow)}</span>
              <h3>${t("controlReplay.graphsTitle")}</h3>
            </div>
            <p>${escapeHtml(windowModel.graphCopy)}</p>
          </div>
          <div class="oq-working-chart-axis" aria-hidden="true">
            ${windowModel.axis.map((label) => `<span>${escapeHtml(label)}</span>`).join("")}
          </div>
          <div class="oq-working-chart-body">
            <div class="oq-working-chart-control" data-oq-control-replay-scrub="true">
              <input
                class="oq-working-time-slider"
                type="range"
                min="0"
                max="1440"
                step="5"
                value="${escapeHtml(String(graphMinute))}"
                aria-label="${t("controlReplay.graphsTimeAria")}"
                data-oq-control-replay-time="true"
              >
              <span class="oq-working-chart-cursor" style="--oq-chart-left:${escapeHtml(String(graphPercent))}%;">
                <strong>${escapeHtml(graphTimeLabel)}</strong>
              </span>
            </div>
            ${chartBody}
          </div>
        </section>
        ${selectedItem ? renderControlWorkingDetails(selectedItem) : renderControlWorkingGraphEmptyDetails(graphTimeLabel)}
      </div>
    `;
  }

  function getControlWorkingSignature(current) {
    return getRenderSignature({
      tab: getControlWorkingSelectedTab(),
      window: getControlWorkingSelectedWindow(),
      periodMenuOpen: state.controlReplayPeriodMenuOpen,
      customPeriodOpen: state.controlReplayCustomPeriodOpen,
      customStart: state.controlReplayCustomStart,
      customEnd: state.controlReplayCustomEnd,
      customPeriodError: state.controlReplayCustomPeriodError,
      selected: state.controlReplaySelectedEpisode,
      supportDetailsItem: state.controlReplaySupportDetailsItemId,
      graphMinute: getControlWorkingGraphMinute(),
      mode: current.modeLabel,
      title: current.title,
      copy: current.copy,
      expectation: current.expectation,
      hp1Status: current.hp1Status,
      hp2Status: current.hp2Status,
      reason: current.primaryReason,
      hp1Running: current.hp1Running,
      hp2Running: current.hp2Running,
      hp1Starts: current.hp1Starts,
      hp2Starts: current.hp2Starts,
      hp1Hours: current.hp1Hours,
      hp2Hours: current.hp2Hours,
      cvStatus: current.cvStatus,
      strategy: current.strategyLabel,
      outside: current.outsideTemp,
      supply: current.supplyTemp,
      flow: current.flow,
      cooling: current.cooling,
      coolingProtection: current.coolingProtection,
      coolingCapped: current.coolingCapped,
      decisionLog: state.decisionLogSignature,
      decisionLogError: state.decisionLogError,
      theme: state.overviewTheme,
    });
  }

  function renderControlWorkingPanel(current, signature = getControlWorkingSignature(current)) {
    const selectedTab = getControlWorkingSelectedTab();
    const items = selectedTab === "status" ? [] : getControlWorkingDecisionLogItems();
    const selectedItem = getControlWorkingSelectedItem(items);
    const visibleItem = selectedTab === "graphs"
      ? getControlWorkingItemForMinute(items, getControlWorkingGraphMinute())
      : selectedItem;
    const body = selectedTab === "status"
      ? renderControlWorkingStatusTab(current)
      : selectedTab === "graphs"
      ? renderControlWorkingGraphsTab(visibleItem, items)
      : renderControlWorkingTimelineTab(items, visibleItem);
    const periodChoices = selectedTab === "status" ? "" : renderControlWorkingWindowChoices();
    return `
      <section class="oq-working" data-render-signature="${escapeHtml(signature)}">
        <header class="oq-working-head">
          <div class="oq-working-head-copy">
            <span class="oq-working-kicker">
              <span class="oq-working-eyebrow">${t("controlReplay.panelEyebrow")}</span>
              <span class="oq-working-beta">BETA</span>
            </span>
            <h2>${t("controlReplay.panelTitle")}</h2>
            <p>${t("controlReplay.panelCopy")}</p>
          </div>
          <div class="oq-working-head-actions">
            ${renderControlWorkingTabs()}
            ${periodChoices}
          </div>
        </header>
        ${body}
      </section>
    `;
  }

  export function renderControlReplayView() {
    const current = getControlWorkingCurrent(getHeatPumpPanels());
    return `
      <section class="oq-helper-panel oq-helper-panel--flush">
        <div class="oq-overview-board oq-overview-board--${escapeHtml(state.overviewTheme)}">
          ${renderControlWorkingPanel(current)}
        </div>
      </section>
    `;
  }

  function patchControlReplayDom() {
    if (!state.root || state.appView !== "control") {
      return false;
    }
    const board = state.root.querySelector(".oq-overview-board");
    const panel = board ? board.querySelector(".oq-working") : null;
    if (!board || !panel) {
      return false;
    }
    const activeElement = document.activeElement;
    if (activeElement && activeElement.closest("[data-oq-control-replay-period-menu]") &&
        activeElement.matches("[data-oq-control-replay-custom-input]")) {
      return true;
    }
    const nextBoardClass = `oq-overview-board oq-overview-board--${state.overviewTheme}`;
    if (board.className !== nextBoardClass) {
      board.className = nextBoardClass;
    }
    const current = getControlWorkingCurrent(getHeatPumpPanels());
    const signature = getControlWorkingSignature(current);
    return replaceOuterHtmlIfSignatureChanged(
      panel,
      signature,
      () => renderControlWorkingPanel(current, signature),
    ) || true;
  }

  setViewPatchControls({ patchControlReplayDom });
