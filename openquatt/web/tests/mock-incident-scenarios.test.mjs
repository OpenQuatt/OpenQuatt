import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import vm from "node:vm";
import { fileURLToPath } from "node:url";

const testDir = path.dirname(fileURLToPath(import.meta.url));
const source = await readFile(path.join(testDir, "../js/mock-incident-scenarios.js"), "utf8");
const mockDeviceSource = await readFile(path.join(testDir, "../js/mock-device.js"), "utf8");
const context = { window: {} };
vm.runInNewContext(source, context, { filename: "mock-incident-scenarios.js" });
const catalog = context.window.__OQ_MOCK_INCIDENT_SCENARIOS__;
const protectionStates = new Set(["clear", "limited", "start_blocked", "fault_active", "fault_recovery"]);

test("heat-pump mock catalog has stable, compatible scenario and phase contracts", () => {
  assert.ok(catalog);
  assert.ok(catalog.scenarios.length >= 10);
  assert.equal(new Set(catalog.scenarios.map((scenario) => scenario.id)).size, catalog.scenarios.length);

  catalog.scenarios.forEach((scenario) => {
    assert.match(scenario.id, /^[a-z0-9-]+$/);
    assert.ok(["any", "single", "duo"].includes(scenario.topology));
    assert.ok(scenario.phases.length >= 1);
    assert.equal(new Set(scenario.phases.map((phase) => phase.id)).size, scenario.phases.length);
    scenario.phases.forEach((phase, index) => {
      assert.ok(Number.isInteger(phase.elapsed_s));
      if (index > 0) {
        assert.ok(phase.elapsed_s >= scenario.phases[index - 1].elapsed_s);
      }
      assert.ok(Number.isInteger(phase.system.control_mode));
      assert.ok(Array.isArray(phase.heat_pumps));
      assert.ok(Array.isArray(phase.events));
      phase.heat_pumps.forEach((hp) => {
        assert.ok(protectionStates.has(hp.protection_state));
      });
    });
  });
});

test("incident phases do not overwrite persistent boiler settings", () => {
  const applyStart = mockDeviceSource.indexOf("function applyIncidentScenario()");
  const applyEnd = mockDeviceSource.indexOf("\n  function ", applyStart + 1);
  const applySource = mockDeviceSource.slice(applyStart, applyEnd);
  assert.ok(applyStart >= 0);
  assert.doesNotMatch(applySource, /setSwitch\("Boiler assist enabled"/);
  assert.doesNotMatch(applySource, /setSwitch\(\s*"Boiler fallback on heat-pump fault"/);
});

test("brief link dip never creates an incident, stop or CM4 transition", () => {
  const scenario = catalog.getScenario("brief-link-dip");
  assert.equal(scenario.phases.length, 4);
  scenario.phases.forEach((phase) => {
    assert.notEqual(phase.system.control_mode, 4);
    assert.equal(phase.system.boiler_command_active, false);
    assert.equal(phase.events.length, 0);
    assert.equal(phase.heat_pumps[0].incidents.length, 0);
    assert.equal(phase.heat_pumps[0].must_stop, false);
  });
  assert.equal(scenario.phases[1].heat_pumps[0].link_state, "suspect");
  assert.equal(scenario.phases[1].heat_pumps[0].availability, "unknown");
  assert.equal(scenario.phases[1].heat_pumps[0].available_for_start, false);
  assert.equal(scenario.phases.at(-1).heat_pumps[0].link_state, "healthy");
  assert.equal(scenario.phases.at(-1).heat_pumps[0].availability, "available");
});

test("confirmed link loss waits for stop confirmation before CM4", () => {
  const scenario = catalog.getScenario("confirmed-link-loss");
  const lost = scenario.phases.find((phase) => phase.id === "lost");
  const fallback = scenario.phases.find((phase) => phase.id === "fallback");
  assert.equal(lost.elapsed_s, 30);
  assert.equal(lost.system.action, "fallback_blocked");
  assert.equal(lost.system.fallback_block_reason, 10);
  assert.equal(lost.heat_pumps[0].stop_confirmation_pending, true);
  assert.equal(lost.heat_pumps[0].stop_unconfirmed, false);
  assert.equal(lost.heat_pumps[0].incidents.some((incident) => incident.definition.id === 1003), false);
  assert.equal(lost.events.some((event) => event.reason === "hp_stop_unconfirmed"), false);
  assert.equal(lost.heat_pumps[0].protection_state, "clear");
  assert.equal(lost.heat_pumps[0].fault_active, false);
  assert.equal(fallback.system.control_mode, 4);
  assert.equal(fallback.system.boiler_command_active, true);
  assert.equal(fallback.heat_pumps[0].protection_state, "clear");
  assert.equal(fallback.heat_pumps[0].fault_active, false);
  assert.ok(
    catalog.collectEvents(scenario.id, scenario.phases.indexOf(fallback))
      .some((event) => event.event_type === "hp_stop_confirmed"),
  );
});

test("CM3 to CM4 R1 and OpenTherm fixtures preserve the active boiler command", () => {
  for (const [id, transport] of [
    ["cm3-cm4-r1", "R1"],
    ["cm3-cm4-opentherm", "OpenTherm"],
  ]) {
    const scenario = catalog.getScenario(id);
    const [assist, fallback] = scenario.phases;
    assert.equal(scenario.boiler_transport, transport);
    assert.equal(assist.system.control_mode, 3);
    assert.equal(assist.system.boiler_command_active, true);
    assert.equal(fallback.system.control_mode, 4);
    assert.equal(fallback.system.boiler_command_active, true);
    assert.equal(fallback.system.boiler_output_continuous, true);
    assert.deepEqual(
      Array.from(fallback.events
        .filter((event) => event.event_type === "control_mode_change")
        .map((event) => [event.value_a, event.value_b]), (values) => Array.from(values)),
      [[3, 4]],
    );
  }
});

test("Duo fixtures keep one healthy HP before allowing fallback", () => {
  const oneFault = catalog.buildPhaseState("duo-one-fault", 0, "duo").phase;
  assert.equal(oneFault.system.control_mode, 2);
  assert.equal(oneFault.system.boiler_command_active, false);
  assert.equal(oneFault.heat_pumps[0].availability, "unavailable");
  assert.equal(oneFault.heat_pumps[1].availability, "available");
  assert.equal(oneFault.heat_pumps[1].run_state, "running");

  const bothFault = catalog.buildPhaseState("duo-both-fault", 1, "duo").phase;
  assert.equal(bothFault.system.control_mode, 4);
  assert.equal(bothFault.system.boiler_command_active, true);
  assert.ok(bothFault.heat_pumps.every((hp) => hp.availability === "unavailable"));
});

test("action and API failure scenarios expose deterministic runner controls", () => {
  const retry = catalog.getScenario("start-failed-retry").phases[0].actions.start_failure_retry;
  assert.equal(retry.target_phase, "retried");
  assert.equal(retry.complete_after_reads, 2);
  assert.equal(retry.result, "start_failure_cleared");

  const powerCycle = catalog.getScenario("power-cycle-confirmation").phases[1]
    .actions.confirm_odu_power_cycle;
  assert.equal(powerCycle.reject_csrf_once, true);
  assert.equal(powerCycle.target_phase, "confirmed");
  const powerCycleHp = catalog.getScenario("power-cycle-confirmation").phases[1].heat_pumps[0];
  assert.equal(powerCycleHp.protection_state, "fault_active");
  assert.equal(powerCycleHp.fault_active, true);
  assert.equal(powerCycleHp.incidents[0].runtime.lifecycle, "latched");

  assert.deepEqual(
    Array.from(
      catalog.getScenario("incident-api-transient").phases,
      (phase) => phase.incident_http_status,
    ),
    [200, 503, 503, 503, 200],
  );
});

test("fault recovery matches the firmware-derived output without publishing a cleared raw incident", () => {
  const recovering = catalog.getScenario("hard-fault-recovery").phases[1].heat_pumps[0];
  assert.equal(recovering.link_state, "healthy");
  assert.equal(recovering.protection_state, "fault_recovery");
  assert.equal(recovering.availability, "recovering");
  assert.equal(recovering.fallback_cause_present, true);
  assert.equal(recovering.fallback_eligible, true);
  assert.equal(recovering.primary_incident_id, 0);
  assert.equal(recovering.incidents.length, 0);
});

test("stop confirmation block retains its confirmed fallback cause", () => {
  const blocked = catalog.getScenario("stop-unconfirmed-block").phases[0].heat_pumps[0];
  assert.equal(blocked.protection_state, "fault_active");
  assert.equal(blocked.fault_active, true);
  assert.equal(blocked.fallback_cause_present, true);
  assert.equal(blocked.fallback_eligible, false);
  assert.equal(blocked.stop_unconfirmed, true);
  assert.equal(blocked.must_stop, true);
  assert.deepEqual(
    Array.from(blocked.incidents, (incident) => incident.definition.id),
    [22, 1003],
  );
});

test("phase builder never leaks HP2 into a Single snapshot", () => {
  const single = catalog.buildPhaseState("none", 0, "single").phase;
  const duo = catalog.buildPhaseState("none", 0, "duo").phase;
  assert.deepEqual(Array.from(single.heat_pumps, (hp) => hp.index), [1]);
  assert.deepEqual(Array.from(duo.heat_pumps, (hp) => hp.index), [1, 2]);
});
