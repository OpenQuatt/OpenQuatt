import { getEntityNumericValue, getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";
import { parseLooseNumber } from "../core/entity-store.js";
import { formatNumericState } from "../core/formatting.js";
import { escapeHtml } from "../core/html.js";
import { formatDateTime, formatNumber, getIntlLocale, getLocale, t } from "../i18n/index.js";
import { state } from "../core/state.js";
import { renderStatCard } from "../views/stat-card.js";
import { renderNamedActionButton, renderSettingsAdvancedDisclosure, renderSettingsCompactSwitchControl, renderSettingsInfoToggle, renderSettingsSystemRow } from "./controls.js";
import { configuredLinePowerAt, houseLearningFitKey, learnedLinePowerAt, renderHouseLearningChart } from "./house-learning-chart.js";
import { selectDisplayedHouseLine } from "./house-learning-readiness.js";
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
const RESUME_REASON_KEYS = {
  ...COLLECTION_REJECTION_KEYS,
  ...SOURCE_REJECTION_KEYS,
  checkpoint_missing: "houseLearning.resumeReasons.missing",
  checkpoint_expired: "houseLearning.resumeReasons.expired",
  checkpoint_tick_discontinuity: "houseLearning.collectionRejections.timeDiscontinuity",
  checkpoint_heat_time_invalid: "houseLearning.resumeReasons.invalid",
  checkpoint_invalid: "houseLearning.resumeReasons.invalid",
  checkpoint_context_unavailable: "houseLearning.resumeReasons.context",
  checkpoint_context_mismatch: "houseLearning.resumeReasons.context",
  checkpoint_decode_failed: "houseLearning.resumeReasons.decode",
  checkpoint_quality_invalid: "houseLearning.resumeReasons.quality",
  context_changed: "houseLearning.collectionRejections.mixedContext",
  restart_unconfirmed: "houseLearning.resumeReasons.unconfirmed",
  unexpected_restart: "houseLearning.resumeReasons.unconfirmed",
  paused: "houseLearning.resumeReasons.paused",
};
const reasons = (values, fallback) => {
  const unique = [...new Set((values || []).map((value) => t(REASON_LABEL_KEYS[value] || "houseLearning.reasons.unknownBlock")))];
  return unique.length ? unique.join(" · ") : fallback;
};
const estimateNote = (value, ready) => value == null ? t("houseLearning.estimate.none") : ready ? t("houseLearning.estimate.sufficient") : t("houseLearning.estimate.provisional");
const statusIsStale = (status) => {
  const age = Math.floor(Date.now() / 1000) - status.tickEpoch;
  // The controller and the browser use separate clocks.
  return !Number.isFinite(status.tickEpoch) || status.tickEpoch <= 0 || Math.abs(age) > 30;
};

const canAssessModel = (status) => status?.enabled === true && status.storageReady && !statusIsStale(status)
  && !state.houseLearningStatusError && ["no_compatible_history", "cleared", "restored", "saved_verified"].includes(status.journalStatus)
  && !status.blockedReasons.includes("runtime_blocked");

function learningStatusPending(status = state.houseLearningStatus) {
  return status?.storageReady && hasEntity("houseLearningEnabled") && status.enabled !== isEntityActive("houseLearningEnabled");
}

function houseLearningLine(status = state.houseLearningStatus) {
  return { h: status?.hBatch, t0: status?.t0Batch,
    ready: canAssessModel(status) && !learningStatusPending(status) && status.batchAdviceReady,
    referenceRoomC: status?.referenceRoomC, batchStatus: status?.batchStatus,
    recordCount: state.houseLearningChartFit === houseLearningFitKey(status) ? status?.records : undefined,
    fit: status?.batchFit, preview: status?.batchPreview };
}

function renderCurrentLearningStatus() {
  if (!state.houseLearningReset && (learningStatusPending()
      || (!state.houseLearningStatus && state.busyAction === "switch-houseLearningEnabled"))) {
    return `<p class="oq-settings-action-note" role="status">${escapeHtml(t("learning.awaitingStatus"))}</p>`;
  }
  return renderHouseLearningStatusMarkup(state.houseLearningStatus, "primary");
}

const SHADOW_REASON_KEYS = {
  unavailable: "houseLearning.shadow.reasons.unavailable",
  batch_not_ready: "houseLearning.shadow.reasons.batchNotReady",
  context_mismatch: "houseLearning.shadow.reasons.contextMismatch",
  outside_range: "houseLearning.shadow.reasons.outsideRange",
  invalid_candidate: "houseLearning.shadow.reasons.invalidCandidate",
  inactive: "houseLearning.shadow.reasons.inactive",
  startup: "houseLearning.shadow.reasons.startup",
  warmup_active: "houseLearning.shadow.reasons.warmupActive",
  external_override: "houseLearning.shadow.reasons.externalOverride",
  stale_frame: "houseLearning.shadow.reasons.staleFrame",
  source_changed: "houseLearning.shadow.reasons.sourceChanged",
  settings_changed: "houseLearning.shadow.reasons.settingsChanged",
  service_or_ota: "houseLearning.shadow.reasons.serviceOrOta",
  reset_pending: "houseLearning.shadow.reasons.resetPending",
  invalid_inputs: "houseLearning.shadow.reasons.invalidInputs",
};

let shadowExpiryTimer = null;

function getShadowLineEstimate(status) {
  if (!status.shadow.lineEstimate || !status.batchAdviceReady || !status.sources.outside.valid) return null;
  const confirmedNumber = (key) => parseLooseNumber(state.entities[key]?.value ?? state.entities[key]?.state);
  const outsideC = confirmedNumber("outsideTempSelected");
  const coldC = confirmedNumber("houseColdTemp");
  const zeroC = confirmedNumber("houseOutdoorMax");
  const ratedW = confirmedNumber("housePower");
  const { hBatch: h, t0Batch: t0 } = status;
  if (![outsideC, coldC, zeroC, ratedW, h, t0].every(Number.isFinite) || zeroC <= coldC || ratedW <= 0 || h <= 0) return null;
  return { outsideC, manualW: configuredLinePowerAt({ coldC, zeroC, ratedW }, outsideC), candidateW: learnedLinePowerAt({ h, t0 }, outsideC) };
}

export function renderHouseLearningShadow(status = state.houseLearningStatus) {
  if (!status?.shadow) return "";
  const shadow = status.shadow;
  const pending = state.houseLearningReset === "pending" || state.houseLearningReset === "uncertain";
  const current = canAssessModel(status) && !learningStatusPending(status) && !pending && !state.busyAction;
  const remaining = shadow.expiresAt - performance.now();
  const available = current && status.batchAdviceReady && shadow.available && remaining > 0;
  if (status === state.houseLearningStatus) {
    window.clearTimeout(shadowExpiryTimer);
    if (available) {
      shadowExpiryTimer = window.setTimeout(() => {
        if (state.houseLearningStatus !== status) return;
        const node = state.root?.querySelector("[data-oq-house-learning-shadow]");
        if (node) patchLearningMarkup(node, renderHouseLearningShadow());
      }, Math.ceil(remaining));
    }
  }
  const reason = pending ? "reset_pending" : !status.enabled ? "inactive"
    : !current || (shadow.available && remaining <= 0) ? "stale_frame" : shadow.available ? "batch_not_ready" : shadow.reason;
  const estimate = reason === "outside_range" ? getShadowLineEstimate(status) : null;
  const values = [["houseLearning.shadow.manual", estimate?.manualW ?? shadow.manualW],
    ["houseLearning.shadow.candidate", estimate?.candidateW ?? shadow.candidateW]];
  if (!estimate) values.push(["houseLearning.shadow.delta", shadow.deltaW]);
  const copy = estimate ? t("houseLearning.shadow.lineEstimate", { temperature: metric(estimate.outsideC, "°C") })
    : t(available ? "houseLearning.shadow.copy" : SHADOW_REASON_KEYS[reason] || "houseLearning.shadow.reasons.unavailable");
  return `<section class="oq-learning-shadow" aria-label="${escapeHtml(t("houseLearning.shadow.title"))}">
    <div class="oq-house-learning-section-head"><h5>${escapeHtml(t("houseLearning.shadow.title"))}</h5>
      ${renderSettingsInfoToggle("house-learning-shadow", t("houseLearning.shadow.title"), t(estimate ? "houseLearning.shadow.lineEstimateHelp" : "houseLearning.shadow.help"))}</div>
    <p>${escapeHtml(copy)}</p>
    ${available || estimate ? `<div class="oq-settings-grid oq-house-learning-model${estimate ? " oq-house-learning-model--line-estimate" : ""}">${values.map(([key, value], index) => {
      const formatted = estimate ? `≈ ${metric(value / 1000, "kW")}`
        : available ? `${index === 2 && value > 0 ? "+" : ""}${formatNumber(value, { maximumFractionDigits: 0 })} W` : "—";
      return renderStatCard({ label: t(key), value: formatted });
    }).join("")}</div>` : ""}
  </section>`;
}

function measurementProgress(phase, status) {
  if (!status.enabled || statusIsStale(status) || !Number.isFinite(phase?.elapsedSeconds)
      || phase.elapsedSeconds < 0 || !Number.isFinite(phase.targetSeconds) || phase.targetSeconds <= 0) return "";
  const percent = Math.min(100, phase.elapsedSeconds / phase.targetSeconds * 100);
  return `<div class="oq-house-learning-readiness-track" aria-hidden="true"><span style="width:${percent}%"></span></div>`;
}

function modelAssessment(status, available, ready) {
  if (!available) return t("houseLearning.estimate.none");
  if (!canAssessModel(status)) return t("learning.previous");
  return t(ready ? "learning.assessed" : "houseLearning.estimate.provisional");
}
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

function renderHouseLearningTechnical(status, dailyCollection) {
  const legacyCount = status.legacyRecordCount ?? (status.dailyRecordCount == null ? status.records : null);
  const historyNote = legacyCount > 0
    ? `<p class="oq-settings-action-note">${escapeHtml(t("houseLearning.cards.legacyRecords", { count: formatNumber(legacyCount) }))}</p>`
    : "";
  const batchDiagnosticCards = [
    [t("houseLearning.cards.batchHeatLossH"), metric(status.hBatch, "W/K"), t(dailyCollection ? "houseLearning.cards.batchHeatLossCopy" : "houseLearning.cards.stableBatchPeriodsCopy")],
    [t("houseLearning.cards.batchStartTemperature"), metric(status.t0Batch, "°C"), t("houseLearning.cards.batchStartTemperatureCopy")],
  ];
  return renderSettingsAdvancedDisclosure("house-learning-technical", t("houseLearning.layout.technical"), "", `<div class="oq-settings-system-summary oq-house-learning-sources">${Object.entries(status.sources).map(([key, source]) => sourceRow(key, source, status)).join("")}</div>${renderWaterTemperatureCards()}<div class="oq-settings-grid oq-house-learning-parameters">${batchDiagnosticCards.map(([label, value, note]) => renderStatCard({ label, value, note })).join("")}</div>${historyNote}`);
}

export function renderHouseLearningStatusMarkup(status = state.houseLearningStatus, section = "all") {
  if ((section === "secondary" || section === "technical") && (!status || learningStatusPending(status)
      || ["pending", "error", "uncertain"].includes(state.houseLearningReset))) {
    return section === "secondary" && !state.houseLearningReset
      ? `<p class="oq-settings-action-note" role="status">${escapeHtml(t("houseLearning.diagnostics.waiting"))}</p>`
      : "";
  }
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
  if (section === "technical") return renderHouseLearningTechnical(status, dailyCollection);
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
  const resumeReason = phase?.resumeReason;
  const resumeNote = current && phase.resumeStatus === "discarded"
    ? `<p class="oq-settings-action-note">${escapeHtml(t("houseLearning.status.dayDiscarded", {
      reason: t(Object.hasOwn(RESUME_REASON_KEYS, resumeReason) ? RESUME_REASON_KEYS[resumeReason] : "houseLearning.resumeReasons.unknown"),
    }))}</p>` : "";
  const uncertaintyNote = current && phase.missingEnergyUncertaintyWh >= 0
    && Number.isFinite(phase.missingEnergyUncertaintyWh) && phase.missingEnergyBudgetWh > 0
    && Number.isFinite(phase.missingEnergyBudgetWh)
    ? `<p class="oq-settings-action-note">${escapeHtml(t(phase.budgetScope === "model_advice_only"
      ? "houseLearning.status.gapBudgetModelAdvice" : "houseLearning.status.gapBudget", {
      used: formatNumber(phase.missingEnergyUncertaintyWh, { maximumFractionDigits: 1 }),
      budget: formatNumber(phase.missingEnergyBudgetWh, { maximumFractionDigits: 1 }),
    }))}</p>` : "";
  const modelNote = `<p>${escapeHtml(t("houseLearning.cards.modelStatus"))}: <strong>${escapeHtml(modelStatus(status))}</strong> · ${escapeHtml(modelStatusNote(status))}</p>`;
  const modelCards = [
    [t("houseLearning.cards.r1rcPeriods"), formatNumber(status.rlsSamples), t("houseLearning.cards.acceptedThirtyMinutePeriods")],
    [t("houseLearning.cards.heatLossU"), metric(status.uRls, "W/K"), estimateNote(status.uRls, status.rlsReady)],
    [t("houseLearning.cards.heatStorageC"), metric(status.cRlsWhPerK, "Wh/K"), status.cRlsWhPerK == null ? t("houseLearning.estimate.none") : t("houseLearning.estimate.provisionalNotValidated")],
  ];
  const collectionNote = dailyCollection ? t(status.collection?.batch?.budgetScope === "model_advice_only"
    ? "houseLearning.status.dailyQualityWindowCopy"
    : status.collection?.batch?.resumeStatus != null ? "houseLearning.status.dailyRestartWindowCopy" : "houseLearning.status.dailyWindowCopy") : "";
  const thermalHelp = [
    t(dailyCollection ? "houseLearning.diagnostics.copy" : "houseLearning.diagnostics.legacyCopy"),
    collectionCard(t("houseLearning.cards.heatingCooling"), status.collection?.thermal, status, measurementWaitingNote(status))[2],
    status.rlsSamples ? t(status.rlsSamples === 1 ? "houseLearning.model.acceptedPeriodOne" : "houseLearning.model.acceptedPeriodMany", { count: formatNumber(status.rlsSamples) }) : "",
  ].filter(Boolean).join("\n\n");
  const learnedLine = houseLearningLine(status);
  const dailyLine = selectDisplayedHouseLine(learnedLine);
  const dailyModel = modelAssessment(status, dailyLine.h > 0 && Number.isFinite(dailyLine.t0), learnedLine.ready);
  // RLS readiness does not independently validate heat capacity. Keep this model
  // provisional until the firmware can provide that separate validation.
  const thermalModel = modelAssessment(status, status.uRls > 0 && status.cRlsWhPerK > 0,
    status.rlsReady && status.capacityValidated && !status.rlsReadinessReasons.length);
  const primary = `<div class="oq-learning-models">
  <div class="oq-house-learning-current">
    <div class="oq-house-learning-current-head"><div><h4>${escapeHtml(daily[0])}</h4><strong>${escapeHtml(daily[1])}</strong>${renderSettingsInfoToggle("house-learning-window", daily[0], collectionNote)}</div><div class="oq-house-learning-count"><span>${escapeHtml(t("houseLearning.cards.dailyPeriods"))}</span><strong>${status.dailyRecordCount == null ? "—" : formatNumber(status.dailyRecordCount)}</strong>${status.dailyRecordCount == null ? `<small>${escapeHtml(t("houseLearning.cards.dailyCountUnavailable"))}</small>` : ""}</div></div>
    <p class="oq-house-learning-current-note">${escapeHtml(t("learning.dailyPurpose"))}</p>
    ${status.enabled ? `<p class="oq-house-learning-current-time">${escapeHtml(daily[2])}</p>` : ""}
    ${measurementProgress(phase, status)}
    <p class="oq-house-learning-current-note">${escapeHtml(t("learning.modelAssessment", { status: dailyModel }))}</p>
    ${rejectionNote}${resumeNote}${uncertaintyNote}
  </div>
  <div class="oq-house-learning-current">
    <div class="oq-house-learning-current-head"><div><h4>${escapeHtml(thermal[0])}</h4><strong>${escapeHtml(thermal[1])}</strong></div><div class="oq-house-learning-count"><span>${escapeHtml(t("learning.thermalCount"))}</span><strong>${formatNumber(status.rlsSamples)}</strong></div></div>
    <p class="oq-house-learning-current-note">${escapeHtml(t("learning.dynamicPurpose"))}</p>
    ${status.enabled ? `<p class="oq-house-learning-current-time">${escapeHtml(thermal[2])}</p>` : ""}
    ${measurementProgress(status.collection?.thermal, status)}
    <p class="oq-house-learning-current-note">${escapeHtml(t("learning.modelAssessment", { status: thermalModel }))}</p>
  </div></div>
  ${status.enabled || status.blockedReasons.includes("runtime_blocked") ? `<p class="oq-house-learning-current-note"><strong>${escapeHtml(activityValue)}</strong> · ${escapeHtml(activityNote)}</p>` : ""}${state.houseLearningStatusError ? `<p class="oq-settings-action-note oq-settings-action-note--error">${escapeHtml(localizeStateMessage(state.houseLearningStatusError))}</p>` : ""}`;
  if (section === "primary") return primary;
  const secondary = `<section class="oq-house-learning-thermal"><div class="oq-house-learning-section-head"><h5>${escapeHtml(t("houseLearning.cards.heatingCooling"))}</h5>${renderSettingsInfoToggle("house-learning-thermal", t("houseLearning.cards.heatingCooling"), thermalHelp)}</div>${modelNote}<p><strong>${escapeHtml(thermal[1])}</strong> · ${escapeHtml(thermal[2])}</p><div class="oq-settings-grid oq-house-learning-model">${modelCards.map(([label, value, note]) => renderStatCard({ label, value, note })).join("")}</div></section>`;
  return section === "secondary" ? secondary : primary + secondary + renderHouseLearningTechnical(status, dailyCollection);
}

function houseLearningChartSignature(busy = Boolean(state.busyAction) || state.houseLearningReset === "pending") {
  return JSON.stringify([getLocale(), busy, state.houseLearningChartFetchedAt, state.houseLearningChart?.length,
    state.houseLearningChartLoading, state.houseLearningChartError, state.houseLearningChartBandMode,
    Boolean(state.settingsAdvancedOpen?.["house-learning-readiness"]),
    state.houseLearningStatus?.records === 0 && state.houseLearningStatus.journalStatus === "cleared",
    houseLearningLine(),
    ...["houseColdTemp", "houseOutdoorMax", "housePower"].map(getEntityNumericValue)]);
}

function renderHouseLearningDataActions(busy, resetOnly = false) {
  const resetting = state.busyAction === "houseLearningReset" || state.houseLearningReset === "pending";
  const reset = hasEntity("houseLearningReset")
    ? renderNamedActionButton("houseLearningReset", t(resetting ? "houseLearning.reset.clearingButton" : "houseLearning.reset.button"), "oq-helper-button oq-helper-button--ghost", busy)
    : "";
  if (resetOnly) return `<div class="oq-helper-actions oq-house-learning-actions">${reset}</div>`;
  return `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="download-house-learning" ${busy ? "disabled" : ""}>${escapeHtml(t(state.busyAction === "houseLearningExport" ? "houseLearning.export.downloading" : "houseLearning.export.button"))}</button>`;
}

// The measurement card is always open: the chart loads by itself, the button reloads it.
function renderHouseLearningChartPanel(busy = Boolean(state.busyAction) || state.houseLearningReset === "pending") {
  const status = state.houseLearningStatus;
  const records = state.houseLearningChart ?? (status?.records === 0 && status.journalStatus === "cleared" ? [] : null);
  const chart = records !== null ? renderHouseLearningChart(
    records,
    { coldC: getEntityNumericValue("houseColdTemp"), zeroC: getEntityNumericValue("houseOutdoorMax"), ratedW: getEntityNumericValue("housePower") },
    houseLearningLine(),
    state.houseLearningChartBandMode,
  ) : "";
  return `<div class="oq-settings-section-head">
    <div class="oq-house-learning-heading"><h3>${escapeHtml(t("houseLearning.chart.panelTitle"))}</h3><button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="load-house-learning-chart" ${state.houseLearningChartLoading || busy ? "disabled" : ""}>${escapeHtml(t("houseLearning.chart.refresh"))}</button></div>
    <p>${escapeHtml(t("houseLearning.chart.panelCopy"))}</p>
  </div>
  <div class="oq-house-learning-chart-panel">
  ${state.houseLearningChartError ? `<p class="oq-settings-action-note oq-settings-action-note--error">${escapeHtml(localizeStateMessage(state.houseLearningChartError))}</p>` : ""}
  ${records === null && !state.houseLearningChartError ? `<p class="oq-settings-action-note" role="status">${escapeHtml(t("houseLearning.chart.loading"))}</p>` : ""}
  ${chart}${state.houseLearningChart !== null && state.houseLearningChartFetchedAt ? `<p class="oq-house-learning-chart-freshness">${escapeHtml(t("houseLearning.chart.loadedAt", { date: formatDateTime(state.houseLearningChartFetchedAt, { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" }) }))}</p>` : ""}
  </div>`;
}

export function renderHouseLearningSettings(section = "measurements") {
  const initialCapabilityCheck = state.houseLearningEndpointChecking && !state.houseLearningEndpointChecked;
  if (!hasEntity("houseLearningEnabled") || (!state.houseLearningEndpointAvailable && !initialCapabilityCheck)) return "";
  const resetting = state.busyAction === "houseLearningReset" || state.houseLearningReset === "pending";
  const busy = Boolean(state.busyAction) || resetting;
  if (section === "export") return `<div class="oq-learning-download">
    <div><h5>${escapeHtml(t("houseLearning.export.label"))}</h5><p>${escapeHtml(t("houseLearning.export.copy"))}</p></div>
    <div data-oq-house-learning-actions>${renderHouseLearningDataActions(busy)}</div>
  </div>`;
  const learningSwitch = getSettingsSwitchModel("houseLearningEnabled");
  if (section === "configuration") return `<section class="oq-settings-section" data-oq-house-learning>
    <div class="oq-settings-section-head"><div class="oq-house-learning-heading"><h3>${escapeHtml(t("houseLearning.switch.label"))}</h3><div data-oq-learning-switch>${renderSettingsCompactSwitchControl("houseLearningEnabled", t("houseLearning.switch.label"), learningSwitch.enabled, busy || learningSwitch.busy, t("common.on"), t("common.off"))}</div></div><p>${escapeHtml(t("houseLearning.switch.copy"))}</p></div>
    <div class="oq-settings-section-body"><div data-oq-house-learning-reset-status>${state.houseLearningReset ? renderHouseLearningStatusMarkup() : ""}</div><div data-oq-house-learning-actions data-learning-action-mode="reset">${renderHouseLearningDataActions(busy || state.loadingEntities, true)}</div></div>
  </section>`;
  if (section === "diagnostics") return `<section class="oq-settings-section" data-oq-house-learning>
    <h4 class="oq-learning-diagnostic-title">${escapeHtml(t("houseLearning.diagnostics.title"))}</h4>
    <div class="oq-settings-section-body">
      <div data-oq-house-learning-reset-status>${state.houseLearningReset ? renderHouseLearningStatusMarkup() : ""}</div>
      <div data-oq-house-learning-secondary>${renderHouseLearningStatusMarkup(state.houseLearningStatus, "secondary")}</div>
      <div data-oq-house-learning-shadow>${renderHouseLearningShadow()}</div>
      <div data-oq-house-learning-technical>${renderHouseLearningStatusMarkup(state.houseLearningStatus, "technical")}</div>
    </div>
  </section>`;
  return `<section class="oq-settings-section" data-oq-house-learning>
      <div class="oq-settings-section-body"><div data-oq-house-learning-status>${renderCurrentLearningStatus()}</div></div>
    </section>
    <section class="oq-settings-section" data-oq-house-learning-chart-panel data-oq-chart-signature="${escapeHtml(houseLearningChartSignature(busy))}">${renderHouseLearningChartPanel(busy)}</section>`;
}

const learningMarkup = new WeakMap();
function patchLearningMarkup(node, markup) {
  if (learningMarkup.get(node) === markup) return;
  const active = document.activeElement;
  const focused = node.contains?.(active);
  const control = focused && active.dataset?.controlKey;
  const info = focused && active.dataset?.infoId;
  const action = focused && active.dataset?.oqAction;
  const bandControl = focused && active.hasAttribute?.("data-oq-house-learning-band-mode");
  const selector = control === "houseLearningEnabled" ? '[data-control-key="houseLearningEnabled"]'
    : ["house-learning-window", "house-learning-readiness", "house-learning-thermal", "house-learning-chart", "house-learning-shadow"].includes(info)
    ? `[data-info-id="${info}"]`
    : ["load-house-learning-chart", "download-house-learning"].includes(action) ? `[data-oq-action="${action}"]`
    : focused && active.dataset?.oqButtonKey === "houseLearningReset" ? '[data-oq-button-key="houseLearningReset"]'
    : focused && ["house-learning-technical", "house-learning-readiness"].includes(active.dataset?.settingsAdvanced) ? `summary[data-settings-advanced="${active.dataset.settingsAdvanced}"]` : null;
  const point = focused && active.dataset?.oqHouseLearningTip;
  node.innerHTML = markup;
  learningMarkup.set(node, markup);
  if (bandControl) {
    (node.querySelector("[data-oq-house-learning-band-mode]") || node.querySelector('[data-oq-action="load-house-learning-chart"]'))?.focus({ preventScroll: true });
  } else if (point) {
    const replacement = [...node.querySelectorAll("[data-oq-house-learning-tip]")]
      .find(candidate => candidate.dataset.oqHouseLearningTip === point);
    (replacement || node.querySelector('[data-oq-action="load-house-learning-chart"]'))?.focus({ preventScroll: true });
  } else if (selector) node.querySelector(selector)?.focus({ preventScroll: true });
}

export function patchHouseLearningSettingsStatus() {
  const panel = state.root?.querySelector("[data-oq-house-learning]");
  const initialCapabilityCheck = state.houseLearningEndpointChecking && !state.houseLearningEndpointChecked;
  if (panel && (!hasEntity("houseLearningEnabled") || (!state.houseLearningEndpointAvailable && !initialCapabilityCheck))) {
    return false;
  }
  const node = panel?.querySelector("[data-oq-house-learning-status]");
  if (!panel) return false;
  const toggle = panel.querySelector("[data-oq-learning-switch]");
  if (toggle) {
    const model = getSettingsSwitchModel("houseLearningEnabled");
    patchLearningMarkup(toggle, renderSettingsCompactSwitchControl("houseLearningEnabled", t("houseLearning.switch.label"),
      model.enabled, Boolean(state.busyAction) || state.houseLearningReset === "pending" || model.busy, t("common.on"), t("common.off")));
  }
  const chartNode = state.root.querySelector("[data-oq-house-learning-chart-panel]");
  if (chartNode) {
    const signature = houseLearningChartSignature();
    if (chartNode.getAttribute("data-oq-chart-signature") !== signature) {
      patchLearningMarkup(chartNode, renderHouseLearningChartPanel());
      chartNode.setAttribute("data-oq-chart-signature", signature);
    }
  }
  const shadowNode = panel.querySelector("[data-oq-house-learning-shadow]");
  if (shadowNode) patchLearningMarkup(shadowNode, renderHouseLearningShadow());
  if (node) patchLearningMarkup(node, renderCurrentLearningStatus());
  const resetStatus = panel.querySelector("[data-oq-house-learning-reset-status]");
  if (resetStatus) patchLearningMarkup(resetStatus, state.houseLearningReset ? renderHouseLearningStatusMarkup() : "");
  const actions = state.root.querySelector("[data-oq-house-learning-actions]");
  if (actions) patchLearningMarkup(actions, renderHouseLearningDataActions(state.loadingEntities || Boolean(state.busyAction) || state.houseLearningReset === "pending", actions.dataset.learningActionMode === "reset"));
  const secondary = panel.querySelector("[data-oq-house-learning-secondary]");
  if (secondary) patchLearningMarkup(secondary, renderHouseLearningStatusMarkup(state.houseLearningStatus, "secondary"));
  const technical = panel.querySelector("[data-oq-house-learning-technical]");
  if (technical) patchLearningMarkup(technical, renderHouseLearningStatusMarkup(state.houseLearningStatus, "technical"));
  return true;
}
