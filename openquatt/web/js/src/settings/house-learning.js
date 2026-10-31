import { getEntityNumericValue, getEntityStateText, hasEntity } from "../core/app-shared.js";
import { formatNumericState } from "../core/formatting.js";
import { escapeHtml } from "../core/html.js";
import { formatDateTime, formatNumber, getIntlLocale, getLocale, t } from "../i18n/index.js";
import { state } from "../core/state.js";
import { renderStatCard } from "../views/stat-card.js";
import { renderNamedActionButton, renderSettingsAdvancedDisclosure, renderSettingsCompactSwitchControl, renderSettingsInfoToggle, renderSettingsSystemRow } from "./controls.js";
import { renderHouseLearningChart } from "./house-learning-chart.js";
import { getSettingsSwitchModel } from "./field-models.js";

const metric = (value, unit = "") => value != null && Number.isFinite(Number(value))
  ? `${formatNumber(value, { maximumFractionDigits: 1 })} ${unit}`.trim()
  : "—";
const REASON_LABEL_KEYS = {
  essential_source: "houseLearning.reasons.essentialSource",
  source_stale: "houseLearning.reasons.sourceStale",
  not_heating: "houseLearning.reasons.notHeating",
  boiler_heat: "houseLearning.reasons.boilerHeat",
  defrost_or_oil_return: "houseLearning.reasons.defrostOrOilReturn",
  control_mode: "houseLearning.reasons.controlMode",
  active_limit: "houseLearning.reasons.activeLimit",
  service_or_ota: "houseLearning.reasons.serviceOrOta",
  cooling: "houseLearning.reasons.cooling",
  source_uncertain: "houseLearning.reasons.sourceUncertain",
  setpoint_recovery: "houseLearning.reasons.setpointRecovery",
  external_heat_not_excluded: "houseLearning.reasons.externalHeatNotExcluded",
  persistence_unavailable: "houseLearning.reasons.persistenceUnavailable",
  not_enough_samples: "houseLearning.reasons.notEnoughSamples",
  outside_span: "houseLearning.reasons.outsideSpan",
  heat_span: "houseLearning.reasons.heatSpan",
  unobservable: "houseLearning.reasons.unobservable",
  parameter_bounds: "houseLearning.reasons.parameterBounds",
  residual_rms: "houseLearning.reasons.residualRms",
  residual_bias: "houseLearning.reasons.residualBias",
  invalid_state: "houseLearning.reasons.invalidState",
  recent_data_invalid: "houseLearning.reasons.recentDataInvalid",
  stale_model: "houseLearning.reasons.staleModel",
  model_context_mismatch: "houseLearning.reasons.modelContextMismatch",
  insufficient_shared_temperature_range: "houseLearning.reasons.insufficientSharedTemperatureRange",
  model_disagreement: "houseLearning.reasons.modelDisagreement",
  thermal_storage_not_stationary: "houseLearning.reasons.thermalStorageNotStationary",
};
const COLLECTION_REJECTION_KEYS = {
  invalid_measurement: "houseLearning.collectionRejections.invalidMeasurement",
  mixed_context: "houseLearning.collectionRejections.mixedContext",
  time_discontinuity: "houseLearning.collectionRejections.timeDiscontinuity",
  segment_ineligible: "houseLearning.collectionRejections.segmentIneligible",
  nonpositive_heat: "houseLearning.collectionRejections.nonpositiveHeat",
  setpoint_changed: "houseLearning.collectionRejections.setpointChanged",
  room_unstable: "houseLearning.collectionRejections.roomUnstable",
  water_storage_unstable: "houseLearning.collectionRejections.waterStorageUnstable",
  invalid_configuration: "houseLearning.collectionRejections.invalidConfiguration",
  stale_data: "houseLearning.collectionRejections.staleData",
};
const SOURCE_REJECTION_KEYS = {
  boiler_unavailable: "houseLearning.collectionRejections.boilerUnavailable",
  boiler_command_stale: "houseLearning.collectionRejections.boilerCommandStale",
  boiler_command_active: "houseLearning.collectionRejections.boilerCommandActive",
  boiler_active: "houseLearning.collectionRejections.boilerHeat",
  boiler_unknown: "houseLearning.collectionRejections.boilerUnavailable",
  missing_measurement: "houseLearning.collectionRejections.missingMeasurement",
  source_stale: "houseLearning.collectionRejections.sourceStale",
  time_skew: "houseLearning.collectionRejections.timeSkew",
  invalid_value: "houseLearning.collectionRejections.invalidMeasurement",
  cooling_active: "houseLearning.reasons.cooling",
  control_mode_blocked: "houseLearning.reasons.controlMode",
  service_or_ota_active: "houseLearning.reasons.serviceOrOta",
};
const MODEL_REASONS = ["model_context_mismatch", "insufficient_shared_temperature_range", "model_disagreement", "thermal_storage_not_stationary"];
const reasons = (values, fallback) => {
  const unique = [...new Set((values || []).map((value) => t(REASON_LABEL_KEYS[value] || "houseLearning.reasons.unknownBlock")))];
  return unique.length ? unique.join(" · ") : fallback;
};
const estimateNote = (value, ready) => value == null ? t("houseLearning.estimate.none") : ready ? t("houseLearning.estimate.sufficient") : t("houseLearning.estimate.provisional");
const statusIsStale = (status) => !Number.isFinite(status.tickEpoch) || status.tickEpoch <= 0
  || Math.floor(Date.now() / 1000) - status.tickEpoch > 30;
const SOURCE_LABEL_KEYS = { room: "houseLearning.sources.room", setpoint: "houseLearning.sources.setpoint", outside: "houseLearning.sources.outside", flow: "houseLearning.sources.flow" };
const ROUTE_LABEL_KEYS = {
  selected_room_temperature: "houseLearning.routes.selectedRoomTemperature",
  selected_room_setpoint: "houseLearning.routes.selectedRoomSetpoint",
  selected_outside_temperature: "houseLearning.routes.selectedOutsideTemperature",
  selected_flow: "houseLearning.routes.selectedFlow",
  "HP1/HP2 composition": "houseLearning.routes.hpComposition",
  "Synthesized zero": "houseLearning.routes.synthesizedZero",
  unknown: "houseLearning.routes.unknown",
};
const sourceLabel = (key) => t(SOURCE_LABEL_KEYS[key]);
const formatList = (values) => new Intl.ListFormat(getIntlLocale(), { style: "long", type: "conjunction" }).format(values);

function duration(seconds) {
  if (!Number.isFinite(seconds) || seconds < 0) return null;
  const rounded = Math.floor(seconds);
  const minutes = Math.floor(rounded / 60);
  const remainder = String(rounded % 60).padStart(2, "0");
  return minutes >= 60 ? `${Math.floor(minutes / 60)}:${String(minutes % 60).padStart(2, "0")}:${remainder}` : `${minutes}:${remainder}`;
}

function collectionCard(label, phase, status, waitingNote = "", showIntervals = true) {
  if (!status.enabled) return [label, t("houseLearning.status.paused"), t("houseLearning.status.passiveLearningOff"), true, ""];
  if (statusIsStale(status)) return [label, t("houseLearning.status.stale"), t("houseLearning.status.waitCurrentController"), true, "orange"];
  if (!phase) return [label, t("houseLearning.status.unknown"), t("houseLearning.status.noProgressFromFirmware"), true, "sky"];
  const elapsed = duration(phase.elapsedSeconds);
  const target = duration(phase.targetSeconds);
  const progress = elapsed && target ? `${elapsed} / ${target}` : elapsed || target || t("houseLearning.status.timeUnknown");
  const intervals = showIntervals && Number.isFinite(phase.intervals) ? ` · ${t("houseLearning.status.intervalsCompleted", { count: formatNumber(phase.intervals) })}` : "";
  if (phase.restorePending) return [label, t("houseLearning.status.restoringDay"), `${progress} · ${t("houseLearning.status.restoringDayCopy")}`, true, "orange"];
  if (phase.gapPending) return [label, t("houseLearning.status.waitShortMeasurement"), `${progress} · ${t("houseLearning.status.dailyGapHeld")}`, true, "orange"];
  if (phase.active) return [label, t("houseLearning.status.collecting"), `${progress}${intervals}${phase.resumeStatus === "restored" ? ` · ${t("houseLearning.status.dayRestored")}` : ""}`, true, "green"];
  return [label, t("houseLearning.status.waiting"), waitingNote || (elapsed && target ? `${progress}${intervals} · ${t("houseLearning.status.waitValidMeasurementLower")}` : t("houseLearning.status.waitValidMeasurement")), true, "orange"];
}

function sourceRow(key, source, status) {
  const route = ROUTE_LABEL_KEYS[source.route] ? t(ROUTE_LABEL_KEYS[source.route]) : source.route;
  if (!status.enabled) return renderSettingsSystemRow({ label: sourceLabel(key), value: t("houseLearning.status.notAssessed"), note: t("houseLearning.status.passiveLearningOff") });
  if (statusIsStale(status)) return renderSettingsSystemRow({ label: sourceLabel(key), value: t("houseLearning.status.stale"), note: t("houseLearning.status.waitCurrentController") });
  return renderSettingsSystemRow({ label: sourceLabel(key), value: source.valid ? t("houseLearning.status.valid") : t("houseLearning.status.invalid"), note: t("houseLearning.status.viaRoute", { route }) });
}

function renderWaterTemperatureCards() {
  const temperatures = [
    ["houseLearning.water.hp1In", "hp1WaterIn"],
    ["houseLearning.water.hp1Out", "hp1WaterOut"],
    ["houseLearning.water.hp2In", "hp2WaterIn"],
    ["houseLearning.water.hp2Out", "hp2WaterOut"],
  ].filter(([, key]) => hasEntity(key));
  if (!temperatures.length) return "";
  return `<div class="oq-settings-grid oq-house-learning-water">${temperatures.map(([label, key]) => {
    const value = getEntityNumericValue(key);
    return renderStatCard({ label: t(label), value: Number.isNaN(value) ? getEntityStateText(key) : formatNumericState(value, 1, "°C") });
  }).join("")}</div>`;
}

function measurementWaitingNote(status) {
  const waitingNoteKeys = {
    defrost_or_oil_return: "houseLearning.waiting.defrostOrOilReturn",
    service_or_ota: "houseLearning.waiting.serviceOrOta",
    cooling: "houseLearning.waiting.cooling",
    boiler_heat: "houseLearning.waiting.boilerHeat",
    external_heat_not_excluded: "houseLearning.waiting.externalHeatNotExcluded",
    active_limit: "houseLearning.waiting.activeLimit",
    persistence_unavailable: "houseLearning.waiting.persistenceUnavailable",
  };
  const reason = [...status.invalidReasons, ...status.blockedReasons].find((key) => waitingNoteKeys[key]);
  return reason ? t(waitingNoteKeys[reason]) : t("houseLearning.waiting.default");
}

function activity(status) {
  if (status.blockedReasons.includes("runtime_blocked") && !statusIsStale(status)) return [t("houseLearning.activity.restartRequired"), t("houseLearning.activity.restartCopy"), "orange"];
  if (!status.enabled) return [t("houseLearning.status.paused"), t("houseLearning.activity.enableToCollect"), ""];
  if (statusIsStale(status)) return [t("houseLearning.status.stale"), t("houseLearning.status.waitCurrentController"), "orange"];
  if (status.collection?.batch?.gapPending) return [t("houseLearning.status.waitShortMeasurement"), t("houseLearning.status.dailyGapHeld"), "orange"];
  if (status.collection?.batch?.active && !status.collection?.thermal?.active && (status.dailyRecordCount != null || status.collection.batch.targetSeconds === 86400)) {
    return [t("houseLearning.activity.collectingDaily"), t(status.collection?.thermal?.active === false
      ? "houseLearning.activity.dailyOnlyCopy" : "houseLearning.activity.processingMeasurement"), "green"];
  }
  if (status.sourceStatus === "series_junction_mismatch") return [t("houseLearning.activity.waterMismatch"), t("houseLearning.activity.waterMismatchCopy"), "orange"];
  if (status.status === "collecting") {
    if (status.controlMode === 0 || status.controlMode === 1) return [t("houseLearning.activity.collectingDuringPause"), t("houseLearning.activity.pauseCounts"), "green"];
    return [t("houseLearning.activity.collectingNow"), t("houseLearning.activity.processingMeasurement"), "green"];
  }
  const missing = Object.entries(status.sources).filter(([, source]) => !source.valid).map(([key]) => sourceLabel(key).toLocaleLowerCase(getIntlLocale()));
  if (missing.length) return [t("houseLearning.activity.waitingForSource"), t("houseLearning.activity.noValidSourceValue", { sources: formatList(missing) }), "orange"];
  return [t("houseLearning.activity.waitingForMeasurement"), measurementWaitingNote(status), "orange"];
}

function modelStatus(status) {
  if (!status.storageReady) return t("houseLearning.model.storageNotReady");
  if (!status.enabled) return t("houseLearning.status.notAssessedYet");
  if (statusIsStale(status)) return t("houseLearning.status.stale");
  if (status.adviceReady) return t("houseLearning.model.heatLossMatches");
  return status.rlsSamples ? t("houseLearning.estimate.provisional") : t("houseLearning.model.noR1rcData");
}

function modelStatusNote(status) {
  if (status.enabled && statusIsStale(status)) return t("houseLearning.model.noCurrentValidatedQuality");
  const modelReasons = status.blockedReasons.filter((reason) => MODEL_REASONS.includes(reason));
  if (modelReasons.length) return reasons(modelReasons, t("houseLearning.model.validationBlocked"));
  if (status.adviceReady) return t("houseLearning.model.modelsAgreeCopy");
  if (status.rlsReady && status.batchAdviceReady) return t("houseLearning.model.notJointlyValidated");
  if (status.rlsSamples) {
    return t("houseLearning.layout.moreModelData");
  }
  return status.enabled ? t("houseLearning.model.waitFirstPeriod") : t("houseLearning.model.startToAssess");
}

function localizeStateMessage(message) {
  if (!message || typeof message !== "object" || message.translationKey === undefined) return String(message || "");
  const vars = Object.fromEntries(Object.entries(message.vars || {}).map(([key, value]) => [key, localizeStateMessage(value)]));
  return t(message.translationKey, vars);
}

export function renderHouseLearningStatusMarkup(status = state.houseLearningStatus, section = "all") {
  if (section === "secondary" && (!status || state.houseLearningReset === "pending" || state.houseLearningReset === "error" || state.houseLearningReset === "uncertain")) return "";
  if (state.houseLearningReset === "pending") {
    return `<p class="oq-settings-action-note" role="status">${escapeHtml(t("houseLearning.reset.clearing"))}</p>`;
  }
  if (state.houseLearningReset === "error" || state.houseLearningReset === "uncertain") {
    return `<p class="oq-settings-action-note oq-settings-action-note--error" role="alert">${escapeHtml(localizeStateMessage(state.houseLearningResetError))}</p>`;
  }
  if (!status) return state.houseLearningEndpointChecking && !state.houseLearningEndpointChecked
    ? `<p class="oq-settings-action-note" role="status">${escapeHtml(t("houseLearning.status.checkingAvailability"))}</p>`
    : state.houseLearningStatusError
    ? `<p class="oq-settings-action-note oq-settings-action-note--error">${escapeHtml(localizeStateMessage(state.houseLearningStatusError))}</p>`
    : "";
  const [activityValue, activityNote] = activity(status);
  const invalidSources = Object.entries(status.sources).filter(([, source]) => !source.valid).map(([key]) => sourceLabel(key).toLocaleLowerCase(getIntlLocale()));
  const dailyCollection = status.dailyRecordCount != null || status.collection?.batch?.targetSeconds === 86400;
  const batchWaiting = dailyCollection ? t("houseLearning.status.waitDailyMeasurement")
    : invalidSources.length ? t("houseLearning.status.waitForSources", { sources: formatList(invalidSources) })
    : status.invalidReasons.includes("setpoint_recovery") ? t("houseLearning.status.waitStableAfterSetpoint")
    : t("houseLearning.status.waitStableHeatingMeasurement");
  const lastRejection = status.collection?.batchLastRejection;
  const sourceRejection = lastRejection === "invalid_measurement" ? status.collection?.batchLastSourceStatus : null;
  const rejectionKey = Object.hasOwn(SOURCE_REJECTION_KEYS, sourceRejection)
    ? SOURCE_REJECTION_KEYS[sourceRejection]
    : Object.hasOwn(COLLECTION_REJECTION_KEYS, lastRejection)
      ? COLLECTION_REJECTION_KEYS[lastRejection] : "houseLearning.collectionRejections.unknown";
  const rejectionNote = lastRejection
    ? `<p class="oq-settings-action-note">${escapeHtml(t("houseLearning.status.lastStablePeriodRejection", {
      reason: t(rejectionKey),
    }))}</p>`
    : "";
  const daily = collectionCard(t(dailyCollection ? "houseLearning.cards.houseLine" : "houseLearning.cards.legacyHouseLine"), status.collection?.batch, status, batchWaiting);
  const thermal = collectionCard(t("houseLearning.cards.heatingCooling"), status.collection?.thermal, status, measurementWaitingNote(status), false);
  const phase = status.collection?.batch;
  const current = status.enabled && !statusIsStale(status) && Number.isFinite(phase?.elapsedSeconds) && phase.elapsedSeconds >= 0 && phase.targetSeconds > 0;
  const fraction = current ? Math.max(0, Math.min(100, phase.elapsedSeconds / phase.targetSeconds * 100)) : 0;
  const modelNote = `<p class="oq-settings-action-note">${escapeHtml(t("houseLearning.cards.modelStatus"))}: <strong>${escapeHtml(modelStatus(status))}</strong> · ${escapeHtml(modelStatusNote(status))}</p>`;
  const modelCards = [
    [t("houseLearning.cards.r1rcPeriods"), formatNumber(status.rlsSamples), t("houseLearning.cards.acceptedThirtyMinutePeriods")],
    [t("houseLearning.cards.heatLossU"), metric(status.uRls, "W/K"), estimateNote(status.uRls, status.rlsReady)],
    [t("houseLearning.cards.heatStorageC"), metric(status.cRlsWhPerK, "Wh/K"), status.cRlsWhPerK == null ? t("houseLearning.estimate.none") : t("houseLearning.estimate.provisionalNotValidated")],
  ];
  const legacyCount = status.legacyRecordCount ?? (status.dailyRecordCount == null ? status.records : null);
  const historyNote = legacyCount > 0
    ? `<p class="oq-settings-action-note">${escapeHtml(t("houseLearning.cards.legacyRecords", { count: formatNumber(legacyCount) }))}</p>`
    : "";
  const collectionNote = dailyCollection ? t(status.collection?.batch?.resumeStatus != null ? "houseLearning.status.dailyRestartWindowCopy" : "houseLearning.status.dailyWindowCopy") : "";
  const batchDiagnosticCards = [
    [t("houseLearning.cards.batchHeatLossH"), metric(status.hBatch, "W/K"), t(dailyCollection ? "houseLearning.cards.batchHeatLossCopy" : "houseLearning.cards.stableBatchPeriodsCopy")],
    [t("houseLearning.cards.batchStartTemperature"), metric(status.t0Batch, "°C"), t("houseLearning.cards.batchStartTemperatureCopy")],
  ];
  const thermalHelp = [
    t(dailyCollection ? "houseLearning.diagnostics.copy" : "houseLearning.diagnostics.legacyCopy"),
    collectionCard(t("houseLearning.cards.heatingCooling"), status.collection?.thermal, status, measurementWaitingNote(status))[2],
    status.rlsSamples ? t(status.rlsSamples === 1 ? "houseLearning.model.acceptedPeriodOne" : "houseLearning.model.acceptedPeriodMany", { count: formatNumber(status.rlsSamples) }) : "",
  ].filter(Boolean).join("\n\n");
  const primary = `<div class="oq-house-learning-current">
    <div class="oq-house-learning-current-head"><div><h4>${escapeHtml(daily[0])}</h4><strong>${escapeHtml(daily[1])}</strong>${renderSettingsInfoToggle("house-learning-window", daily[0], collectionNote)}</div><div class="oq-house-learning-count"><span>${escapeHtml(t("houseLearning.cards.dailyPeriods"))}</span><strong>${status.dailyRecordCount == null ? "—" : formatNumber(status.dailyRecordCount)}</strong>${status.dailyRecordCount == null ? `<small>${escapeHtml(t("houseLearning.cards.dailyCountUnavailable"))}</small>` : ""}</div></div>
    ${status.enabled ? `<p class="oq-house-learning-current-time">${escapeHtml(daily[2])}</p>` : ""}
    ${current ? `<div class="oq-house-learning-readiness-track" aria-hidden="true"><span style="width:${fraction}%"></span></div>` : ""}
    <p class="oq-house-learning-current-note">${activityValue !== daily[1] ? `<strong>${escapeHtml(activityValue)}</strong> · ` : ""}${escapeHtml(activityNote)}</p>
    ${rejectionNote}
  </div>${state.houseLearningStatusError ? `<p class="oq-settings-action-note oq-settings-action-note--error">${escapeHtml(localizeStateMessage(state.houseLearningStatusError))}</p>` : ""}`;
  if (section === "primary") return primary;
  const secondary = `<section class="oq-house-learning-thermal"><div class="oq-house-learning-section-head"><h4>${escapeHtml(t("houseLearning.cards.heatingCooling"))}</h4>${renderSettingsInfoToggle("house-learning-thermal", t("houseLearning.cards.heatingCooling"), thermalHelp)}</div><p class="oq-house-learning-thermal-progress"><strong>${escapeHtml(thermal[1])}</strong> · ${escapeHtml(thermal[2])}</p>${modelNote}<div class="oq-settings-grid oq-house-learning-model">${modelCards.map(([label, value, note]) => renderStatCard({ label, value, note })).join("")}</div></section>
    ${renderSettingsAdvancedDisclosure("house-learning-technical", t("houseLearning.layout.technical"), "", `<div class="oq-settings-system-summary oq-house-learning-sources">${Object.entries(status.sources).map(([key, source]) => sourceRow(key, source, status)).join("")}</div>${renderWaterTemperatureCards()}<div class="oq-settings-grid oq-house-learning-parameters">${batchDiagnosticCards.map(([label, value, note]) => renderStatCard({ label, value, note })).join("")}</div>${historyNote}`)}`;
  return section === "secondary" ? secondary : primary + secondary;
}

function houseLearningChartSignature(busy = Boolean(state.busyAction) || state.houseLearningReset === "pending") {
  return JSON.stringify([getLocale(), busy, state.houseLearningChartFetchedAt, state.houseLearningChart?.length,
    state.houseLearningChartLoading, state.houseLearningChartError,
    state.houseLearningStatus?.hBatch, state.houseLearningStatus?.t0Batch, state.houseLearningStatus?.batchAdviceReady, state.houseLearningStatus?.referenceRoomC,
    state.houseLearningStatus?.batchStatus,
    ...["houseColdTemp", "houseOutdoorMax", "housePower"].map(getEntityNumericValue)]);
}

function renderHouseLearningDataActions(busy) {
  const resetting = state.busyAction === "houseLearningReset" || state.houseLearningReset === "pending";
  const reset = hasEntity("houseLearningReset")
    ? renderNamedActionButton("houseLearningReset", t(resetting ? "houseLearning.reset.clearingButton" : "houseLearning.reset.button"), "oq-helper-button oq-helper-button--ghost", busy)
    : "";
  return `<div class="oq-helper-actions oq-house-learning-actions"><button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="download-house-learning" ${busy ? "disabled" : ""}>${escapeHtml(t(state.busyAction === "houseLearningExport" ? "houseLearning.export.downloading" : "houseLearning.export.button"))}</button>${reset}</div>`;
}

function renderHouseLearningChartPanel(busy = Boolean(state.busyAction) || state.houseLearningReset === "pending") {
  const open = state.houseLearningChart !== null || Boolean(state.houseLearningChartError);
  const chart = state.houseLearningChart !== null ? renderHouseLearningChart(
    state.houseLearningChart,
    { coldC: getEntityNumericValue("houseColdTemp"), zeroC: getEntityNumericValue("houseOutdoorMax"), ratedW: getEntityNumericValue("housePower") },
    { h: state.houseLearningStatus?.hBatch, t0: state.houseLearningStatus?.t0Batch, ready: state.houseLearningStatus?.batchAdviceReady, referenceRoomC: state.houseLearningStatus?.referenceRoomC, batchStatus: state.houseLearningStatus?.batchStatus },
  ) : "";
  const actionLabel = t(state.houseLearningChartLoading ? "houseLearning.chart.loading" : open ? "houseLearning.chart.hide" : "houseLearning.chart.show");
  return `<button class="oq-house-learning-chart-head" type="button" data-oq-action="load-house-learning-chart" aria-label="${escapeHtml(actionLabel)}" aria-expanded="${open}" ${state.houseLearningChartLoading || busy ? "disabled" : ""}>
    <span class="oq-house-learning-disclosure-copy"><strong>${escapeHtml(t("houseLearning.chart.panelTitle"))}</strong><span>${escapeHtml(t("houseLearning.chart.panelCopy"))}</span></span>
    <span class="oq-house-learning-disclosure-action">${escapeHtml(t(state.houseLearningChartLoading ? "houseLearning.chart.loading" : open ? "houseLearning.layout.close" : "houseLearning.layout.open"))}<svg viewBox="0 0 20 20" width="18" height="18" aria-hidden="true"><path d="m5 7.5 5 5 5-5"/></svg></span>
  </button>
  ${state.houseLearningChartError ? `<p class="oq-settings-action-note oq-settings-action-note--error">${escapeHtml(localizeStateMessage(state.houseLearningChartError))}</p>` : ""}
  ${chart}${open && state.houseLearningChartFetchedAt ? `<p class="oq-house-learning-chart-freshness">${escapeHtml(t("houseLearning.chart.loadedAt", { date: formatDateTime(state.houseLearningChartFetchedAt, { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" }) }))}</p>` : ""}
  ${open ? `<div data-oq-house-learning-secondary>${renderHouseLearningStatusMarkup(state.houseLearningStatus, "secondary")}</div>${renderHouseLearningDataActions(busy)}` : ""}`;
}

export function renderHouseLearningSettings() {
  const initialCapabilityCheck = state.houseLearningEndpointChecking && !state.houseLearningEndpointChecked;
  if (!hasEntity("houseLearningEnabled") || (!state.houseLearningEndpointAvailable && !initialCapabilityCheck)) return "";
  const resetting = state.busyAction === "houseLearningReset" || state.houseLearningReset === "pending";
  const busy = Boolean(state.busyAction) || resetting;
  const learningSwitch = getSettingsSwitchModel("houseLearningEnabled");
  return `
    <section class="oq-settings-section" data-oq-house-learning>
      <div class="oq-settings-section-head">
        <div class="oq-settings-section-head-meta">
          <p class="oq-helper-label">${escapeHtml(t("houseLearning.panel.eyebrow"))}</p>
          <span class="oq-settings-section-badge oq-settings-section-badge--experimental">${escapeHtml(t("houseLearning.panel.experimental"))}</span>
        </div>
        <div class="oq-house-learning-heading"><h3>${escapeHtml(t("houseLearning.panel.title"))}</h3>${renderSettingsCompactSwitchControl("houseLearningEnabled", t("houseLearning.switch.label"), learningSwitch.enabled, busy || learningSwitch.busy, t("common.on"), t("common.off"))}</div>
        <p>${escapeHtml(t("houseLearning.panel.copy"))}</p>
      </div>
      <div class="oq-settings-section-body">
        <div data-oq-house-learning-status>${renderHouseLearningStatusMarkup(state.houseLearningStatus, "primary")}</div>
        <div class="oq-house-learning-chart-panel" data-oq-house-learning-chart-panel data-oq-chart-signature="${escapeHtml(houseLearningChartSignature(busy))}">${renderHouseLearningChartPanel(busy)}</div>
      </div>
    </section>`;
}

export function patchHouseLearningSettingsStatus() {
  const panel = state.root?.querySelector("[data-oq-house-learning]");
  const initialCapabilityCheck = state.houseLearningEndpointChecking && !state.houseLearningEndpointChecked;
  if (panel && (!hasEntity("houseLearningEnabled") || (!state.houseLearningEndpointAvailable && !initialCapabilityCheck))) {
    panel.remove?.();
    return true;
  }
  const node = panel?.querySelector("[data-oq-house-learning-status]");
  if (!node) return false;
  const chartNode = panel?.querySelector("[data-oq-house-learning-chart-panel]");
  if (chartNode) {
    const signature = houseLearningChartSignature();
    if (chartNode.getAttribute("data-oq-chart-signature") !== signature) {
      chartNode.innerHTML = renderHouseLearningChartPanel();
      chartNode.setAttribute("data-oq-chart-signature", signature);
    }
  }
  const markup = renderHouseLearningStatusMarkup(state.houseLearningStatus, "primary");
  if (node.innerHTML !== markup) node.innerHTML = markup;
  const secondary = panel?.querySelector("[data-oq-house-learning-secondary]");
  if (secondary) {
    const secondaryMarkup = renderHouseLearningStatusMarkup(state.houseLearningStatus, "secondary");
    if (secondary.innerHTML !== secondaryMarkup) secondary.innerHTML = secondaryMarkup;
  }
  return true;
}
