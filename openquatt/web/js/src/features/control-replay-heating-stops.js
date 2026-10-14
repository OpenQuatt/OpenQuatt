import { formatNumber, t } from "../i18n/index.js";

const reasons = {
  heating_request_cleared: ["heatingStops.demandZero", "heatingStops.requestEndedCopy", "heatingStops.curveNext"],
  supervisory_override: ["heatingStops.override", "heatingStops.overrideCopy", "heatingStops.centralNext"],
  frost_protection: ["controlReplay.reasonFrost", "controlReplay.reasonFrostCopy", "heatingStops.centralNext"],
  heating_central_pause: ["heatingStops.central", "heatingStops.centralCopy", "heatingStops.centralNext"],
  ph_demand_zero: ["heatingStops.demandZero", "heatingStops.demandZeroCopy", "heatingStops.demandNext"],
  ph_low_load: ["heatingStops.lowLoad", "heatingStops.lowLoadCopy", "heatingStops.lowLoadNext"],
  ph_comfort_stop: ["heatingStops.comfort", "heatingStops.comfortCopy", "heatingStops.comfortNext"],
  ph_comfort_hold: ["heatingStops.comfortHold", "heatingStops.comfortHoldCopy", "heatingStops.comfortNext"],
  curve_water_stop: ["heatingStops.water", "heatingStops.waterCopy", "heatingStops.curveNext"],
  curve_low_load: ["heatingStops.lowLoad", "heatingStops.curveLowCopy", "heatingStops.curveNext"],
  heating_input_invalid: ["heatingStops.invalid", "heatingStops.invalidCopy", "heatingStops.validNext"],
  heating_single_sufficient: ["heatingStops.single", "heatingStops.singleCopy", "heatingStops.singleNext"],
  heating_electrical_limit: ["heatingStops.electrical", "heatingStops.electricalCopy", "heatingStops.electricalNext"],
  heating_no_candidate: ["heatingStops.noCandidate", "heatingStops.noCandidateCopy", "heatingStops.candidateNext"],
  heating_disabled: ["heatingStops.disabled", "heatingStops.disabledCopy", "heatingStops.enabledNext"],
  heating_water_limit: ["heatingStops.waterLimit", "heatingStops.waterLimitCopy", "heatingStops.safetyNext"],
  heating_flow_limit: ["heatingStops.flowLimit", "heatingStops.flowLimitCopy", "heatingStops.safetyNext"],
  heating_source_unavailable: ["heatingStops.unavailable", "heatingStops.unavailableCopy", "heatingStops.candidateNext"],
  heating_optimizer: ["heatingStops.optimizer", "heatingStops.optimizerCopy", "heatingStops.optimizerNext"],
};

export function getHeatingStopReasonMeta(reason) {
  const keys = reasons[reason];
  return keys ? { labelKey: keys[0], summaryKey: keys[1], checkKeys: [] } : null;
}

// A truncated archive/page must never borrow another stop's context. Firmware
// writes exactly three context records immediately before the command record.
export function attachHeatingStopSnapshots(events) {
  return events.map((event, index) => {
    if (event.event_type !== "source_stop" || !(Number(event.flags) & 32) || !reasons[event.reason]) return event;
    const context = events.slice(index - 3, index);
    if (context.length !== 3 || context.some((item, offset) =>
      item.event_type !== "heating_stop_context" || item.subject !== event.subject || item.reason !== event.reason
      || item.cm !== event.cm || !Number.isInteger(item.seq) || item.seq !== event.seq - 3 + offset
      || Number(item.flags) !== offset + 1
      || item.duration_s !== context[0].duration_s)) return event;
    return { ...event, _oq_heating_stop_snapshot: { power: context[0], room: context[1], water: context[2] } };
  });
}

function hasValue(value) {
  return typeof value === "number" && Number.isInteger(value) && value >= -32767 && value <= 32767;
}

function snapshotValue(value, decimals, scale = 1) {
  return hasValue(value)
    ? `${value === 32767 ? "≥ " : value === -32767 ? "≤ " : ""}${formatNumber(value / scale, { minimumFractionDigits: decimals, maximumFractionDigits: decimals })}` : "—";
}

// Format the full readings only for the selected stop, not every timeline row.
export function getHeatingStopSupportValues(event) {
  const snapshot = event?._oq_heating_stop_snapshot;
  if (!snapshot) return [t("heatingStops.missing")];
  return [
    hasValue(snapshot.power.value_a) || hasValue(snapshot.power.value_b) ? t("heatingStops.powerValues", {
      base: snapshotValue(snapshot.power.value_a, 0), effective: snapshotValue(snapshot.power.value_b, 0),
      thermal: snapshotValue(snapshot.power.threshold_a, 0),
    }) : t("heatingStops.thermalValue", { thermal: snapshotValue(snapshot.power.threshold_a, 0) }),
    [snapshot.room.value_a, snapshot.room.value_b, snapshot.room.threshold_a].some(hasValue) ? t("heatingStops.roomValues", {
      room: snapshotValue(snapshot.room.value_a, 2, 100), stop: snapshotValue(snapshot.room.threshold_a, 2, 100),
      restart: snapshotValue(snapshot.room.value_b, 2, 100),
    }) : "",
    event.reason === "ph_low_load" ? t("heatingStops.lowLoadValues", {
      stop: snapshotValue(snapshot.water.threshold_a, 0), restart: snapshotValue(snapshot.water.value_b, 0),
    }) : [snapshot.water.value_a, snapshot.water.value_b, snapshot.water.threshold_a].some(hasValue) ? t("heatingStops.waterValues", {
      water: snapshotValue(snapshot.water.value_a, 2, 100), stop: snapshotValue(snapshot.water.threshold_a, 2, 100),
      restart: snapshotValue(snapshot.water.value_b, 2, 100),
    }) : "",
    t("heatingStops.age", { seconds: snapshot.power.duration_s }),
  ].filter(Boolean);
}

export function getHeatingStopCopy(event, subject) {
  if (["heating_request_cleared", "supervisory_override", "frost_protection"].includes(event.reason) && !(Number(event.flags) & 32)) return null;
  const keys = event.event_type === "source_stop" && reasons[event.reason];
  if (!keys) return null;
  const snapshot = event._oq_heating_stop_snapshot;
  const lowLoad = event.reason === "ph_low_load";
  let snapshotSummary = "";
  if (snapshot) {
    const powerValue = (value) => Math.abs(value) === 32767 ? `${snapshotValue(value, 0)} W` : `${snapshotValue(value, 1, 1000)} kW`;
    if (["ph_comfort_stop", "ph_comfort_hold"].includes(event.reason) && hasValue(snapshot.room.value_a) && hasValue(snapshot.room.threshold_a)) {
      snapshotSummary = t("heatingStops.roomBrief", {
        room: snapshotValue(snapshot.room.value_a, 2, 100), stop: snapshotValue(snapshot.room.threshold_a, 2, 100),
      });
    } else if (lowLoad && hasValue(snapshot.power.value_b) && hasValue(snapshot.water.threshold_a)) {
      snapshotSummary = t("heatingStops.lowLoadBrief", {
        demand: powerValue(snapshot.power.value_b), stop: powerValue(snapshot.water.threshold_a),
      });
    } else if (["curve_water_stop", "curve_low_load"].includes(event.reason) && hasValue(snapshot.water.value_a) && hasValue(snapshot.water.threshold_a)) {
      snapshotSummary = t("heatingStops.waterBrief", {
        water: snapshotValue(snapshot.water.value_a, 2, 100), stop: snapshotValue(snapshot.water.threshold_a, 2, 100),
      });
    }
  }
  const summary = t(keys[1]);
  let next = t(keys[2]);
  if (snapshot && hasValue(snapshot.room.value_b) && ["ph_comfort_stop", "ph_comfort_hold"].includes(event.reason)) {
    next = t("heatingStops.comfortRestart", { value: snapshotValue(snapshot.room.value_b, 2, 100) });
  } else if (snapshot && lowLoad && hasValue(snapshot.water.value_b)) {
    next = t("heatingStops.lowLoadRestart", { value: snapshotValue(snapshot.water.value_b, 0) });
  }
  return {
    title: t("heatingStops.title", { subject, reason: t(keys[0]) }), summary,
    reasonLabel: t(keys[0]), reasonSummary: summary,
    detail: t("heatingStops.residual"), next, checks: [], snapshotSummary, heatingStop: true,
  };
}
