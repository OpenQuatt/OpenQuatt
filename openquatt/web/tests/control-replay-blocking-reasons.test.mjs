import assert from "node:assert/strict";
import test from "node:test";
import { readFile } from "node:fs/promises";
import { loadReplayHarness } from "./helpers/replay-render-harness.mjs";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { setLocale } = await import("../js/src/i18n/index.js");
const { getOverviewLikeHydrationKeys } = await import("../js/src/core/entity-sync.js");
const source = await readFile(new URL("../js/src/features/control-replay-view.js", import.meta.url), "utf8");
const { getControlWorkingBlockingReasons: reasons, getControlWorkingCurrent: current,
  getControlWorkingSignature: signature, renderControlWorkingNowCard: card } =
  await loadReplayHarness(source + "\nexport { getControlWorkingBlockingReasons, getControlWorkingCurrent, getControlWorkingSignature, renderControlWorkingNowCard };");
const value = (value) => ({ value, state: String(value) });
function setup(values = {}) {
  setLocale("nl");
  state.drafts = {};
  state.inputDrafts = {};
  state.entities = Object.fromEntries(Object.entries(values).map(([k, v]) => [k, value(v)]));
  state.decisionLog = null;
}
const off = { hp1Running: false, hp2Running: false, primaryReason: "keep_current" };

test("missing or active request never claims no heat demand", () => {
  for (const values of [{}, { strategyRequestActive: "nan" }, { strategyRequestActive: true }]) {
    setup(values);
    assert.match(reasons(off)[0], /bevestigt geen specifieke/);
  }
  setup({ strategyRequestActive: false });
  assert.match(reasons(off)[0], /vraagt momenteel geen warmte/);
  setup({ strategyRequestActive: false, coolingRequestActive: true });
  assert.doesNotMatch(reasons(off).join(" "), /geen warmte\b/);
});

test("actual CM4 and cooling buffer models preserve their live explanations", () => {
  setup({ controlModeLabel: "CM4", strategyRequestActive: true });
  const model = current([]);
  assert.equal(model.primaryReason, "fallback_blocked");
  assert.deepEqual(reasons(model), [model.copy]);
  for (const primaryReason of ["boiler_fallback", "boiler_assist", "defrost_hold", "buffer_stop"]) {
    assert.deepEqual(reasons({ ...off, primaryReason, copy: "Actuele reden" }), ["Actuele reden"]);
  }
});

test("low-load latch ON is permission; OFF blocks below restart threshold including hysteresis", () => {
  const values = { strategyActiveCode: 3, strategyRequestActive: true,
    strategyRequestedPower: 1900, lowLoadOnW: 2200, lowLoadOffW: 1600 };
  setup({ ...values, lowLoadLatch: true });
  assert.doesNotMatch(reasons(off).join(" "), /Laaglast/);
  setup({ ...values, lowLoadLatch: false });
  assert.match(reasons(off)[0], /1900 W.*2200 W/);
  assert.match(reasons(off)[1], /1600 W/);
  state.entities.strategyRequestedPower = value(2200);
  assert.doesNotMatch(reasons(off).join(" "), /Laaglast/);
  setup({ ...values, lowLoadLatch: false, strategyActiveCode: 2 });
  assert.doesNotMatch(reasons(off).join(" "), /Laaglast/);
});

test("multiple live blockers are collected; unknown data and drafts do not invent limits", () => {
  setup({ openquattEnabled: false, heatingBlockedByThermostat: true, strategyWaterTripActive: true });
  assert.equal(reasons(off).length, 3);
  state.drafts.openquattEnabled = true;
  assert.match(reasons(off)[0], /uitgeschakeld/);
  setup({ strategyActiveCode: 3, strategyRequestActive: true, lowLoadLatch: false, strategyRequestedPower: 900 });
  assert.doesNotMatch(reasons(off).join(" "), /0 W|NaN|Laaglast/);
});

test("render signature follows blocker changes without unrelated telemetry changing", () => {
  setup({ strategyRequestActive: true });
  const before = signature(off);
  state.entities.heatingBlockedByThermostat = value(true);
  assert.notEqual(signature(off), before);
  const blocked = signature(off);
  state.entities.heatingBlockedByThermostat = value(false);
  assert.notEqual(signature(off), blocked);
});

test("running heat pumps hide the section and cooling protection is not duplicated", () => {
  setup();
  for (const key of ["hp1Running", "hp2Running"]) {
    const model = { ...off, [key]: true };
    assert.deepEqual(reasons(model), []);
    assert.doesNotMatch(card(model), /Waarom staat/);
  }
  assert.deepEqual(reasons({ ...off, primaryReason: "restart_wait", coolingProtection: true,
    copy: "Veilige herstart" }), ["Veilige herstart"]);
});

test("English is supported and device-derived explanations are escaped", () => {
  setup({ openquattEnabled: false });
  setLocale("en");
  assert.equal(reasons(off)[0], "OpenQuatt is disabled.");
  assert.match(card(off), /Why is my heat pump off/);
  setup();
  const html = card({ ...off, primaryReason: "boiler_fallback", copy: "<script>alert(1)</script>" });
  assert.ok(html.includes("&lt;script&gt;"));
  assert.ok(!html.includes("<script>"));
});

test("control view hydrates every additional live blocking input", () => {
  setup();
  const keys = getOverviewLikeHydrationKeys("control", { forceFast: true });
  for (const key of ["strategyRequestActive", "strategyWaterTripActive", "strategyWaterHardTripActive",
    "heatingBlockedByThermostat", "controlModeOverride", "lowLoadLatch", "lowLoadOnW",
    "lowLoadOffW", "curveRestartInhibit", "curveRestartBlockedByRoom",
    "coolingStartBlockReason", "coolingStartBlockRemaining"]) assert.ok(keys.includes(key), key);
});

test("cooling start protection uses published countdown and clears after release", () => {
  setup({ coolingRequestActive: true, coolingStartBlockReason: "Compressor restart protection",
    coolingStartBlockRemaining: 190 });
  assert.match(reasons(off).join(" "), /3:10/);
  const before = signature(off);
  state.entities.coolingStartBlockRemaining = value(180);
  assert.notEqual(signature(off), before);
  state.entities.coolingStartBlockReason = value("Ready");
  assert.doesNotMatch(reasons(off).join(" "), /herstartbeveiliging/);
});

test("single and duo current explanations follow available panels in both locales", () => {
  const hp1 = { title: "HP1", keys: { mode: "hp1Mode", freq: "hp1Freq", defrost: "hp1Defrost" } };
  const hp2 = { title: "HP2", keys: { mode: "hp2Mode", freq: "hp2Freq", defrost: "hp2Defrost" } };
  setup({ hp1Mode: "Heating", hp2Mode: "Standby" });
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const single = current([hp1]);
    assert.equal(single.hp1Running, true);
    assert.equal(single.hp2Available, false);
    assert.doesNotMatch(single.copy + single.expectation + card(single), /HP2|andere warmtepomp|extra warmtepomp|other heat pump|additional heat pump/);
    const duo = current([hp1, hp2]);
    assert.equal(duo.hp2Available, true);
    assert.match(duo.copy, /andere warmtepomp|other heat pump/);
    assert.match(card(duo), /HP2/);
  }
});

test("active run extension explains the configured choice and live stop temperature in NL and EN", () => {
  const hp1 = { title: "HP1", keys: { mode: "hp1Mode", freq: "hp1Freq", defrost: "hp1Defrost" } };
  setup({ hp1Mode: "Heating", strategyActiveCode: 3, phRunExtension: true,
    phRunExtensionStatus: "extending", phRunExtensionComfortStop: 21.7 });
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    const model = current([hp1]);
    assert.equal(model.primaryReason, "run_extension");
    assert.match(model.copy, /Langer doorverwarmen|Extended heating/);
    assert.match(model.expectation, locale === "nl" ? /21,7 °C/ : /21\.7 °C/);
    assert.match(card(model), /minimumvermogen|minimum output/);
  }
  state.drafts.phRunExtension = false;
  state.inputDrafts.phRunExtensionComfortStop = "30";
  assert.equal(current([hp1]).primaryReason, "run_extension");
  assert.match(current([hp1]).expectation, /21\.7 °C/);
  const before = signature(current([hp1]));
  state.entities.phRunExtensionComfortStop = value(22.1);
  assert.notEqual(signature(current([hp1])), before);
  delete state.entities.phRunExtensionComfortStop;
  const unknown = current([hp1]);
  assert.equal(unknown.primaryReason, "run_extension");
  assert.doesNotMatch(unknown.expectation, /NaN|0 °C|undefined/);
  assert.match(unknown.expectation, /configured stop temperature/);
  const hydration = getOverviewLikeHydrationKeys("control", { forceFast: true });
  for (const key of ["phRunExtension", "phRunExtensionStatus", "phRunExtensionComfortStop"]) {
    assert.ok(hydration.includes(key), key);
  }
});

test("run extension requires confirmed active heating and preserves protection and cooling explanations", () => {
  const hp1 = { title: "HP1", keys: { mode: "hp1Mode", freq: "hp1Freq", defrost: "hp1Defrost" } };
  const active = { hp1Mode: "Heating", strategyActiveCode: 3, phRunExtension: true, phRunExtensionStatus: "extending" };
  for (const patch of [
    { phRunExtension: false }, { phRunExtension: "nan" }, { phRunExtension: undefined },
    { phRunExtensionStatus: undefined }, { phRunExtensionStatus: "normal" },
    { phRunExtensionStatus: "comfort_stop" }, { phRunExtensionStatus: "blocked" },
    { phRunExtensionStatus: "wait_warm_restart" }, { strategyActiveCode: 2 },
    { hp1Mode: "Standby" }, { hp1Defrost: true }, { controlModeLabel: "CM98" },
    { controlModeLabel: "CM4" }, { coolingRequestActive: true }, { stickyActive: true },
  ]) {
    setup({ ...active, ...patch });
    assert.notEqual(current([hp1]).primaryReason, "run_extension", JSON.stringify(patch));
  }
});
