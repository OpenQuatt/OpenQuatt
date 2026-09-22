import { formatDateTime, formatNumber, hasTranslation, t } from "../i18n/index.js";

export const INCIDENT_MONITORING_SCHEMA_VERSION = 1;
export const INCIDENT_MONITORING_FAILURE_THRESHOLD = 3;

const SEVERITY_RANK = Object.freeze({
  normal: 0,
  limited: 1,
  attention: 2,
  fault: 3,
});

// Systeemacties zijn taal-onafhankelijke codes; presentatie (label/copy)
// komt uit de i18n-catalogus (incidents.system*).
const SYSTEM_ACTION_SEVERITY = Object.freeze({
  none: "normal",
  boiler_assist: "normal",
  boiler_fallback: "fault",
  fallback_blocked: "fault",
});

function systemActionSuffix(action) {
  if (action === "boiler_assist") return "BoilerAssist";
  if (action === "boiler_fallback") return "BoilerFallback";
  if (action === "fallback_blocked") return "FallbackBlocked";
  return "None";
}

function isKnownSystemAction(action) {
  return Object.prototype.hasOwnProperty.call(SYSTEM_ACTION_SEVERITY, action);
}

// Incidentcatalogus: taal-onafhankelijke [id, key]-paren; labels leven in
// de i18n-catalogus (incidents.label.<key>).
const INCIDENT_CATALOG = [
  [1, "main_line_current"],
  [2, "compressor_phase_current"],
  [3, "ipm_module"],
  [4, "compressor_oil_return"],
  [5, "high_pressure_switch"],
  [6, "high_pressure_speed_limit"],
  [7, "first_start_preheat"],
  [8, "gas_discharge_temperature"],
  [9, "evaporator_coil_temperature"],
  [10, "ac_voltage"],
  [11, "ambient_temperature_range"],
  [12, "ambient_temperature_frequency_limit"],
  [13, "low_pressure_switch"],
  [14, "low_pressure_speed_limit"],
  [17, "ambient_temperature_sensor"],
  [18, "evaporator_coil_temperature_sensor"],
  [19, "gas_discharge_temperature_sensor"],
  [20, "gas_return_temperature_sensor"],
  [21, "evaporator_pressure_sensor_lock"],
  [22, "condenser_pressure_sensor"],
  [23, "high_pressure_switch_lock"],
  [24, "low_pressure_switch_lock"],
  [25, "fan"],
  [27, "evaporating_pressure_lock"],
  [28, "condenser_pressure_lock"],
  [30, "evi_pressure_sensor"],
  [31, "evi_inlet_temperature_sensor"],
  [32, "evi_outlet_temperature_sensor"],
  [33, "odu_master_slave_communication"],
  [34, "odu_control_pcb_communication"],
  [35, "compressor_phase_current_failure"],
  [36, "compressor_phase_current_overload"],
  [37, "compressor_driver"],
  [38, "module_vdc_voltage"],
  [39, "ac_current"],
  [40, "eeprom"],
  [41, "fan_drive_pcb"],
  [42, "inlet_water_temperature_sensor"],
  [43, "outlet_water_temperature_sensor"],
  [44, "inner_coil_temperature_sensor"],
  [46, "dc_water_pump"],
  [1001, "hp_link_loss"],
  [1002, "hp_start_failed"],
  [1003, "hp_stop_unconfirmed"],
  [1004, "hp_manual_reset_persistence_failure"],
  [1005, "hp_runtime_frequency_mapping"],
];
const INCIDENT_KEY_BY_ID = new Map(INCIDENT_CATALOG.map(([id, key]) => [id, key]));
const INCIDENT_ID_BY_KEY = Object.freeze(Object.fromEntries(
  INCIDENT_CATALOG.map(([id, key]) => [key, id]),
));

const CATEGORY_CODES = Object.freeze(["status", "protection", "warning", "fault", "unknown"]);
const PUMP_IPWM_STATUS_FALLBACK = Object.freeze({
  pump_on_abnormal: "PumpOnAbnormal",
  pump_off_abnormal: "PumpOffAbnormal",
  pump_off_failure: "PumpOffFailure",
});

function pickTranslation(base, code, fallback = "") {
  const key = `${base}.${code}`;
  return hasTranslation(key) ? t(key) : fallback;
}

function formatIncidentNumber(value) {
  return formatNumber(Number(value), { maximumFractionDigits: 0 });
}

function capitalizeStatusKey(value) {
  return String(value || "")
    .split(/[_-]+/)
    .filter(Boolean)
    .map((part) => part.charAt(0).toUpperCase() + part.slice(1))
    .join("");
}

const isObject = (value) => Boolean(value) && typeof value === "object" && !Array.isArray(value);
const normalizeInteger = (value, fallback = null) => {
  const number = Number(value);
  return Number.isInteger(number) && number >= 0 ? number : fallback;
};
const normalizeBoolean = (value) => value === true;
const normalizeOptionalBoolean = (value) => typeof value === "boolean" ? value : null;
const maxSeverity = (...values) => values.reduce((highest, value) => (
  SEVERITY_RANK[value] > SEVERITY_RANK[highest] ? value : highest
), "normal");

function parseSnapshot(input) {
  if (typeof input !== "string") return input;
  try {
    return JSON.parse(input);
  } catch {
    return null;
  }
}

function technicalIncidentCode(id) {
  const numericId = Number(id);
  if (!Number.isInteger(numericId) || numericId < 1 || numericId > 48) return "";
  return `R${2119 + Math.floor((numericId - 1) / 16)}.b${(numericId - 1) % 16}`;
}

export function getIncidentTechnicalCode(incident = {}) {
  const register = normalizeInteger(incident.register ?? incident.register_address);
  const bit = normalizeInteger(incident.bit);
  if (register >= 2119 && register <= 2121 && bit >= 0 && bit <= 15) {
    return `R${register}.b${bit}`;
  }
  return technicalIncidentCode(incident.id);
}

export function getPumpIncidentContextRows(incident = {}, pumpContext = null) {
  const isPumpIncident = Number(incident.id) === 46
    || (Number(incident.register) === 2121 && Number(incident.bit) === 13);
  if (!isPumpIncident || !isObject(pumpContext)) return [];

  const rows = [];
  const onOff = (value) => value ? t("incidents.pumpOn") : t("incidents.pumpOff");
  if (typeof pumpContext.requestOn === "boolean") {
    rows.push([t("incidents.pumpRequest"), onOff(pumpContext.requestOn)]);
  }
  if (typeof pumpContext.relayOn === "boolean") {
    rows.push([t("incidents.pumpRelay"), onOff(pumpContext.relayOn)]);
  }
  if (typeof pumpContext.flowSwitchOn === "boolean") {
    rows.push([t("incidents.pumpFlowSwitch"), onOff(pumpContext.flowSwitchOn)]);
  }
  const statusKey = String(pumpContext.ipwmStatus || "unknown");
  if (pumpContext.feedbackRaw !== null || statusKey !== "unknown") {
    const raw = pumpContext.feedbackRaw !== null ? `${pumpContext.feedbackRaw} raw` : t("incidents.pumpRawUnknown");
    const status = pickTranslation("incidents.pumpStatus", statusKey, PUMP_IPWM_STATUS_FALLBACK[statusKey] || statusKey);
    rows.push([t("incidents.pumpFeedback"), `${raw} · ${status}`]);
  }
  if (pumpContext.pumpPowerW !== null) {
    rows.push([t("incidents.pumpDerivedPower"), `${formatIncidentNumber(pumpContext.pumpPowerW)} W`]);
  }
  if (pumpContext.flowLph !== null) {
    rows.push([t("incidents.pumpFlow"), `${formatIncidentNumber(pumpContext.flowLph)} L/h`]);
  }
  return rows;
}

export function getIncidentDisplayLabel(incident = {}) {
  const displayLabel = String(incident.displayLabel ?? incident.display_label ?? "").trim();
  if (displayLabel) return displayLabel;
  const id = Number(incident.id);
  const key = String(incident.key || "").trim().toLowerCase();
  if (key === "unclassified_odu_fault") {
    const code = technicalIncidentCode(id);
    return code ? t("incidents.unclassifiedOduWithCode", { code }) : t("incidents.unclassifiedOdu");
  }
  if (INCIDENT_ID_BY_KEY[key]) return t(`incidents.label.${key}`);
  if (INCIDENT_KEY_BY_ID.has(id)) return t(`incidents.label.${INCIDENT_KEY_BY_ID.get(id)}`);
  if (key) {
    const words = key.replace(/^odu_/, "").replaceAll("_", " ");
    return t("incidents.oduReport", { label: `${words.charAt(0).toUpperCase()}${words.slice(1)}` });
  }
  const code = technicalIncidentCode(id);
  return code ? t("incidents.unclassifiedOduWithCode", { code }) : t("incidents.unclassifiedHeatpump");
}

export function getIncidentCategoryLabel(category) {
  const normalized = String(category || "").toLowerCase();
  return t(`incidents.category.${CATEGORY_CODES.includes(normalized) ? normalized : "unknown"}`);
}

export function getIncidentEffectLabels(effects = []) {
  const normalized = effects.map((effect) => String(effect).toLowerCase());
  const controlling = normalized
    .filter((effect) => effect !== "display")
    .map((effect) => pickTranslation("incidents.effect", effect))
    .filter(Boolean);
  return controlling.length ? controlling : normalized.includes("display") ? [t("incidents.effectDisplayOnly")] : [];
}

export function getIncidentRecoveryLabel(value) {
  const normalized = String(value || "").toLowerCase();
  return pickTranslation("incidents.recovery", normalized, String(value || "").replaceAll("_", " "));
}

export function getIncidentUserActionLabel(value) {
  const normalized = String(value || "").toLowerCase();
  if (!normalized || normalized === "none") return "";
  return pickTranslation("incidents.userAction", normalized, String(value || "").replaceAll("_", " "));
}

export function formatIncidentOccurrenceTime(epochS, uptimeMs) {
  const epoch = Number(epochS);
  if (Number.isFinite(epoch) && epoch >= 946684800) {
    return formatDateTime(new Date(epoch * 1000), {
      day: "2-digit",
      month: "short",
      hour: "2-digit",
      minute: "2-digit",
    });
  }
  const uptime = Number(uptimeMs);
  if (!Number.isFinite(uptime) || uptime < 0) return "";
  const minutes = Math.round(uptime / 60000);
  const span = minutes < 60
    ? t("incidents.occurrenceMinutes", { minutes })
    : t("incidents.occurrenceHoursMinutes", { hours: Math.floor(minutes / 60), minutes: minutes % 60 });
  return t("incidents.occurrenceAfterBoot", { span });
}

export function getFallbackBlockReasonLabel(reason) {
  const index = Number(reason);
  const key = `incidents.fallbackBlock.${index}`;
  if (Number.isInteger(index) && hasTranslation(key)) {
    return t(key);
  }
  return t("incidents.fallbackBlockUnknown");
}

export function getSystemActionPresentation(action) {
  const code = String(action || "").toLowerCase();
  const known = isKnownSystemAction(code) ? code : "none";
  const suffix = systemActionSuffix(known);
  return {
    label: t(`incidents.system${suffix}Label`),
    copy: known === "none" ? "" : t(`incidents.system${suffix}Copy`),
    severity: SYSTEM_ACTION_SEVERITY[known],
  };
}

export function getIncidentLifecyclePresentation(incident = {}) {
  if (incident.active) {
    return { label: t("incidents.lifecycleActive"), tone: incident.severity === "fault" ? "fault" : "warning" };
  }
  if (incident.recovering) return { label: t("incidents.lifecycleRecovering"), tone: "warning" };
  if (incident.latched && !incident.acknowledged) {
    return { label: t("incidents.lifecycleLatched"), tone: "warning" };
  }
  return { label: t("incidents.lifecycleCleared"), tone: "clear" };
}

export function getHeatPumpStatusPresentation(heatPump = {}) {
  const linkState = String(heatPump.linkState || "unknown");
  const runState = String(heatPump.runState || "unknown");
  const linkLabel = pickTranslation("incidents", `link${capitalizeStatusKey(linkState)}`, t("incidents.linkUnknown"));
  const runStatus = heatPump.stopConfirmationPending
    ? t("incidents.runReconfirm")
    : pickTranslation("incidents", `run${capitalizeStatusKey(runState)}`, t("incidents.runUnknown"));
  const note = `${linkLabel} · ${runStatus}`;
  if (heatPump.faultActive || heatPump.protectionState === "fault_active") {
    return { label: t("incidents.hpFaultActive"), note, tone: "fault" };
  }
  if (heatPump.linkState === "lost") return { label: t("incidents.hpUnavailable"), note, tone: "fault" };
  if (heatPump.protectionState === "start_blocked") {
    return { label: t("incidents.hpStartBlocked"), note, tone: "warning" };
  }
  if (heatPump.protectionState === "limited") {
    return { label: t("incidents.hpLimited"), note, tone: "warning" };
  }
  if (heatPump.availability === "recovering" || heatPump.linkState === "recovering") {
    return { label: t("incidents.hpRecovering"), note, tone: "warning" };
  }
  if (heatPump.availableForStart || heatPump.availability === "available") {
    return { label: t("incidents.hpAvailable"), note, tone: "clear" };
  }
  return {
    label: t("incidents.hpDetermining"),
    note,
    tone: heatPump.linkState === "suspect" ? "clear" : "warning",
  };
}

function normalizeIncident(raw, subject) {
  if (!isObject(raw) || !isObject(raw.definition) || !isObject(raw.runtime)) return null;
  const definition = raw.definition;
  const runtime = raw.runtime;
  const id = normalizeInteger(definition.id);
  if (id === null) return null;
  const category = CATEGORY_CODES.includes(definition.category) ? definition.category : "unknown";
  const severity = definition.severity;
  const runtimeLifecycle = ["active", "recovering", "latched"].includes(runtime.lifecycle)
    ? runtime.lifecycle
    : "cleared";
  const lifecycle = runtimeLifecycle === "latched" ? "cleared" : runtimeLifecycle;
  const effects = Array.isArray(definition.effects)
    ? [...new Set(definition.effects.filter((value) => typeof value === "string"))]
    : [];
  return {
    id: String(id),
    key: String(definition.key || "").trim(),
    displayLabel: String(definition.display_label || "").trim(),
    subject,
    category,
    severity: severity === "info" ? "normal" : severity === "fault" ? "fault" : "attention",
    lifecycle,
    active: lifecycle === "active",
    recovering: lifecycle === "recovering",
    latched: normalizeBoolean(runtime.latched) || runtimeLifecycle === "latched",
    acknowledged: normalizeBoolean(runtime.acknowledged),
    effects,
    effectMask: normalizeInteger(definition.effect_mask),
    firstSeenS: normalizeInteger(runtime.first_seen_s),
    lastSeenS: normalizeInteger(runtime.last_seen_s),
    firstSeenMs: normalizeInteger(runtime.first_seen_ms),
    lastSeenMs: normalizeInteger(runtime.last_seen_ms),
    occurrenceCount: normalizeInteger(runtime.occurrence_count, 0),
    register: normalizeInteger(definition.register_address),
    bit: normalizeInteger(definition.bit),
    technicalDescription: String(definition.source_description || "").trim(),
    recoveryCondition: String(definition.recovery_condition || "").trim(),
    userAction: String(definition.user_action || "").trim(),
  };
}

function normalizeHeatPump(raw) {
  if (!isObject(raw)) return null;
  const index = normalizeInteger(raw.index);
  if (index !== 1 && index !== 2) return null;
  const linkState = String(raw.link_state || "unknown");
  const protectionState = String(raw.protection_state || "unknown");
  const availableForStart = normalizeBoolean(raw.available_for_start);
  const mustStop = normalizeBoolean(raw.must_stop);
  let availability = String(raw.availability || "unknown");
  if (availability === "unknown") {
    availability = availableForStart
      ? "available"
      : linkState === "recovering" || protectionState === "fault_recovery"
        ? "recovering"
        : mustStop || linkState === "lost" || protectionState === "fault_active"
          ? "unavailable"
          : protectionState === "start_blocked" ? "blocked" : "unknown";
  }
  const subject = `hp${index}`;
  const lastActionResult = normalizeIncidentActionResult(raw.last_action_result);
  const actionResults = Array.isArray(raw.action_results)
    ? raw.action_results.map(normalizeIncidentActionResult).filter(Boolean)
    : [];
  if (lastActionResult && !actionResults.some((result) => (
    result.requestId === lastActionResult.requestId
    && result.action === lastActionResult.action
  ))) {
    actionResults.push(lastActionResult);
  }
  return {
    index,
    subject,
    linkState,
    protectionState,
    runState: String(raw.run_state || "unknown"),
    availability,
    availableForStart,
    mustStop,
    faultActive: normalizeBoolean(raw.fault_active),
    stopConfirmationPending: normalizeBoolean(raw.stop_confirmation_pending),
    stopUnconfirmedDueToLinkLoss: normalizeBoolean(raw.stop_unconfirmed_due_to_link_loss),
    pumpContext: normalizePumpContext(raw.pump_context),
    lastActionResult,
    actionResults,
    incidents: Array.isArray(raw.incidents)
      ? raw.incidents.map((incident) => normalizeIncident(incident, subject)).filter(Boolean)
      : [],
  };
}

function normalizePumpContext(raw) {
  if (!isObject(raw)) return null;
  const optionalNumber = (value) => {
    if (value === null || value === undefined || value === "") return null;
    const number = Number(value);
    return Number.isFinite(number) ? number : null;
  };
  return {
    requestOn: normalizeOptionalBoolean(raw.request_on),
    relayOn: normalizeOptionalBoolean(raw.relay_on),
    flowSwitchOn: normalizeOptionalBoolean(raw.flow_switch_on),
    feedbackRaw: raw.ipwm_feedback_raw === null || raw.ipwm_feedback_raw === undefined
      ? null
      : normalizeInteger(raw.ipwm_feedback_raw),
    ipwmStatus: String(raw.ipwm_status || "unknown"),
    pumpPowerW: optionalNumber(raw.pump_power_w),
    flowLph: optionalNumber(raw.flow_lph),
  };
}

function normalizeIncidentActionResult(raw) {
  if (!isObject(raw)) return null;
  const sequence = normalizeInteger(raw.sequence);
  const requestId = normalizeInteger(raw.request_id);
  if (sequence === null || requestId === null || requestId < 1) return null;
  return {
    sequence,
    requestId,
    action: String(raw.action || "").trim(),
    ok: raw.ok === true,
    result: String(raw.result || "").trim(),
    atMs: normalizeInteger(raw.at_ms, 0),
  };
}

function normalizeSystem(raw) {
  const system = isObject(raw) ? raw : {};
  const controlMode = normalizeInteger(system.control_mode, 0);
  const rawAction = isKnownSystemAction(system.action) ? system.action : "none";
  const rawRole = String(system.boiler_role || "off");
  const boilerRole = controlMode === 3 ? "assist" : controlMode === 4 ? "fallback" : rawRole;
  const previousBoilerRole = String(system.previous_boiler_role || "off");
  const boilerCommandActive = normalizeBoolean(system.boiler_command_active);
  const fallbackBlockReason = normalizeInteger(system.fallback_block_reason, 0);
  const roleAction = rawAction !== "none"
    ? rawAction
    : controlMode === 3 ? "boiler_assist" : controlMode === 4 ? "boiler_fallback" : "none";
  const action = roleAction === "boiler_fallback" && !boilerCommandActive
    ? "fallback_blocked"
    : roleAction === "boiler_assist" && !boilerCommandActive ? "none" : roleAction;
  const boilerOutputContinuous = normalizeOptionalBoolean(system.boiler_output_continuous);
  const assistToFallback = previousBoilerRole === "assist" && boilerRole === "fallback";
  return {
    controlMode,
    action,
    boilerRole,
    previousBoilerRole,
    boilerCommandActive,
    boilerOutputContinuous,
    boilerTransition: assistToFallback
      ? boilerOutputContinuous === true
        ? "assist_to_fallback_continuous"
        : boilerOutputContinuous === false ? "assist_to_fallback_interrupted" : "assist_to_fallback"
      : "none",
    fallbackBlockReason,
  };
}

function invalidSnapshot(error, schemaVersion = null) {
  return {
    valid: false,
    error,
    schemaVersion,
    actionCsrfToken: "",
    generatedAtS: null,
    system: normalizeSystem({}),
    heatPumps: [],
  };
}

export function normalizeIncidentMonitoringSnapshot(input) {
  const raw = parseSnapshot(input);
  if (!isObject(raw)) return invalidSnapshot("invalid_payload");
  const schemaVersion = normalizeInteger(raw.schema_version);
  if (schemaVersion === null) return invalidSnapshot("missing_schema_version");
  if (schemaVersion !== INCIDENT_MONITORING_SCHEMA_VERSION) {
    return invalidSnapshot("unsupported_schema_version", schemaVersion);
  }
  return {
    valid: true,
    error: "",
    schemaVersion,
    actionCsrfToken: String(raw.action_csrf_token || ""),
    generatedAtS: normalizeInteger(raw.generated_at_s),
    system: normalizeSystem(raw.system),
    heatPumps: Array.isArray(raw.heat_pumps)
      ? raw.heat_pumps.map(normalizeHeatPump).filter(Boolean).sort((a, b) => a.index - b.index)
      : [],
  };
}

export function getIncidentActionPresentation(action = {}, hpIndex = null) {
  const hp = normalizeInteger(action.hp, 0);
  if ((hpIndex !== null && hp !== Number(hpIndex)) || (hp !== 1 && hp !== 2)) {
    return { visible: false, label: "", copy: "", tone: "clear" };
  }
  const kindLabel = action.kind === "confirm_odu_power_cycle"
    ? t("incidents.actionKindPowerCycle")
    : t("incidents.actionKindStartFault");
  if (action.pending) {
    return {
      visible: true,
      label: action.outcomeUnknown
        ? t("incidents.actionPendingCheck", { kind: kindLabel })
        : t("incidents.actionPendingBusy", { kind: kindLabel }),
      copy: action.outcomeUnknown
        ? t("incidents.actionPendingCopyUnknown")
        : t("incidents.actionPendingCopy"),
      tone: "warning",
    };
  }
  if (action.ok === true) {
    return {
      visible: true,
      label: t("incidents.actionDone", { kind: kindLabel }),
      copy: pickTranslation("incidents.actionResult", String(action.result || ""), t("incidents.actionConfirmed")),
      tone: "clear",
    };
  }
  if (action.ok === false) {
    return {
      visible: true,
      label: t("incidents.actionFailed", { kind: kindLabel }),
      copy: pickTranslation("incidents.actionResult", String(action.result || ""), String(action.message || t("incidents.actionRefused"))),
      tone: "fault",
    };
  }
  return { visible: false, label: "", copy: "", tone: "clear" };
}

function resolveIncidentAction(snapshot, action) {
  const requestId = normalizeInteger(action?.requestId, 0);
  const hp = normalizeInteger(action?.hp, 0);
  if (!action?.pending || requestId < 1 || (hp !== 1 && hp !== 2)) return action;
  const heatPump = snapshot.heatPumps.find(
    (candidate) => candidate.index === hp,
  );
  const result = heatPump?.actionResults?.find((candidate) => (
    candidate.requestId === requestId
    && candidate.action === action.kind
  )) || heatPump?.lastActionResult;
  if (!result || result.requestId !== requestId || result.action !== action.kind) return action;
  return {
    ...action,
    pending: false,
    ok: result.ok,
    result: result.result,
    sequence: result.sequence,
    completedAtMs: result.atMs,
  };
}

let fallbackIncidentActionRequestId =
  (Date.now() >>> 0) || 1;

export function createIncidentActionRequestId(
  cryptoSource = globalThis.crypto,
) {
  if (cryptoSource?.getRandomValues) {
    const values = new Uint32Array(1);
    cryptoSource.getRandomValues(values);
    if (values[0] !== 0) return values[0];
  }
  fallbackIncidentActionRequestId =
    (fallbackIncidentActionRequestId + 1) >>> 0;
  if (fallbackIncidentActionRequestId === 0) {
    fallbackIncidentActionRequestId = 1;
  }
  return fallbackIncidentActionRequestId;
}

function incidentActionRequestError(message, definitive) {
  const error = new Error(message);
  error.incidentActionDefinitive = definitive;
  return error;
}

export async function postIncidentActionRequest(
  fetcher,
  endpoint,
  hp,
  requestId,
  csrfToken,
  refreshCsrfToken,
) {
  const hpIndex = normalizeInteger(hp, 0);
  if (hpIndex !== 1 && hpIndex !== 2) throw new Error(t("incidents.actionResult.invalid_hp"));
  const actionRequestId = normalizeInteger(requestId, 0);
  if (actionRequestId < 1) {
    throw incidentActionRequestError(
      t("incidents.actionResult.invalid_request_id"),
      true,
    );
  }
  const expectedAction = endpoint.endsWith("/retry-start")
    ? "start_failure_retry"
    : endpoint.endsWith("/confirm-odu-power-cycle")
      ? "confirm_odu_power_cycle"
      : "";
  if (!expectedAction) throw new Error(t("incidents.unknownAction"));

  const post = (token) => fetcher(endpoint, {
    method: "POST",
    credentials: "same-origin",
    cache: "no-store",
    headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
    body: new URLSearchParams({
      hp: String(hpIndex),
      request_id: String(actionRequestId),
      csrf_token: String(token || ""),
    }),
  });

  let token = String(csrfToken || "");
  let response = null;
  let networkError = null;
  for (let attempt = 0; attempt < 2 && !response; attempt += 1) {
    try {
      response = await post(token);
    } catch (error) {
      networkError = error;
    }
  }
  if (!response) {
    throw incidentActionRequestError(
      networkError?.message || t("incidents.noControllerResponse"),
      false,
    );
  }
  if (response.status === 403 && typeof refreshCsrfToken === "function") {
    token = String(await refreshCsrfToken() || "");
    try {
      response = await post(token);
    } catch (error) {
      throw incidentActionRequestError(
        error?.message || t("incidents.noControllerResponse"),
        false,
      );
    }
  }
  let payload = {};
  try {
    payload = await response.json();
  } catch (_error) {
    // The response status remains authoritative for a malformed body.
  }
  if (response.status !== 202 || payload?.accepted !== true) {
    const result = String(payload?.result || "");
    throw incidentActionRequestError(
      pickTranslation("incidents.actionResult", result, t("incidents.actionHttp", { status: response.status })),
      true,
    );
  }
  const actionId = normalizeInteger(payload.action_id, 0);
  if (payload.hp !== hpIndex
      || payload.action !== expectedAction
      || actionId !== actionRequestId) {
    throw incidentActionRequestError(
      t("incidents.noValidConfirmation"),
      false,
    );
  }
  return { hp: hpIndex, action: expectedAction, actionId, csrfToken: token };
}

const incidentVisible = (incident) => incident.active
  || incident.recovering
  || (incident.lifecycle === "cleared" && incident.latched && !incident.acknowledged);

// Presentation-side grouping for issue #706. Shared by the summary problem
// list and the detail cards so control truth and presentation truth stay
// identical: group only when the firmware reports the stop as unconfirmed
// *due to* this link loss, while the outage is live and the stop entry
// itself is still open. A stop failure predating the loss, or an already
// recovered stop entry, keeps its standalone entry.
const LINK_LOSS_GROUP = Object.freeze({
  linkLossId: "1001",
  stopUnconfirmedId: "1003",
  copyKey: "incidents.linkLossGroupCopy",
});

export function getLinkLossConsequenceForHeatPump(heatPump) {
  if (!isObject(heatPump)) return null;
  if (heatPump.linkState !== "lost" && heatPump.linkState !== "recovering") return null;
  if (heatPump.stopUnconfirmedDueToLinkLoss !== true) return null;
  const visible = (heatPump.incidents || []).filter(
    (incident) => incident.category !== "status" && incidentVisible(incident),
  );
  const linkLoss = visible.find(
    (incident) => incident.id === LINK_LOSS_GROUP.linkLossId && (incident.active || incident.recovering),
  ) || null;
  const consequence = visible.find(
    (incident) => incident.id === LINK_LOSS_GROUP.stopUnconfirmedId && (incident.active || incident.recovering),
  ) || null;
  if (!linkLoss || !consequence) return null;
  return { linkLoss, consequence, copy: t(LINK_LOSS_GROUP.copyKey) };
}

export function summarizeIncidentMonitoring(input) {
  const snapshot = isObject(input)
    && typeof input.valid === "boolean"
    && Array.isArray(input.heatPumps)
    ? input
    : normalizeIncidentMonitoringSnapshot(input);
  if (!snapshot.valid) {
    return {
      available: false,
      active: false,
      severity: "normal",
      title: t("incidents.summaryNoData"),
      copy: "",
      problemCount: 0,
      activeIncidentCount: 0,
      recoveredIncidentCount: 0,
      systemAction: "none",
      systemActionLabel: getSystemActionPresentation("none").label,
      boilerRole: "off",
      boilerTransition: "none",
      problems: [],
      snapshot,
    };
  }
  const allIncidents = snapshot.heatPumps.flatMap((heatPump) => heatPump.incidents);
  const visible = allIncidents.filter((incident) => incident.category !== "status" && incidentVisible(incident));
  const activeIncidents = visible.filter((incident) => incident.active || incident.recovering);
  const recoveredIncidents = visible.filter((incident) => !incident.active && !incident.recovering);
  const action = snapshot.system.action;
  const actionPresentation = getSystemActionPresentation(action);
  const incidentSeverity = visible.reduce((severity, incident) => maxSeverity(
    severity,
    recoveredIncidents.includes(incident) ? "attention" : incident.severity,
  ), "normal");
  const severity = maxSeverity(incidentSeverity, actionPresentation.severity);
  const actionAttention = actionPresentation.severity !== "normal";
  const groupedBySubject = new Map();
  for (const heatPump of snapshot.heatPumps) {
    const pair = getLinkLossConsequenceForHeatPump(heatPump);
    if (pair) groupedBySubject.set(heatPump.subject, pair);
  }
  const problems = visible
    .filter((incident) => {
      const pair = groupedBySubject.get(incident.subject);
      return !pair || incident.id !== pair.consequence.id;
    })
    .map((incident) => {
      const entry = {
        key: `incident:${incident.subject}:${incident.id}`,
        label: `${incident.subject === "hp1" ? t("incidents.hp1Label") : t("incidents.hp2Label")}: ${getIncidentDisplayLabel(incident)}`,
        severity: recoveredIncidents.includes(incident) ? "attention" : incident.severity,
        incidentId: incident.id,
      };
      const pair = groupedBySubject.get(incident.subject);
      if (pair && incident.id === pair.linkLoss.id) {
        entry.copy = pair.copy;
        entry.groupedIncidentIds = [pair.linkLoss.id, pair.consequence.id];
      }
      return entry;
    });
  // Grouped consequences stay part of the firmware-truth counts above, but
  // the user-facing copy counts what the problem list actually shows. Every
  // hidden consequence pairs with a still-displayed live outage, so a shown
  // count can never drop to zero inside a taken branch.
  let hiddenActiveIncidentCount = 0;
  let hiddenRecoveredIncidentCount = 0;
  for (const pair of groupedBySubject.values()) {
    if (pair.consequence.active || pair.consequence.recovering) hiddenActiveIncidentCount += 1;
    else hiddenRecoveredIncidentCount += 1;
  }
  const shownActiveIncidentCount = activeIncidents.length - hiddenActiveIncidentCount;
  const shownRecoveredIncidentCount = recoveredIncidents.length - hiddenRecoveredIncidentCount;
  if (actionAttention) {
    problems.unshift({
      key: `system-action:${action}`,
      label: actionPresentation.label,
      severity: actionPresentation.severity,
      incidentId: "",
    });
  }

  const pluralize = (count) => count === 1 ? "" : t("incidents.pluralSuffix");
  let title = t("incidents.summaryClear");
  let copy = t("incidents.summaryClearCopy");
  if (action === "boiler_fallback" || action === "fallback_blocked") {
    title = actionPresentation.label;
    copy = actionPresentation.copy;
  } else if (activeIncidents.some((incident) => incident.severity === "fault")) {
    title = t("incidents.summaryFaultActive");
    copy = t("incidents.summaryFaultCopy", { count: shownActiveIncidentCount, plural: pluralize(shownActiveIncidentCount) });
  } else if (activeIncidents.length) {
    title = t("incidents.summaryAttention");
    copy = t("incidents.summaryAttentionCopy", { count: shownActiveIncidentCount, plural: pluralize(shownActiveIncidentCount) });
  } else if (recoveredIncidents.length) {
    title = t("incidents.summaryRecovered");
    copy = t("incidents.summaryRecoveredCopy", { count: shownRecoveredIncidentCount, plural: pluralize(shownRecoveredIncidentCount) });
  }
  if (action === "fallback_blocked") {
    const reason = snapshot.system.fallbackBlockReason;
    copy = `${copy} ${reason
      ? t("incidents.summaryBlockPrefix", { reason: getFallbackBlockReasonLabel(reason) })
      : t("incidents.summaryBlockedNoReason")}`;
  }
  if (snapshot.system.boilerCommandActive
      && snapshot.system.boilerTransition === "assist_to_fallback_continuous") {
    copy = `${copy} ${t("incidents.summaryNoPulse")}`;
  }
  return {
    available: true,
    active: visible.length > 0 || actionAttention,
    severity,
    title,
    copy,
    problemCount: problems.length,
    activeIncidentCount: activeIncidents.length,
    recoveredIncidentCount: recoveredIncidents.length,
    systemAction: action,
    systemActionLabel: actionPresentation.label,
    boilerRole: snapshot.system.boilerRole,
    boilerTransition: snapshot.system.boilerTransition,
    problems,
    snapshot,
  };
}

export function combineInstallationMonitoringModel(baseModel, incidentInput) {
  const base = isObject(baseModel) ? baseModel : {};
  const incidentMonitoring = summarizeIncidentMonitoring(incidentInput);
  if (!incidentMonitoring.available) return { ...base, incidentMonitoring };
  const problems = [];
  const seen = new Set();
  for (const problem of [...incidentMonitoring.problems, ...(base.problems || [])]) {
    if (problem?.key && !seen.has(problem.key)) {
      seen.add(problem.key);
      problems.push(problem);
    }
  }
  const baseSeverity = base.active ? "attention" : "normal";
  const incidentDominates = incidentMonitoring.active
    && SEVERITY_RANK[incidentMonitoring.severity] > SEVERITY_RANK[baseSeverity];
  let copy = incidentDominates ? incidentMonitoring.copy : (base.copy || incidentMonitoring.copy);
  if (incidentDominates && base.active && base.problems?.length) {
    copy += ` ${t("incidents.summaryAlsoBase", { count: base.problems.length, plural: base.problems.length === 1 ? "" : t("incidents.pluralSuffix") })}`;
  } else if (!incidentDominates && incidentMonitoring.active) {
    copy += ` ${t("incidents.summaryAlsoIncidents", { count: incidentMonitoring.problemCount, plural: incidentMonitoring.problemCount === 1 ? "" : t("incidents.pluralSuffix") })}`;
  }
  return {
    ...base,
    problems,
    active: Boolean(base.active) || incidentMonitoring.active,
    severity: maxSeverity(baseSeverity, incidentMonitoring.severity),
    title: incidentDominates ? incidentMonitoring.title : (base.title || incidentMonitoring.title),
    copy,
    incidentMonitoring,
  };
}

export function getIncidentMonitoringSuccessUpdate(current = {}, payload, now = Date.now()) {
  const snapshot = normalizeIncidentMonitoringSnapshot(payload);
  if (!snapshot.valid) throw new Error(`incident monitoring ${snapshot.error}`);
  // generatedAtS changes on every poll but has no visual meaning. Excluding it
  // prevents a healthy, unchanged installation from needlessly rerendering.
  const { generatedAtS: _generatedAtS, ...stableSnapshot } = snapshot;
  const signature = JSON.stringify(stableSnapshot);
  const incidentAction = resolveIncidentAction(snapshot, current.incidentAction || {});
  return {
    changed: current.incidentMonitoringSignature !== signature
      || Boolean(current.incidentMonitoringError)
      || current.incidentMonitoringUnsupported === true
      || incidentAction !== current.incidentAction,
    incidentMonitoringSnapshot: snapshot,
    incidentMonitoringError: "",
    incidentMonitoringUnsupported: false,
    incidentMonitoringFailureCount: 0,
    incidentMonitoringSignature: signature,
    incidentMonitoringLastFetchAt: now,
    incidentAction,
  };
}

export function getIncidentMonitoringFailureUpdate(current = {}, error, now = Date.now()) {
  const failureCount = Number(current.incidentMonitoringFailureCount || 0) + 1;
  const previousError = String(current.incidentMonitoringError || "");
  const failureMessage = String(error?.message || error || t("incidents.summaryUpdateFailed"));
  const authenticationFailed = /\bHTTP (?:401|403)\b/i.test(failureMessage);
  const message = authenticationFailed || failureCount >= INCIDENT_MONITORING_FAILURE_THRESHOLD
    ? failureMessage
    : previousError;
  return {
    changed: message !== previousError,
    incidentMonitoringSnapshot: current.incidentMonitoringSnapshot || null,
    incidentMonitoringError: message,
    incidentMonitoringUnsupported: false,
    incidentMonitoringFailureCount: failureCount,
    incidentMonitoringSignature: String(current.incidentMonitoringSignature || ""),
    incidentMonitoringLastFetchAt: now,
  };
}

export function getIncidentMonitoringUnsupportedUpdate(current = {}, now = Date.now()) {
  return {
    changed: Boolean(current.incidentMonitoringSnapshot
      || current.incidentMonitoringError
      || !current.incidentMonitoringUnsupported),
    incidentMonitoringSnapshot: null,
    incidentMonitoringError: "",
    incidentMonitoringUnsupported: true,
    incidentMonitoringFailureCount: 0,
    incidentMonitoringSignature: "",
    incidentMonitoringLastFetchAt: now,
  };
}
