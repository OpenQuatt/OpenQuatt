import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { setLocale, t } from "../js/src/i18n/index.js";
import { loadReplayHarness } from "./helpers/replay-render-harness.mjs";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const source = await readFile(new URL("../js/src/features/control-replay-view.js", import.meta.url), "utf8");
const replay = await loadReplayHarness(source + "\nexport { getControlWorkingOptimizerModel };\n");
const event = (event_type, reason = "less_power", subject = "HP2", cm = 2) => ({ event_type, reason, subject, cm });
const historical = (rawDecisionEvent) => ({ reasonCode: rawDecisionEvent.reason, source: rawDecisionEvent.subject, rawDecisionEvent });
const entity = (value) => ({ value, state: String(value) });

test.beforeEach(() => {
  setLocale("nl");
  state.entities = {
    installationTopology: entity("duo"), runtimeLeadHp: entity("HP1"),
    coolingDemandRaw: entity(8), coolingLimitedDemand: entity(5), coolingLimiterAllowedMax: entity(5),
    coolingPermitted: entity(true),
  };
});

test("a stop command never presents the stopped HP as the source that stays active", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    for (const subject of ["HP1", "HP2"]) {
      for (const reason of ["less_power", "demand_decreased"]) {
        const model = replay.getControlWorkingOptimizerModel(historical(event("source_stop", reason, subject)));
        assert.equal(model.rows.length, 1);
        assert.equal(model.rows[0].option, subject);
        assert.equal(model.rows[0].result, t("controlReplay.optStopCommand"));
        assert.notEqual(model.rows[0].result, t("controlReplay.optStaysActive"));
        assert.match(model.rows[0].detail, locale === "nl" ? /nog niet bevestigd/ : /not yet confirmed/);
        if (reason === "less_power") assert.doesNotMatch(model.summary, locale === "nl" ? /vraag.*gezakt|vraag.*afgenomen/ : /demand.*dropped/i);
      }
    }
  }
});

test("less_power does not invent decreasing demand for live or topology choices either", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    for (const target of [
      { primaryReason: "less_power", source: "HP1" },
      historical(event("topology_change", "less_power", "HP1")),
    ]) {
      const model = replay.getControlWorkingOptimizerModel(target);
      assert.equal(model.summary, t("controlReplay.evDemandLowerSummary"));
      assert.doesNotMatch(JSON.stringify(model), locale === "nl" ? /lagere vraag|vraag.*gezakt/ : /lower demand|demand.*dropped/i);
    }
    for (const cm of [2, 5]) {
      const copy = replay.getDecisionEventCopy(event("source_stop", "less_power", "HP2", cm));
      assert.doesNotMatch(JSON.stringify(copy), locale === "nl" ? /geen koelvraag|vraag is.*afgenomen|vraag.*weggevallen/ : /no.*cooling demand|demand has ended|demand.*decreased/i);
    }
  }
});

test("historical cooling uses the recorded demand and output, never today's safe maximum", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const target = historical({ ...event("cooling_limited", "dew_stop", "COOLING", 5), value_a: 0, value_b: 7 });
    const model = replay.getControlWorkingOptimizerModel(target);
    assert.deepEqual(model.rows.map(({ result }) => result), ["7", t("controlReplay.optNotRecorded"), "0"]);
    assert.equal(model.verdict, t("controlReplay.optCoolPaused"));
    state.entities.coolingDemandRaw = entity(2);
    state.entities.coolingLimitedDemand = entity(1);
    state.entities.coolingLimiterAllowedMax = entity(1);
    state.entities.coolingPermitted = entity(false);
    assert.deepEqual(replay.getControlWorkingOptimizerModel(target), model);
  }
});

test("missing, malformed and non-level historical values remain explicitly unrecorded", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    for (const value of [undefined, null, "0", -1, 11, 1.5, NaN]) {
      const target = historical({ ...event("cooling_limited", "dew_stop", "COOLING", 5), value_a: value, value_b: value });
      assert.ok(replay.getControlWorkingOptimizerModel(target).rows.every(({ result }) => result === t("controlReplay.optNotRecorded")));
    }
    for (const reasonCode of ["capacity_cap", "room_cap", "cooling_limiter", "falling_gap", "projected_floor", "dew_stop", "restart_wait", "level1_hold", "oil_return_recovery", "sensor_fallback"]) {
      for (const target of [
        historical({ ...event("source_stop", reasonCode, "HP2", 5), value_a: 2, value_b: 3 }),
        { reasonCode, source: "HP2", derivedFromDecisionLog: true },
      ]) {
        assert.ok(replay.getControlWorkingOptimizerModel(target).rows.every(({ result }) => result === t("controlReplay.optNotRecorded")));
      }
    }
  }
});

test("current cooling monitoring still reads live demand, cap and output", () => {
  for (const reasonCode of ["capacity_cap", "falling_gap"]) {
    const model = replay.getControlWorkingOptimizerModel({ primaryReason: reasonCode, source: "HP1" });
    assert.deepEqual(model.rows.map(({ result }) => result), ["8", "5", "5"]);
  }
});

test("heating and cooling distinguish stop commands from compressor stop confirmation in both locales", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    for (const cm of [2, 5]) {
      for (const reason of ["keep_current", "less_power", "heating_request_cleared", "dew_stop", "ph_comfort_stop"]) {
        const command = replay.getDecisionEventCopy({ ...event("source_stop", reason, "HP2", cm), flags: 32 });
        const confirmed = replay.getDecisionEventCopy(event("hp_stop_confirmed", reason, "HP2", cm));
        assert.match(command.title, locale === "nl" ? /stopopdracht/i : /stop command/i);
        assert.match(confirmed.title, locale === "nl" ? /bevestigd/ : /confirmed/);
        assert.notEqual(command.title, confirmed.title);
        assert.doesNotMatch(command.summary, locale === "nl" ? /is gestopt|stopte|is klaar/ : /has stopped|is done/);
      }
    }
  }
});

test("cooling start commands wait for compressor feedback, just like heating commands", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const command = replay.getDecisionEventCopy(event("source_start", "runtime_lead", "HP2", 5));
    const confirmed = replay.getDecisionEventCopy(event("hp_start_confirmed", "runtime_lead", "HP2", 5));
    assert.match(command.title, locale === "nl" ? /startopdracht/i : /start command/i);
    assert.match(command.summary, locale === "nl" ? /nog niet bevestigd/ : /not yet confirmed/);
    assert.equal(command.next, t("controlReplay.evHpAwaitConfirmation"));
    assert.notEqual(command.title, confirmed.title);
  }
});
