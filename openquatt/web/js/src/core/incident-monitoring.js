export const INCIDENT_MONITORING_SCHEMA_VERSION = 1;
export const INCIDENT_MONITORING_FAILURE_THRESHOLD = 3;

const SEVERITY_RANK = Object.freeze({
  normal: 0,
  limited: 1,
  attention: 2,
  fault: 3,
});

const SYSTEM_ACTIONS = Object.freeze({
  none: { label: "Geen bijzondere systeemactie", copy: "", severity: "normal" },
  boiler_assist: {
    label: "CV ondersteunt tijdelijk",
    copy: "De warmtepomp blijft de basis leveren; de CV-ketel ondersteunt tijdelijk.",
    severity: "normal",
  },
  boiler_fallback: {
    label: "Ketel neemt verwarming over",
    copy: "De warmtepomp is niet beschikbaar. De CV-ketel krijgt tijdelijk de verwarmingsopdracht.",
    severity: "fault",
  },
  fallback_blocked: {
    label: "Ketelfallback niet vrijgegeven",
    copy: "OpenQuatt kan de ketel niet veilig vrijgeven.",
    severity: "fault",
  },
});

const INCIDENT_CATALOG = [
  [1, "main_line_current", "Netstroombeveiliging"],
  [2, "compressor_phase_current", "Compressorfasestroom"],
  [3, "ipm_module", "IPM-vermogensmodule"],
  [4, "compressor_oil_return", "Olie-retour actief"],
  [5, "high_pressure_switch", "Hogedrukbeveiliging"],
  [6, "high_pressure_speed_limit", "Toerental begrensd door hoge druk"],
  [7, "first_start_preheat", "Eerste-startvoorverwarming"],
  [8, "gas_discharge_temperature", "Persgastemperatuur te hoog"],
  [9, "evaporator_coil_temperature", "Verdampertemperatuur buiten bereik"],
  [10, "ac_voltage", "Netspanning buiten bereik"],
  [11, "ambient_temperature_range", "Buitentemperatuur buiten werkgebied"],
  [12, "ambient_temperature_frequency_limit", "Vermogen begrensd door buitentemperatuur"],
  [13, "low_pressure_switch", "Lagedrukbeveiliging"],
  [14, "low_pressure_speed_limit", "Toerental begrensd door lage druk"],
  [17, "ambient_temperature_sensor", "Buitentemperatuursensor"],
  [18, "evaporator_coil_temperature_sensor", "Verdampertemperatuursensor"],
  [19, "gas_discharge_temperature_sensor", "Persgastemperatuursensor"],
  [20, "gas_return_temperature_sensor", "Zuiggastemperatuursensor"],
  [21, "evaporator_pressure_sensor_lock", "Verdamperdruksensor vergrendeld"],
  [22, "condenser_pressure_sensor", "Condensordruksensor"],
  [23, "high_pressure_switch_lock", "Hogedrukbeveiliging vergrendeld"],
  [24, "low_pressure_switch_lock", "Lagedrukbeveiliging vergrendeld"],
  [25, "fan", "Ventilatorstoring"],
  [27, "evaporating_pressure_lock", "Verdamperdruk vergrendeld"],
  [28, "condenser_pressure_lock", "Condensordruk vergrendeld"],
  [30, "evi_pressure_sensor", "EVI-druksensor"],
  [31, "evi_inlet_temperature_sensor", "EVI-inlaattemperatuursensor"],
  [32, "evi_outlet_temperature_sensor", "EVI-uitlaattemperatuursensor"],
  [33, "odu_master_slave_communication", "Communicatie tussen buitenunits"],
  [34, "odu_control_pcb_communication", "Communicatie met ODU-regelprint"],
  [35, "compressor_phase_current_failure", "Compressorfasestroomstoring"],
  [36, "compressor_phase_current_overload", "Compressorfasestroom overbelast"],
  [37, "compressor_driver", "Compressordriver"],
  [38, "module_vdc_voltage", "DC-tussenkringspanning"],
  [39, "ac_current", "AC-stroommeting"],
  [40, "eeprom", "ODU-geheugen"],
  [41, "fan_drive_pcb", "Ventilatorregelprint"],
  [42, "inlet_water_temperature_sensor", "Inlaatwatertemperatuursensor"],
  [43, "outlet_water_temperature_sensor", "Uitlaatwatertemperatuursensor"],
  [44, "inner_coil_temperature_sensor", "Binnenste-wisselaartemperatuursensor"],
  [46, "dc_water_pump", "Waterpomp in buitenunit"],
  [1001, "hp_link_loss", "Verbinding met warmtepomp bevestigd weg"],
  [1002, "hp_start_failed", "Warmtepompstart niet bevestigd"],
  [1003, "hp_stop_unconfirmed", "Warmtepompstop niet bevestigd"],
  [1004, "hp_manual_reset_persistence_failure", "Opslag van handmatige resetstatus mislukt"],
];
const INCIDENT_LABEL_BY_ID = new Map(INCIDENT_CATALOG.map(([id, , label]) => [id, label]));
const INCIDENT_LABEL_BY_KEY = Object.freeze(Object.fromEntries(
  INCIDENT_CATALOG.map(([, key, label]) => [key, label]),
));

const CATEGORY_LABELS = Object.freeze({
  status: "Status",
  protection: "Beveiliging",
  warning: "Waarschuwing",
  fault: "Storing",
  unknown: "Technische melding",
});
const EFFECT_LABELS = Object.freeze({
  limit_capacity: "ODU begrenst het vermogen",
  block_start: "start blokkeren",
  stop_compressor: "compressor stoppen",
  mark_hp_unavailable: "warmtepomp niet beschikbaar",
  allow_cm4: "CM4 na systeemcontroles toestaan",
  block_boiler: "ketel blokkeren",
  require_confirmed_odu_power_cycle: "bevestigde ODU-powercycle vereist",
  pump_unavailable: "ODU-waterpomp niet beschikbaar",
});
const RECOVERY_LABELS = Object.freeze({
  when_bit_clears: "automatisch zodra de ODU-melding verdwijnt",
  stable_reads_and_recovery_window: "automatisch na stabiele herstelmetingen",
  after_stable_reads: "automatisch na meerdere stabiele metingen",
  preheat_complete: "automatisch zodra de voorverwarming klaar is",
  confirmed_odu_power_cycle: "na een uitgevoerde en bevestigde ODU-powercycle",
  stable_telemetry: "automatisch na stabiele telemetrie",
  explicit_retry_after_safe_stop: "na een veilige stop en expliciete herstart",
  fresh_stop_confirmation: "na een nieuwe bevestigde stopstatus",
  review_required: "na technische beoordeling",
});
const USER_ACTION_LABELS = Object.freeze({
  none: "",
  wait_for_automatic_recovery: "Wacht op automatisch herstel.",
  check_installation: "Controleer de installatie.",
  contact_installer: "Neem contact op met de installateur.",
});
const ACTION_RESULT_LABELS = Object.freeze({
  start_failure_cleared: "De startblokkering is vrijgegeven. De normale startvoorwaarden blijven gelden.",
  no_start_failure: "Er is geen startfout meer om vrij te geven.",
  stop_not_confirmed: "De warmtepomp is nog niet veilig als gestopt bevestigd.",
  link_not_healthy: "De verbinding met de warmtepomp is nog niet stabiel genoeg.",
  hard_fault_active: "Er is nog een actieve warmtepompstoring.",
  fault_recovery_pending: "Het automatische storingsherstel is nog niet afgerond.",
  odu_power_cycle_confirmed: "De bevestigde ODU-powercycle is verwerkt.",
  no_cleared_manual_reset_latch: "Er is geen herstelde powercycle-latch om vrij te geven.",
  persistence_unavailable: "De resetstatus kan momenteel niet veilig worden opgeslagen.",
  persistence_write_failed: "Het opslaan van de resetstatus is mislukt; de blokkering blijft actief.",
  incident_state_changed: "De incidentstatus veranderde tijdens de actie; controleer de actuele melding.",
  invalid_hp: "De gekozen warmtepomp is ongeldig.",
  hp_not_configured: "Deze warmtepomp is niet geconfigureerd.",
  queue_unavailable: "De controller kan de actie momenteel niet in de hoofdloop plaatsen.",
  action_in_progress: "Voor deze warmtepomp wordt al een incidentactie verwerkt.",
  invalid_request_id: "De incidentactie heeft geen geldig actienummer.",
  forbidden: "De beveiligingscontrole van de actie is mislukt.",
});
const FALLBACK_BLOCK_LABELS = Object.freeze([
  "Geen blokkade",
  "Handmatige override actief",
  "Commissioning actief",
  "Koeling actief",
  "Vorstbescherming actief",
  "Geen warmtevraag",
  "Ketelfallback staat uit",
  "Er is nog een warmtepomp beschikbaar",
  "Beschikbaarheid warmtepompen nog niet zeker",
  "Nog geen bevestigde fallbackoorzaak",
  "Stopstatus warmtepomp nog niet veilig bevestigd",
  "Flowmeting niet beschikbaar",
  "Waterflow onvoldoende",
  "Aanvoertemperatuur niet beschikbaar",
  "Ketelbeveiliging geeft niet vrij",
]);
const PUMP_IPWM_STATUS_LABELS = Object.freeze({
  unknown: "Onbekend",
  pwm_short: "PWM-interface kortgesloten",
  standby: "Stand-by",
  running: "Pomp draait",
  pump_on_abnormal: "PumpOnAbnormal",
  pump_off_abnormal: "PumpOffAbnormal",
  pump_off_failure: "PumpOffFailure",
  pwm_open: "PWM-interface open",
});

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
  const onOff = (value) => value ? "AAN" : "UIT";
  if (typeof pumpContext.requestOn === "boolean") {
    rows.push(["Pompaanvraag (OpenQuatt) · R2010.b12", onOff(pumpContext.requestOn)]);
  }
  if (typeof pumpContext.relayOn === "boolean") {
    rows.push(["Pomprelais · R2108.b11", onOff(pumpContext.relayOn)]);
  }
  if (typeof pumpContext.flowSwitchOn === "boolean") {
    rows.push(["Flowswitch · R2115.b13", onOff(pumpContext.flowSwitchOn)]);
  }
  const statusKey = String(pumpContext.ipwmStatus || "unknown");
  if (pumpContext.feedbackRaw !== null || statusKey !== "unknown") {
    const raw = pumpContext.feedbackRaw !== null ? `${pumpContext.feedbackRaw} raw` : "Raw onbekend";
    const status = PUMP_IPWM_STATUS_LABELS[statusKey] || statusKey;
    rows.push(["iPWM-feedback · R2137", `${raw} · ${status}`]);
  }
  if (pumpContext.pumpPowerW !== null) {
    rows.push(["Afgeleid pompvermogen", `${pumpContext.pumpPowerW.toLocaleString("nl-NL")} W`]);
  }
  if (pumpContext.flowLph !== null) {
    rows.push(["Flow · R2138", `${pumpContext.flowLph.toLocaleString("nl-NL")} L/h`]);
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
    return code ? `Niet-geclassificeerde ODU-melding (${code})` : "Niet-geclassificeerde ODU-melding";
  }
  if (INCIDENT_LABEL_BY_KEY[key]) return INCIDENT_LABEL_BY_KEY[key];
  if (INCIDENT_LABEL_BY_ID.has(id)) return INCIDENT_LABEL_BY_ID.get(id);
  if (key) {
    const words = key.replace(/^odu_/, "").replaceAll("_", " ");
    return `ODU-melding: ${words.charAt(0).toUpperCase()}${words.slice(1)}`;
  }
  const code = technicalIncidentCode(id);
  return code ? `Niet-geclassificeerde ODU-melding (${code})` : "Niet-geclassificeerde warmtepompmelding";
}

export function getIncidentCategoryLabel(category) {
  return CATEGORY_LABELS[String(category || "").toLowerCase()] || CATEGORY_LABELS.unknown;
}

export function getIncidentEffectLabels(effects = []) {
  const normalized = effects.map((effect) => String(effect).toLowerCase());
  const controlling = normalized
    .filter((effect) => effect !== "display")
    .map((effect) => EFFECT_LABELS[effect] || "")
    .filter(Boolean);
  return controlling.length ? controlling : normalized.includes("display") ? ["alleen tonen"] : [];
}

export function getIncidentRecoveryLabel(value) {
  return RECOVERY_LABELS[String(value || "").toLowerCase()] || String(value || "").replaceAll("_", " ");
}

export function getIncidentUserActionLabel(value) {
  return USER_ACTION_LABELS[String(value || "").toLowerCase()] ?? String(value || "").replaceAll("_", " ");
}

export function formatIncidentOccurrenceTime(epochS, uptimeMs) {
  const epoch = Number(epochS);
  if (Number.isFinite(epoch) && epoch >= 946684800) {
    return new Intl.DateTimeFormat("nl-NL", {
      day: "2-digit",
      month: "short",
      hour: "2-digit",
      minute: "2-digit",
    }).format(new Date(epoch * 1000));
  }
  const uptime = Number(uptimeMs);
  if (!Number.isFinite(uptime) || uptime < 0) return "";
  const minutes = Math.round(uptime / 60000);
  return `${minutes < 60 ? `${minutes} min` : `${Math.floor(minutes / 60)}u ${minutes % 60}m`} na controllerstart`;
}

export function getFallbackBlockReasonLabel(reason) {
  return FALLBACK_BLOCK_LABELS[Number(reason)] || "Onbekende veiligheidsblokkade";
}

export function getSystemActionPresentation(action) {
  return SYSTEM_ACTIONS[String(action || "").toLowerCase()] || SYSTEM_ACTIONS.none;
}

export function getIncidentLifecyclePresentation(incident = {}) {
  if (incident.active) {
    return { label: "Actief", tone: incident.severity === "fault" ? "fault" : "warning" };
  }
  if (incident.recovering) return { label: "Herstelt", tone: "warning" };
  if (incident.latched && !incident.acknowledged) {
    return { label: "Hersteld · vastgehouden", tone: "warning" };
  }
  return { label: "Hersteld", tone: "clear" };
}

export function getHeatPumpStatusPresentation(heatPump = {}) {
  const links = {
    bootstrap: "Verbinding wordt opgebouwd",
    healthy: "Verbinding gezond",
    suspect: "Korte hapering wordt eerst bevestigd",
    lost: "Verbinding bevestigd weg",
    recovering: "Verbinding herstelt",
    unknown: "Verbindingsstatus onbekend",
  };
  const runs = {
    unknown: "Compressorstatus onbekend",
    stopped: "Compressor gestopt",
    start_requested: "Start aangevraagd",
    wait_mode: "Wacht op bedrijfsmodus",
    wait_compressor: "Wacht op compressorbevestiging",
    running: "Compressor draait",
    stopping: "Stop aangevraagd",
    stop_unconfirmed: "Stop nog niet bevestigd",
  };
  const runStatus = heatPump.stopConfirmationPending
    ? "Stopstatus wordt opnieuw bevestigd"
    : runs[heatPump.runState] || runs.unknown;
  const note = `${links[heatPump.linkState] || links.unknown} · ${runStatus}`;
  if (heatPump.faultActive || heatPump.protectionState === "fault_active") {
    return { label: "Storing actief", note, tone: "fault" };
  }
  if (heatPump.linkState === "lost") return { label: "Niet beschikbaar", note, tone: "fault" };
  if (heatPump.protectionState === "start_blocked") {
    return { label: "Start tijdelijk geblokkeerd", note, tone: "warning" };
  }
  if (heatPump.protectionState === "limited") {
    return { label: "Vermogen begrensd", note, tone: "warning" };
  }
  if (heatPump.availability === "recovering" || heatPump.linkState === "recovering") {
    return { label: "Herstel wordt bevestigd", note, tone: "warning" };
  }
  if (heatPump.availableForStart || heatPump.availability === "available") {
    return { label: "Beschikbaar", note, tone: "clear" };
  }
  return {
    label: "Status wordt bepaald",
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
  const category = CATEGORY_LABELS[definition.category] ? definition.category : "unknown";
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
  const rawAction = SYSTEM_ACTIONS[system.action] ? system.action : "none";
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
    ? "ODU-powercycle"
    : "Startfout";
  if (action.pending) {
    return {
      visible: true,
      label: action.outcomeUnknown
        ? `${kindLabel}: uitkomst controleren`
        : `${kindLabel}: verwerking loopt`,
      copy: action.outcomeUnknown
        ? "Het antwoord ging verloren. OpenQuatt controleert met hetzelfde actienummer of de controller de actie heeft verwerkt."
        : "De controller heeft het verzoek geaccepteerd; OpenQuatt wacht op het resultaat met hetzelfde actienummer.",
      tone: "warning",
    };
  }
  if (action.ok === true) {
    return {
      visible: true,
      label: `${kindLabel}: uitgevoerd`,
      copy: ACTION_RESULT_LABELS[action.result] || "De controller heeft de actie bevestigd.",
      tone: "clear",
    };
  }
  if (action.ok === false) {
    return {
      visible: true,
      label: `${kindLabel}: niet uitgevoerd`,
      copy: ACTION_RESULT_LABELS[action.result] || String(action.message || "De controller heeft de actie geweigerd."),
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
  if (hpIndex !== 1 && hpIndex !== 2) throw new Error(ACTION_RESULT_LABELS.invalid_hp);
  const actionRequestId = normalizeInteger(requestId, 0);
  if (actionRequestId < 1) {
    throw incidentActionRequestError(
      ACTION_RESULT_LABELS.invalid_request_id,
      true,
    );
  }
  const expectedAction = endpoint.endsWith("/retry-start")
    ? "start_failure_retry"
    : endpoint.endsWith("/confirm-odu-power-cycle")
      ? "confirm_odu_power_cycle"
      : "";
  if (!expectedAction) throw new Error("Onbekende incidentactie.");

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
      networkError?.message || "Geen antwoord van de controller.",
      false,
    );
  }
  if (response.status === 403 && typeof refreshCsrfToken === "function") {
    token = String(await refreshCsrfToken() || "");
    try {
      response = await post(token);
    } catch (error) {
      throw incidentActionRequestError(
        error?.message || "Geen antwoord van de controller.",
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
      ACTION_RESULT_LABELS[result]
        || `Incidentactie HTTP ${response.status}`,
      true,
    );
  }
  const actionId = normalizeInteger(payload.action_id, 0);
  if (payload.hp !== hpIndex
      || payload.action !== expectedAction
      || actionId !== actionRequestId) {
    throw incidentActionRequestError(
      "De controller gaf geen geldige actiebevestiging terug.",
      false,
    );
  }
  return { hp: hpIndex, action: expectedAction, actionId, csrfToken: token };
}

const incidentVisible = (incident) => incident.active
  || incident.recovering
  || (incident.lifecycle === "cleared" && incident.latched && !incident.acknowledged);

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
      title: "Geen incidentgegevens",
      copy: "",
      problemCount: 0,
      activeIncidentCount: 0,
      recoveredIncidentCount: 0,
      systemAction: "none",
      systemActionLabel: SYSTEM_ACTIONS.none.label,
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
  const problems = visible.map((incident) => ({
    key: `incident:${incident.subject}:${incident.id}`,
    label: `${incident.subject === "hp1" ? "Warmtepomp 1" : "Warmtepomp 2"}: ${getIncidentDisplayLabel(incident)}`,
    severity: recoveredIncidents.includes(incident) ? "attention" : incident.severity,
      incidentId: incident.id,
  }));
  if (actionAttention) {
    problems.unshift({
      key: `system-action:${action}`,
      label: actionPresentation.label,
      severity: actionPresentation.severity,
      incidentId: "",
    });
  }

  let title = "Geen bijzonderheden";
  let copy = "OpenQuatt ziet op dit moment geen actieve incidenten.";
  if (action === "boiler_fallback" || action === "fallback_blocked") {
    title = actionPresentation.label;
    copy = actionPresentation.copy;
  } else if (activeIncidents.some((incident) => incident.severity === "fault")) {
    title = "Storing actief";
    copy = `${activeIncidents.length} actief incident${activeIncidents.length === 1 ? "" : "en"} zichtbaar.`;
  } else if (activeIncidents.length) {
    title = "Aandacht nodig";
    copy = `${activeIncidents.length} actief aandachtspunt${activeIncidents.length === 1 ? "" : "en"} zichtbaar.`;
  } else if (recoveredIncidents.length) {
    title = "Eerdere melding nog niet bevestigd";
    copy = `${recoveredIncidents.length} hersteld incident${recoveredIncidents.length === 1 ? "" : "en"} blijft zichtbaar tot bevestiging.`;
  }
  if (action === "fallback_blocked") {
    const reason = snapshot.system.fallbackBlockReason;
    copy = `${copy} ${reason
      ? `Blokkade: ${getFallbackBlockReasonLabel(reason)}.`
      : "Er is geen blokkadereden aangeleverd; de ketelopdracht blijft inactief."}`;
  }
  if (snapshot.system.boilerCommandActive
      && snapshot.system.boilerTransition === "assist_to_fallback_continuous") {
    copy = `${copy} De controller gaf tijdens de rolwisseling geen uit/aan-puls.`;
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
    copy += ` Daarnaast zijn ${base.problems.length} bestaande aandachtspunt${base.problems.length === 1 ? "" : "en"} zichtbaar.`;
  } else if (!incidentDominates && incidentMonitoring.active) {
    copy += ` Daarnaast zijn ${incidentMonitoring.problemCount} incidentmelding${incidentMonitoring.problemCount === 1 ? "" : "en"} zichtbaar.`;
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
  const failureMessage = String(error?.message || error || "Incidentgegevens konden niet worden bijgewerkt.");
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
