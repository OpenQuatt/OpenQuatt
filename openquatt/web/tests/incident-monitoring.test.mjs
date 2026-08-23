import assert from "node:assert/strict";
import test from "node:test";

import {
  combineInstallationMonitoringModel,
  createIncidentActionRequestId,
  formatIncidentOccurrenceTime,
  getFallbackBlockReasonLabel,
  getHeatPumpStatusPresentation,
  getIncidentActionPresentation,
  getIncidentDisplayLabel,
  getIncidentEffectLabels,
  getIncidentMonitoringFailureUpdate,
  getIncidentMonitoringSuccessUpdate,
  getIncidentMonitoringUnsupportedUpdate,
  getIncidentRecoveryLabel,
  normalizeIncidentMonitoringSnapshot,
  postIncidentActionRequest,
  summarizeIncidentMonitoring,
} from "../js/src/core/incident-monitoring.js";

function snapshot(overrides = {}) {
  return {
    schema_version: 1,
    generated_at_s: 1_721_234_567,
    system: {
      control_mode: 2,
      action: "none",
    },
    incidents: [],
    heat_pumps: [],
    ...overrides,
  };
}

test("incident snapshot parser rejects malformed and unsupported payloads without throwing", () => {
  assert.deepEqual(
    {
      valid: normalizeIncidentMonitoringSnapshot("{").valid,
      error: normalizeIncidentMonitoringSnapshot("{").error,
    },
    { valid: false, error: "invalid_payload" },
  );
  assert.equal(normalizeIncidentMonitoringSnapshot({}).error, "missing_schema_version");
  const unsupported = normalizeIncidentMonitoringSnapshot({ schema_version: 2 });
  assert.equal(unsupported.valid, false);
  assert.equal(unsupported.schemaVersion, 2);
  assert.equal(unsupported.error, "unsupported_schema_version");

  const rawPayloadWithValidityFlag = summarizeIncidentMonitoring({
    schema_version: 1,
    valid: true,
    system: { control_mode: 4, boiler_command_active: true },
  });
  assert.equal(rawPayloadWithValidityFlag.available, true);
  assert.equal(rawPayloadWithValidityFlag.systemAction, "boiler_fallback");
});

test("incident snapshot normalizes HP state, lifecycle and stable machine fields", () => {
  const normalized = normalizeIncidentMonitoringSnapshot(snapshot({
    heat_pumps: [{
      index: 1,
      link_state: "lost",
      protection_state: "fault_active",
      run_state: "stop_unconfirmed",
      availability: "unavailable",
      available_for_start: false,
      must_stop: true,
      primary_incident_id: "ODU_R2120_B4",
      incidents: [{
        definition: {
          id: 21,
          key: "evaporator_pressure_sensor_lock",
          category: "fault",
          severity: "fault",
          effects: ["stop_compressor", "mark_hp_unavailable", "allow_cm4", "allow_cm4"],
          effect_mask: 37,
          register_address: 2120,
          bit: 4,
        },
        runtime: {
          lifecycle: "active",
          raw_active: true,
          confirmed_active: true,
          first_seen_s: 1_721_234_500,
          last_seen_s: 1_721_234_560,
          occurrence_count: 3,
        },
      }],
    }],
  }));

  assert.equal(normalized.valid, true);
  assert.equal(normalized.heatPumps[0].linkState, "lost");
  assert.equal(normalized.heatPumps[0].runState, "stop_unconfirmed");
  assert.equal(normalized.heatPumps[0].mustStop, true);
  assert.equal(normalized.heatPumps[0].stopConfirmationPending, false);
  assert.deepEqual(
    normalized.heatPumps[0].incidents[0].effects,
    ["stop_compressor", "mark_hp_unavailable", "allow_cm4"],
  );
  assert.equal(normalized.heatPumps[0].incidents[0].effectMask, 37);
  assert.equal(normalized.heatPumps[0].incidents[0].active, true);
  assert.equal(normalized.heatPumps[0].incidents[0].occurrenceCount, 3);
});

test("incident snapshot accepts the engine definition/runtime shape and derives availability", () => {
  const normalized = normalizeIncidentMonitoringSnapshot(snapshot({
    heat_pumps: [{
      index: 2,
      link_state: "healthy",
      protection_state: "fault_active",
      run_state: "stopped",
      must_stop: true,
      fault_active: true,
      fallback_cause_present: true,
      fallback_eligible: true,
      incidents: [{
        definition: {
          id: 34,
          key: "odu_r2121_b1",
          category: "fault",
          severity: "warning",
          effects: ["display", "stop_compressor"],
          effect_mask: 9,
          register_address: 2121,
          bit: 1,
          recovery_condition: "confirmed_odu_power_cycle",
          user_action: "Power-cycle the outdoor unit and confirm.",
        },
        runtime: {
          lifecycle: "active",
          raw_active: true,
          confirmed_active: true,
          latched: true,
          first_seen_ms: 1_000,
          last_seen_ms: 2_000,
          occurrence_count: 2,
        },
      }],
    }],
  }));
  const heatPump = normalized.heatPumps[0];
  const incident = heatPump.incidents[0];

  assert.equal(heatPump.availability, "unavailable");
  assert.equal(heatPump.faultActive, true);
  assert.equal(incident.id, "34");
  assert.equal(incident.key, "odu_r2121_b1");
  assert.equal(incident.severity, "attention");
  assert.equal(incident.lifecycle, "active");
  assert.equal(incident.firstSeenMs, 1_000);
  assert.equal(incident.register, 2121);
  assert.equal(incident.recoveryCondition, "confirmed_odu_power_cycle");
  assert.equal(incident.userAction, "Power-cycle the outdoor unit and confirm.");
});

test("status is not raised as a problem while protection and latched recovery remain distinct", () => {
  const summary = summarizeIncidentMonitoring(snapshot({
    heat_pumps: [{
      index: 1,
      incidents: [
        {
          definition: {
            id: 4,
            key: "compressor_oil_return",
            category: "status",
            severity: "info",
            display_label: "Olie-retour actief",
          },
          runtime: { lifecycle: "active", confirmed_active: true },
        },
        {
          definition: {
            id: 7,
            key: "first_start_preheat",
            category: "protection",
            severity: "warning",
            display_label: "Eerste-startvoorverwarming",
          },
          runtime: { lifecycle: "active", confirmed_active: true },
        },
        {
          definition: {
            id: 21,
            key: "evaporator_pressure_sensor_lock",
            category: "fault",
            severity: "fault",
            display_label: "Druksensorvergrendeling hersteld",
          },
          runtime: {
            lifecycle: "latched",
            latched: true,
            acknowledged: false,
          },
        },
      ],
    }],
  }));

  assert.equal(summary.active, true);
  assert.equal(summary.activeIncidentCount, 1);
  assert.equal(summary.recoveredIncidentCount, 1);
  assert.equal(summary.severity, "attention");
  assert.deepEqual(
    summary.problems.map((problem) => problem.incidentId),
    ["7", "21"],
  );
  assert.equal(summary.problems.some((problem) => problem.incidentId === "4"), false);
});

test("CM3 assist and CM4 fallback have separate machine actions and presentation", () => {
  const assist = summarizeIncidentMonitoring(snapshot({
    system: {
      control_mode: 3,
      action: "none",
      boiler_role: "fallback",
      boiler_command_active: true,
    },
  }));
  assert.equal(assist.systemAction, "boiler_assist");
  assert.equal(assist.boilerRole, "assist");
  assert.equal(assist.active, false);
  assert.equal(assist.systemActionLabel, "CV ondersteunt tijdelijk");

  const fallback = summarizeIncidentMonitoring(snapshot({
    system: {
      control_mode: 4,
      action: "none",
      boiler_role: "assist",
      previous_boiler_role: "assist",
      boiler_command_active: true,
      boiler_output_continuous: true,
    },
  }));
  assert.equal(fallback.systemAction, "boiler_fallback");
  assert.equal(fallback.boilerRole, "fallback");
  assert.equal(fallback.boilerTransition, "assist_to_fallback_continuous");
  assert.equal(fallback.active, true);
  assert.equal(fallback.severity, "fault");
  assert.equal(fallback.title, "Ketel neemt verwarming over");
  assert.match(fallback.copy, /geen uit\/aan-puls/);
  assert.equal(fallback.problems.some((problem) => problem.key === "system-action:boiler_assist"), false);

  const unknownContinuity = summarizeIncidentMonitoring(snapshot({
    system: {
      control_mode: 4,
      previous_boiler_role: "assist",
      boiler_output_continuous: "unknown",
    },
  }));
  assert.equal(unknownContinuity.boilerTransition, "assist_to_fallback");
  assert.doesNotMatch(unknownContinuity.copy, /bleef tijdens de rolwisseling actief/);
});

test("fallback blocked remains separate from inactive CM4 and explains the system action", () => {
  const summary = summarizeIncidentMonitoring(snapshot({
    system: {
      control_mode: 1,
      action: "fallback_blocked",
      boiler_role: "off",
      boiler_command_active: false,
    },
  }));

  assert.equal(summary.systemAction, "fallback_blocked");
  assert.equal(summary.boilerRole, "off");
  assert.equal(summary.severity, "fault");
  assert.equal(summary.title, "Ketelfallback niet vrijgegeven");
  assert.equal(summary.problems[0].key, "system-action:fallback_blocked");
});

test("CM3 preserves an explicit fallback block during safe handover", () => {
  const summary = summarizeIncidentMonitoring(snapshot({
    system: {
      control_mode: 3,
      action: "fallback_blocked",
      boiler_role: "assist",
      boiler_command_active: true,
      fallback_block_reason: 10,
    },
  }));

  assert.equal(summary.systemAction, "fallback_blocked");
  assert.equal(summary.boilerRole, "assist");
  assert.equal(summary.title, "Ketelfallback niet vrijgegeven");
  assert.match(summary.copy, /Stopstatus warmtepomp/);
});

test("CM4 role without active boiler command is presented as blocked, not as delivered heat", () => {
  const summary = summarizeIncidentMonitoring(snapshot({
    system: {
      control_mode: 4,
      action: "boiler_fallback",
      boiler_role: "fallback",
      boiler_command_active: false,
      fallback_block_reason: 12,
    },
  }));

  assert.equal(summary.systemAction, "fallback_blocked");
  assert.equal(summary.snapshot.system.boilerRole, "fallback");
  assert.equal(summary.snapshot.system.boilerCommandActive, false);
  assert.match(summary.copy, /Waterflow onvoldoende/);
  assert.equal(getFallbackBlockReasonLabel(12), "Waterflow onvoldoende");
});

test("catalog and synthetic incident fallbacks never expose a bare incident id", () => {
  assert.equal(
    getIncidentDisplayLabel({ id: 22 }),
    "Condensordruksensor",
  );
  assert.equal(
    getIncidentDisplayLabel({ id: 1001 }),
    "Verbinding met warmtepomp bevestigd weg",
  );
  assert.equal(
    getIncidentDisplayLabel({ id: 1004 }),
    "Opslag van handmatige resetstatus mislukt",
  );
  assert.equal(
    getIncidentDisplayLabel({ id: 26 }),
    "Niet-geclassificeerde ODU-melding (R2120.b9)",
  );
  assert.equal(
    getIncidentDisplayLabel({
      id: 29,
      key: "unclassified_odu_fault",
      register: 2120,
      bit: 12,
    }),
    "Niet-geclassificeerde ODU-melding (R2120.b12)",
  );
  assert.notEqual(
    getIncidentDisplayLabel({ id: 26, key: "unclassified_odu_fault" }),
    getIncidentDisplayLabel({ id: 29, key: "unclassified_odu_fault" }),
  );
  assert.equal(
    getIncidentDisplayLabel({ key: "future_fault_name" }),
    "ODU-melding: Future fault name",
  );
  assert.deepEqual(getIncidentEffectLabels(["display"]), ["alleen tonen"]);
  assert.deepEqual(
    getIncidentEffectLabels(["display", "block_start", "stop_compressor"]),
    ["start blokkeren", "compressor stoppen"],
  );
  assert.equal(
    getIncidentRecoveryLabel("after_stable_reads"),
    "automatisch na meerdere stabiele metingen",
  );
  assert.match(formatIncidentOccurrenceTime(1_721_234_500, 0), /\d{2}:\d{2}/);
  assert.equal(formatIncidentOccurrenceTime(0, 5_400_000), "1u 30m na controllerstart");
});

test("a suspect HP link is shown as confirmation in progress instead of an outage", () => {
  const presentation = getHeatPumpStatusPresentation({
    linkState: "suspect",
    protectionState: "clear",
    runState: "running",
    availability: "unknown",
  });

  assert.equal(presentation.label, "Status wordt bepaald");
  assert.equal(presentation.tone, "clear");
  assert.match(presentation.note, /eerst bevestigd/);
});

test("stop revalidation is presented as a neutral pending status", () => {
  const normalized = normalizeIncidentMonitoringSnapshot(snapshot({
    heat_pumps: [{
      index: 1,
      link_state: "healthy",
      protection_state: "clear",
      run_state: "stopping",
      stop_confirmation_pending: true,
    }],
  }));
  const heatPump = normalized.heatPumps[0];
  const presentation = getHeatPumpStatusPresentation(heatPump);

  assert.equal(heatPump.stopConfirmationPending, true);
  assert.equal(presentation.label, "Status wordt bepaald");
  assert.equal(presentation.note, "Verbinding gezond · Stopstatus wordt opnieuw bevestigd");
  assert.equal(presentation.tone, "warning");
});

test("incident polling keeps last-good data through transient failures and cleanly handles 404", () => {
  const success = getIncidentMonitoringSuccessUpdate({}, snapshot({
    heat_pumps: [{ index: 1, link_state: "healthy", available_for_start: true }],
  }), 1000);
  const firstFailure = getIncidentMonitoringFailureUpdate(success, new Error("timeout"), 2000);
  const secondFailure = getIncidentMonitoringFailureUpdate(firstFailure, new Error("timeout"), 3000);
  const thirdFailure = getIncidentMonitoringFailureUpdate(secondFailure, new Error("timeout"), 4000);

  assert.equal(firstFailure.incidentMonitoringSnapshot.valid, true);
  assert.equal(firstFailure.incidentMonitoringError, "");
  assert.equal(firstFailure.changed, false);
  assert.equal(secondFailure.incidentMonitoringError, "");
  assert.equal(thirdFailure.incidentMonitoringError, "timeout");
  assert.equal(thirdFailure.incidentMonitoringSnapshot.valid, true);

  const unsupported = getIncidentMonitoringUnsupportedUpdate({}, 5000);
  assert.equal(unsupported.incidentMonitoringUnsupported, true);
  assert.equal(unsupported.incidentMonitoringSnapshot, null);
  assert.equal(unsupported.incidentMonitoringError, "");
});

test("incident polling marks a stale authentication session immediately", () => {
  const success = getIncidentMonitoringSuccessUpdate({}, snapshot({
    heat_pumps: [{ index: 1, link_state: "healthy", available_for_start: true }],
  }), 1000);
  const unauthorized = getIncidentMonitoringFailureUpdate(
    success,
    new Error("Incident monitoring HTTP 401"),
    2000,
  );

  assert.equal(unauthorized.incidentMonitoringFailureCount, 1);
  assert.equal(unauthorized.incidentMonitoringError, "Incident monitoring HTTP 401");
  assert.equal(unauthorized.incidentMonitoringSnapshot.valid, true);
  assert.equal(unauthorized.changed, true);
});

test("incident polling ignores the transport timestamp when the state is unchanged", () => {
  const first = getIncidentMonitoringSuccessUpdate({}, snapshot({
    generated_at_s: 100,
    heat_pumps: [{ index: 1, link_state: "healthy", available_for_start: true }],
  }), 1000);
  const second = getIncidentMonitoringSuccessUpdate(first, snapshot({
    generated_at_s: 102,
    heat_pumps: [{ index: 1, link_state: "healthy", available_for_start: true }],
  }), 3000);

  assert.equal(first.changed, true);
  assert.equal(second.changed, false);
  assert.equal(second.incidentMonitoringSnapshot.generatedAtS, 102);
});

test("incident snapshot normalizes action token and deferred result", () => {
  const normalized = normalizeIncidentMonitoringSnapshot(snapshot({
    action_csrf_token: "token-1",
    heat_pumps: [{
      index: 1,
      last_action_result: {
        sequence: 8,
        request_id: 17,
        action: "start_failure_retry",
        ok: true,
        result: "start_failure_cleared",
        at_ms: 1234,
      },
    }],
  }));

  assert.equal(normalized.actionCsrfToken, "token-1");
  assert.deepEqual(normalized.heatPumps[0].lastActionResult, {
    sequence: 8,
    requestId: 17,
    action: "start_failure_retry",
    ok: true,
    result: "start_failure_cleared",
    atMs: 1234,
  });
});

test("incident action retries one 403 with a refreshed token and validates the 202 contract", async () => {
  const calls = [];
  const fetcher = async (_endpoint, init) => {
    const body = new URLSearchParams(init.body);
    calls.push({
      csrf: body.get("csrf_token"),
      requestId: body.get("request_id"),
    });
    if (calls.length === 1) {
      return { status: 403, json: async () => ({ accepted: false, result: "forbidden" }) };
    }
    return {
      status: 202,
      json: async () => ({
        accepted: true,
        hp: 2,
        action: "confirm_odu_power_cycle",
        action_id: 91,
      }),
    };
  };

  const accepted = await postIncidentActionRequest(
    fetcher,
    "/openquatt/incidents/confirm-odu-power-cycle",
    2,
    91,
    "stale-token",
    async () => "fresh-token",
  );
  assert.deepEqual(calls, [
    { csrf: "stale-token", requestId: "91" },
    { csrf: "fresh-token", requestId: "91" },
  ]);
  assert.equal(accepted.actionId, 91);
  assert.equal(accepted.action, "confirm_odu_power_cycle");
});

test("incident action retries a lost response with the same idempotency id", async () => {
  const requestIds = [];
  let callCount = 0;
  const accepted = await postIncidentActionRequest(
    async (_endpoint, init) => {
      requestIds.push(new URLSearchParams(init.body).get("request_id"));
      callCount += 1;
      if (callCount === 1) throw new Error("connection reset");
      return {
        status: 202,
        json: async () => ({
          accepted: true,
          duplicate: true,
          hp: 1,
          action: "start_failure_retry",
          action_id: 77,
        }),
      };
    },
    "/openquatt/incidents/retry-start",
    1,
    77,
    "csrf",
  );
  assert.deepEqual(requestIds, ["77", "77"]);
  assert.equal(accepted.actionId, 77);
});

test("incident action request ids never use zero", () => {
  const requestId = createIncidentActionRequestId({
    getRandomValues(values) {
      values[0] = 0;
      return values;
    },
  });
  assert.ok(requestId > 0);
});

test("incident action result resolves only the exact request id", () => {
  const action = {
    hp: 1,
    kind: "start_failure_retry",
    requestId: 17,
    pending: true,
    ok: null,
    result: "",
  };
  const wrong = getIncidentMonitoringSuccessUpdate(
    { incidentAction: action },
    snapshot({
      heat_pumps: [{
        index: 1,
        last_action_result: {
          sequence: 9,
          request_id: 16,
          action: "start_failure_retry",
          ok: true,
          result: "start_failure_cleared",
          at_ms: 200,
        },
      }],
    }),
  );
  assert.equal(wrong.incidentAction, action);
  assert.equal(wrong.incidentAction.pending, true);

  const matching = getIncidentMonitoringSuccessUpdate(
    wrong,
    snapshot({
      heat_pumps: [{
        index: 1,
        last_action_result: {
          sequence: 10,
          request_id: 17,
          action: "start_failure_retry",
          ok: false,
          result: "stop_not_confirmed",
          at_ms: 300,
        },
      }],
    }),
  );
  assert.equal(matching.incidentAction.pending, false);
  assert.equal(matching.incidentAction.ok, false);
  assert.equal(matching.incidentAction.result, "stop_not_confirmed");
  assert.match(getIncidentActionPresentation(matching.incidentAction, 1).copy, /veilig als gestopt/);
});

test("incident action result can resolve from bounded result history", () => {
  const action = {
    hp: 1,
    kind: "start_failure_retry",
    requestId: 17,
    pending: true,
    ok: null,
    result: "",
  };
  const update = getIncidentMonitoringSuccessUpdate(
    { incidentAction: action },
    snapshot({
      heat_pumps: [{
        index: 1,
        last_action_result: {
          sequence: 11,
          request_id: 18,
          action: "confirm_odu_power_cycle",
          ok: true,
          result: "odu_power_cycle_confirmed",
          at_ms: 400,
        },
        action_results: [{
          sequence: 10,
          request_id: 17,
          action: "start_failure_retry",
          ok: true,
          result: "start_failure_cleared",
          at_ms: 300,
        }],
      }],
    }),
  );
  assert.equal(update.incidentAction.pending, false);
  assert.equal(update.incidentAction.requestId, 17);
  assert.equal(update.incidentAction.ok, true);
});

test("incident action presentation distinguishes pending, success and refusal", () => {
  assert.equal(getIncidentActionPresentation({
    hp: 1,
    kind: "start_failure_retry",
    pending: true,
  }, 1).tone, "warning");
  assert.equal(getIncidentActionPresentation({
    hp: 1,
    kind: "start_failure_retry",
    pending: false,
    ok: true,
    result: "start_failure_cleared",
  }, 1).tone, "clear");
  assert.equal(getIncidentActionPresentation({
    hp: 1,
    kind: "confirm_odu_power_cycle",
    pending: false,
    ok: false,
    result: "persistence_write_failed",
  }, 1).tone, "fault");
});

test("incident summary combines with existing installation monitoring without mutating it", () => {
  const base = {
    active: true,
    title: "Aandacht nodig",
    copy: "1 aandachtspunt zichtbaar.",
    problems: [{ key: "lowflowFaultActive", label: "Te lage flow" }],
    cyclingAlertLatched: false,
  };
  const merged = combineInstallationMonitoringModel(base, snapshot({
    system: {
      control_mode: 4,
      previous_boiler_role: "assist",
      boiler_command_active: true,
      boiler_output_continuous: true,
    },
    heat_pumps: [{
      index: 1,
      incidents: [{
        definition: {
          id: 37,
          key: "compressor_driver",
          category: "fault",
          severity: "fault",
          display_label: "Compressor driverstoring",
        },
        runtime: { lifecycle: "active", confirmed_active: true },
      }],
    }],
  }));

  assert.equal(base.problems.length, 1);
  assert.equal(merged.active, true);
  assert.equal(merged.severity, "fault");
  assert.equal(merged.title, "Ketel neemt verwarming over");
  assert.deepEqual(
    merged.problems.map((problem) => problem.key),
    [
      "system-action:boiler_fallback",
      "incident:hp1:37",
      "lowflowFaultActive",
    ],
  );
  assert.equal(merged.incidentMonitoring.systemAction, "boiler_fallback");
});
