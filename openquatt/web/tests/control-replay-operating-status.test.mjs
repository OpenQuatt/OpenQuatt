import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { setLocale } from "../js/src/i18n/index.js";
import { getControlReplayIncidentEventCopy, getControlReplayIncidentDisplaySeverity } from "../js/src/features/control-replay-incidents.js";
import { getControlReplayOperatingStatus } from "../js/src/features/control-replay-status.js";
import { getConfirmedCompressorHistory } from "../js/src/features/control-replay-compressor-history.js";
import { loadReplayHarness } from "./helpers/replay-render-harness.mjs";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const source = await readFile(new URL("../js/src/features/control-replay-view.js", import.meta.url), "utf8");
const replay = await loadReplayHarness(source + "\nexport { mapDecisionEventToControlWorkingItem, renderControlWorkingDetails };\n");
const now = new Date(2026, 9, 23, 12).getTime();
const event = (id, type = "incident_start", extra = {}) => ({
  seq: 1, epoch_s: now / 1000 - 60, uptime_s: 1000,
  event_type: type, subject: "HP2", reason: "hp_fault", severity: "fault", cm: 2,
  value_a: id, duration_s: type === "incident_clear" ? 170 : 0, flags: 34, ...extra,
});
test.beforeEach(() => { state.entities = {}; state.controlReplayWindow = "today"; setLocale("nl"); });
test.afterEach(() => setLocale("nl"));

test("every explicitly non-fault catalogue entry has operating copy; unknown bits and synthetic faults do not", async () => {
  const catalog = await readFile(new URL("../../includes/incidents/oq_hp_incident_catalog.h", import.meta.url), "utf8");
  const entries = [...catalog.matchAll(/make_(status|limit|start_block|diagnostic)\((\d+)U, (\d+)U, "/g)]
    .map(([, , register, bit]) => (Number(register) - 2119) * 16 + Number(bit) + 1);
  assert.deepEqual(entries.sort((a, b) => a - b), [4, 6, 7, 12, 14, 46]);
  for (let id = 1; id <= 48; id++) {
    assert.equal(Boolean(getControlReplayOperatingStatus(event(id))), entries.includes(id), `catalogue ID ${id}`);
  }
  for (const id of [undefined, null, "", -1, 0, 1001, 1002, 1003, 1004, 1005, Number.NaN]) {
    assert.equal(getControlReplayOperatingStatus(event(id)), "");
  }
  for (const reason of ["hp_link_loss", "hp_start_failed", "hp_stop_unconfirmed", "hp_persistence_failure", "unknown"]) {
    assert.equal(getControlReplayOperatingStatus(event(4, "incident_start", { reason })), "");
  }
  assert.equal(getControlReplayOperatingStatus(event(4, "source_start")), "");
});

test("preheating completes without fault recovery copy and keeps its active start block visible, NL/EN", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    for (const reason of ["hp_preheat", "hp_fault", "hp_protection"]) {
      const start = replay.mapDecisionEventToControlWorkingItem(event(7, "incident_start", { reason }), "today", now);
      const clear = replay.mapDecisionEventToControlWorkingItem(event(7, "incident_clear", { reason, flags: 35 }), "today", now);
      assert.equal(start.severity, "limited");
      assert.equal(clear.severity, "normal");
      assert.equal(clear.title, locale === "nl" ? "Voorverwarming afgerond" : "Preheating completed");
      assert.match(start.summary, locale === "nl" ? /mag nog niet starten/ : /cannot start yet/);
      assert.match(clear.summary, locale === "nl" ? /niet meer.*Andere startvoorwaarden.*historie/ : /no longer.*Other start conditions.*history/);
      const html = replay.renderControlWorkingDetails(clear);
      assert.doesNotMatch(html, /Warmtepompstoring|Incident hersteld|oorzaak.*hersteld|Heat pump fault|Incident recovered|cause.*recovered/);
      assert.notEqual(clear.detail, clear.reasonSummary);
      assert.match(clear.next, locale === "nl" ? /geen compressorstart/ : /does not confirm a compressor start/);
    }
  }
});

test("pressure and outdoor temperature protections describe capacity limits, never a fault or all-clear, NL/EN", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    for (const id of [6, 12, 14]) {
      for (const reason of ["hp_protection", "hp_fault"]) {
        const start = replay.mapDecisionEventToControlWorkingItem(event(id, "incident_start", { reason }), "today", now);
        const clear = replay.mapDecisionEventToControlWorkingItem(event(id, "incident_clear", { reason, flags: 35 }), "today", now);
        assert.equal(start.severity, "limited");
        assert.equal(clear.severity, "normal");
        assert.match(start.summary, locale === "nl" ? /begrenst tijdelijk het compressorvermogen/ : /temporarily limits compressor output/);
        assert.match(clear.title, locale === "nl" ? /opgeheven$/ : /ended$/);
        assert.match(clear.summary, locale === "nl" ? /niet meer gemeld.*historie/ : /no longer reported.*history/);
        assert.match(start.reasonSummary, locale === "nl" ? /andere beveiligingen blijven gelden/ : /other protections still apply/);
        assert.doesNotMatch(replay.renderControlWorkingDetails(clear), /Warmtepompstoring|Incident hersteld|oorzaak.*hersteld|Heat pump fault|Incident recovered|cause.*recovered/);
      }
    }
  }
});

test("legacy pump diagnostics and oil return are informational across raw reasons without proving pump health", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    for (const id of [4, 46]) {
      for (const type of ["incident_start", "incident_clear", "incident_acknowledged"]) {
        for (const reason of ["hp_fault", "hp_protection"]) {
          const e = event(id, type, { reason, to: "active" });
          const copy = getControlReplayIncidentEventCopy(e, "HP2");
          assert.equal(getControlReplayIncidentDisplaySeverity(e), "normal");
          assert.doesNotMatch(copy.title + copy.summary + copy.reasonLabel, /storing|hersteld|fault|recovered/i);
          if (id === 46) {
            assert.match(copy.detail, locale === "nl" ? /sommige pomptypen.*stand-by.*bewijst geen pompdefect/ : /some pump types.*standby.*does not prove a pump defect/);
            assert.match(copy.reasonSummary, locale === "nl" ? /niet dat de pomp goed werkt/ : /does not prove that the pump works correctly/);
          }
        }
      }
    }
  }
});

test("availability after preheat, bootstrap or recovery does not invent a prior fault or a running compressor", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    for (const from of ["preheat", "suspect", "recovering", undefined]) {
      const copy = getControlReplayIncidentEventCopy(event(7, "hp_availability_change", { from, to: "available", reason: "hp_preheat" }), "HP2");
      assert.doesNotMatch(copy.title + copy.summary + copy.detail + copy.reasonSummary, /hersteld|recovered|wacht op|waits for/);
      assert.match(copy.detail, locale === "nl" ? /waterflow.*minimale uit-tijd/ : /water flow.*minimum off-time/);
      assert.match(copy.reasonSummary, locale === "nl" ? /niet dat de compressor al draait/ : /does not confirm that the compressor is already running/);
    }
    const preheat = getControlReplayIncidentEventCopy(event(7, "hp_availability_change", { to: "preheat", reason: "hp_fault" }), "HP2");
    assert.match(preheat.next, locale === "nl" ? /wacht.*voorverwarming/ : /waits.*preheating/);
    assert.doesNotMatch(preheat.reasonLabel, /storing|fault/i);
  }
});

test("defrost explains frost removal and ending a phase does not promise resumed heat output", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    for (const type of ["defrost_seen_start", "defrost_seen_clear"]) {
      const item = replay.mapDecisionEventToControlWorkingItem(event(0, type, { reason: "defrost_hold", severity: "normal" }), "today", now);
      assert.equal(item.severity, "normal");
      assert.match(item.detail, locale === "nl" ? /rijp.*buitenwarmtewisselaar/ : /frost.*outdoor heat exchanger/);
      assert.match(item.reasonSummary, locale === "nl" ? /normaal warmtepompbedrijf/ : /normal heat pump operation/);
      assert.match(item.next, locale === "nl" ? /vraag.*zodra/ : /demand.*once/);
      if (type === "defrost_seen_clear") {
        assert.doesNotMatch(item.summary, /vermogen leveren|deliver normal power/);
        assert.doesNotMatch(replay.renderControlWorkingDetails(item), /Ontdooien krijgt voorrang|Defrost takes precedence|Rustig laten herstellen|Allow recovery/);
      }
    }
    const hold = replay.getDecisionEventCopy(event(0, "decision_hold", { reason: "oil_return_hold" }));
    assert.match(hold.detail, locale === "nl" ? /olie-retour/ : /oil return/);
    assert.doesNotMatch(hold.detail, /Compressorherstel|compressor recovery/);
  }
});

test("capacity limits and diagnostics preserve both confirmed runs; real and unknown faults still end certainty", () => {
  const records = [event(0, "hp_start_confirmed", { seq: 1, epoch_s: 10, subject: "BOTH" })];
  for (const [index, id] of [4, 6, 12, 14, 46].entries()) {
    records.push(Object.freeze(event(id, "incident_start", { seq: index + 2, epoch_s: 20 + index })));
  }
  for (const id of [22, 15, 1005]) {
    const scenario = Object.freeze([...records,
      event(id, "incident_start", { seq: 7, epoch_s: 30, subject: "HP1" }),
      event(0, "hp_stop_confirmed", { seq: 8, epoch_s: 50 }),
    ]);
    const result = getConfirmedCompressorHistory(scenario, { start: 0, end: 100, timeOf: e => e.epoch_s });
    assert.deepEqual(result.intervals.map(({ source, start, end, endUncertain }) => [source, start, end, endUncertain]),
      [["HP1", 10, 30, true], ["HP2", 10, 50, false]]);
    assert.equal(getControlReplayIncidentEventCopy(scenario[6]).reasonLabel, "Warmtepompstoring");
  }
});

test("a cleared fault requiring power cycle remains limited and explains that acknowledgement cannot release it", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const e = event(21, "incident_clear", { flags: 97, to: "blocked" });
    const copy = getControlReplayIncidentEventCopy(e, "HP2");
    assert.equal(getControlReplayIncidentDisplaySeverity(e), "limited");
    assert.doesNotMatch(copy.title + copy.reasonLabel, /hersteld|recovered/);
    assert.match(copy.summary, locale === "nl" ? /startblokkade blijft.*spanningsonderbreking/ : /start block remains.*power cycle/);
    assert.match(copy.next, locale === "nl" ? /resetprocedure/ : /reset procedure/);
    assert.equal(getControlReplayIncidentDisplaySeverity({ ...e, flags: 34, to: "idle" }), "normal");
  }
});

test("acknowledging an active fault or protection never masquerades as recovery", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const activeFault = event(22, "incident_acknowledged", { to: "active" });
    const copy = getControlReplayIncidentEventCopy(activeFault, "HP2");
    assert.equal(getControlReplayIncidentDisplaySeverity(activeFault), "fault");
    assert.match(copy.summary, locale === "nl" ? /storing is nog actief/ : /fault is still active/);
    assert.doesNotMatch(copy.summary + copy.reasonLabel, /hersteld|recovered/);
    const activeLimit = event(6, "incident_acknowledged", { reason: "hp_protection", to: "active" });
    assert.equal(getControlReplayIncidentDisplaySeverity(activeLimit), "limited");
    assert.match(getControlReplayIncidentEventCopy(activeLimit).summary, locale === "nl" ? /verandert.*niet/ : /does not change/);
  }
});
