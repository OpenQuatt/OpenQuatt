import assert from "node:assert/strict";
import test from "node:test";

import {
  getControlReplayIncidentDisplaySeverity,
  getControlReplayIncidentEventCopy,
  getControlReplayIncidentModeAfterEvent,
  getControlReplayIncidentModeTransition,
  getControlReplayIncidentReasonMeta,
} from "../js/src/features/control-replay-incidents.js";

test("incident start and clear receive specific labels and recovered severity", () => {
  const started = {
    event_type: "incident_start",
    subject: "HP1",
    reason: "hp_fault",
    severity: "fault",
    value_a: 22,
    flags: 1,
  };
  const cleared = { ...started, event_type: "incident_clear" };

  const startCopy = getControlReplayIncidentEventCopy(started, "Warmtepomp 1");
  const clearCopy = getControlReplayIncidentEventCopy(cleared, "Warmtepomp 1");

  assert.equal(startCopy.title, "Condensordruksensor actief");
  assert.equal(startCopy.reasonLabel, "Warmtepompstoring");
  assert.equal(clearCopy.title, "Condensordruksensor hersteld");
  assert.match(clearCopy.summary, /blijft als historie bewaard/);
  assert.equal(getControlReplayIncidentDisplaySeverity(cleared), "normal");
});

test("synthetic link, start and stop incidents have compact Dutch interpretation", () => {
  const link = getControlReplayIncidentEventCopy({
    event_type: "incident_start",
    reason: "hp_link_loss",
    value_a: 1001,
  }, "Warmtepomp 2");
  const start = getControlReplayIncidentEventCopy({
    event_type: "incident_start",
    reason: "hp_start_failed",
    value_a: 1002,
  }, "Warmtepomp 1");
  const stop = getControlReplayIncidentEventCopy({
    event_type: "incident_start",
    reason: "hp_stop_unconfirmed",
    value_a: 1003,
  }, "Warmtepomp 1");

  assert.equal(link.title, "Verbinding met warmtepomp bevestigd weg");
  assert.equal(start.title, "Warmtepompstart niet bevestigd");
  assert.equal(stop.title, "Warmtepompstop niet bevestigd");
  assert.match(getControlReplayIncidentReasonMeta("hp_link_loss").summary, /eerst gefilterd/);
  const persistence = getControlReplayIncidentEventCopy({
    event_type: "incident_start",
    reason: "hp_persistence_failure",
    value_a: 1004,
  }, "Warmtepomp 1");
  assert.equal(persistence.title, "Opslag van handmatige resetstatus mislukt");
  assert.match(persistence.detail, /blokkeert start en fallback/);
});

test("availability transitions distinguish brief confirmation, outage and recovery", () => {
  const suspect = getControlReplayIncidentEventCopy({
    event_type: "hp_availability_change",
    reason: "hp_link_loss",
    from: "available",
    to: "suspect",
  }, "Warmtepomp 1");
  const offlineEvent = {
    event_type: "hp_availability_change",
    reason: "hp_link_loss",
    from: "suspect",
    to: "offline",
  };
  const availableEvent = {
    event_type: "hp_availability_change",
    reason: "keep_current",
    from: "recovering",
    to: "available",
  };

  assert.match(suspect.summary, /korte hapering/);
  assert.equal(getControlReplayIncidentDisplaySeverity(offlineEvent), "fault");
  assert.equal(getControlReplayIncidentDisplaySeverity(availableEvent), "normal");
  assert.equal(
    getControlReplayIncidentEventCopy(availableEvent, "Warmtepomp 1").title,
    "Warmtepomp 1 beschikbaar",
  );
  assert.match(
    getControlReplayIncidentReasonMeta("hp_recovered").summary,
    /ketelfallback.*beëindigd/,
  );
});

test("CM3 to CM4 remains a role transition and fallback start is explicit", () => {
  const modeEvent = {
    event_type: "control_mode_change",
    reason: "boiler_fallback",
    cm: 4,
    value_a: 3,
    value_b: 4,
  };
  const fallbackEvent = {
    event_type: "boiler_fallback_start",
    reason: "boiler_fallback",
    cm: 4,
    value_a: 350,
    value_b: 1,
  };
  const fallbackStopEvent = {
    event_type: "boiler_fallback_stop",
    reason: "hp_recovered",
    cm: 2,
    value_a: 345,
    value_b: 0,
  };

  assert.deepEqual(
    getControlReplayIncidentModeTransition(modeEvent, 3),
    { from: 3, to: 4 },
  );
  assert.equal(getControlReplayIncidentModeAfterEvent(modeEvent), 4);
  assert.equal(
    getControlReplayIncidentEventCopy(modeEvent, "Systeem").title,
    "CM3 → CM4",
  );
  assert.match(
    getControlReplayIncidentEventCopy(modeEvent, "Systeem").summary,
    /regelrol/,
  );
  assert.equal(
    getControlReplayIncidentEventCopy(fallbackEvent, "CV-ketel").title,
    "Ketelfallback gestart (CM4)",
  );
  assert.deepEqual(
    getControlReplayIncidentModeTransition(fallbackEvent, 3),
    { from: 3, to: 4 },
  );
  assert.deepEqual(
    getControlReplayIncidentModeTransition(fallbackStopEvent, 4),
    { from: 4, to: 2 },
  );
});
