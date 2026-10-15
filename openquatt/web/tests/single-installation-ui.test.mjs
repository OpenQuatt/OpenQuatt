import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { loadReplayHarness } from "./helpers/replay-render-harness.mjs";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };
globalThis.document = { activeElement: null };
const { state } = await import("../js/src/core/state.js");
const { setLocale, t } = await import("../js/src/i18n/index.js");
const { getInstallationTopology } = await import("../js/src/features/device-context.js");
const { getInstallationMonitoringModel } = await import("../js/src/core/installation-monitoring.js");
const { getSettingsServiceModel, renderHpWaterCalibrationWizard, renderSettingsCounterServiceSection } = await import("../js/src/settings/service.js");
const { renderHpWaterSensorOffsetSettings } = await import("../js/src/settings/water.js");
const { renderSettingsHeatPumpLimiterCard } = await import("../js/src/settings/heating.js");
const { renderSettingsSensorSelectionSection } = await import("../js/src/settings/integrations.js");
const { getOduSettingsHpIndexes } = await import("../js/src/features/odu-settings.js");
const { getOduDefrostHpIndexes } = await import("../js/src/features/odu-defrost.js");
const { getOduRuntimeFrequencyHpIndexes } = await import("../js/src/features/odu-runtime-frequency.js");
const { getOduEepromDumpHpIndexes } = await import("../js/src/features/odu-eeprom-dump.js");
const { getHeatPumpPanels } = await import("../js/src/views/heatpump.js");

// Keep the real renderers and shared state; these surfaces do not use embedded images.
const installationUrl = new URL("../js/src/settings/installation.js", import.meta.url);
let installationSource = await readFile(installationUrl, "utf8");
installationSource = installationSource.replace(
  /^import .* from "\.\.\/core\/embedded-assets.js";$/m,
  'const HP_GENERATION_IMAGE_V1 = "", HP_GENERATION_IMAGE_V2 = "";',
).replace(/(from\s+["'])(\.[^"']+)(["'])/g, (_, before, path, after) => `${before}${new URL(path, installationUrl).href}${after}`);
const { renderSettingsInstallationMonitoringSection } = await import(`data:text/javascript;base64,${Buffer.from(installationSource).toString("base64")}`);
const replaySource = await readFile(new URL("../js/src/features/control-replay-view.js", import.meta.url), "utf8");
const replay = await loadReplayHarness(`${replaySource}\nexport { getControlWorkingCurrent, getControlWorkingOptimizerModel, renderControlWorkingDetails };`);
const initialState = structuredClone(state);
const entity = (value, extra = {}) => ({ value, state: String(value), ...extra });
const select = (value, options) => entity(value, { options });

function setup(topology = "single") {
  state.loadingEntities = false;
  state.settingsSourceFocusKey = "flow-source";
  state.settingsSourceDetailOpen = true;
  state.entities = {
    installationTopology: entity(topology),
    hpGeneration: entity("V1.5"),
    hp1Power: entity(0), hp2Power: entity(0),
    hp1Compressor: entity(0), hp2Compressor: entity("nan"),
    hp1Failures: entity("None"), hp2Failures: entity("Communication lost"),
    hp1Freq: entity(0), hp2Freq: entity("nan"),
    hp1WaterIn: entity(25), hp1WaterOut: entity(25),
    hp2WaterIn: entity(25), hp2WaterOut: entity(25),
    hp1WaterInOffset: entity(0), hp1WaterOutOffset: entity(0),
    hp2WaterInOffset: entity(0), hp2WaterOutOffset: entity(0),
    hp2WaterInOffsetSuggested: entity(0), hp2WaterOutOffsetSuggested: entity(0),
    hp1ExcludeMinHz: entity(0), hp1ExcludeMaxHz: entity(0),
    hp2ExcludeMinHz: entity(0), hp2ExcludeMaxHz: entity(0),
    hp1RuntimeHours: entity(10), hp2RuntimeHours: entity(20), runtimeLeadHp: entity("HP2"),
    hp1CompressorStarts2h: entity(1), hp1CompressorStarts72h: entity(40),
    hp2CompressorStarts2h: entity(0), hp2CompressorStarts72h: entity(0), hp2CompressorStarts24h: entity(0),
    compressorCyclingAlertLatched: entity(true),
    compressorCyclingAlertHp1Peak2h: entity(2), compressorCyclingAlertHp1Peak72h: entity(41),
    compressorCyclingAlertHp2Peak2h: entity(0), compressorCyclingAlertHp2Peak72h: entity(0),
    compressorCyclingAlertAlternating: entity(true), alternatingCompressorStartsWarning: entity(true),
    flowMismatch: entity(true),
    cm100Active: entity(true), manualHpStatus: entity("IDLE"), manualHpStart: entity(false),
    manualHp1Mode: select("Standby", ["Standby", "Heating", "Cooling"]),
    manualHp2Mode: select("Standby", ["Standby", "Heating", "Cooling"]),
    manualHp1Level: entity(0), manualHp2Level: entity(0),
    hpWaterCalibrationStatus: entity("IDLE"), hpWaterCalibrationStart: entity(false),
    flowSource: select("Outdoor unit", ["Outdoor unit", "CIC"]),
    qFlowSource: select("Outdoor unit", ["Auto", "Local", "Outdoor unit"]),
    outdoorUnitFlowMode: select("Local aggregate HP1/HP2", ["Flowmeter HP1", "Flowmeter HP2", "Local aggregate HP1/HP2"]),
    flowLocal: entity(500), hp1Flow: entity(500), hp2Flow: entity(500), flowSelected: entity(500),
  };
}

test.beforeEach(() => {
  Object.assign(state, structuredClone(initialState));
  setLocale("nl");
  setup();
});

test("Single hides HP2 starts, incident peaks and Duo diagnostics despite available HP2 fields", () => {
  const markup = renderSettingsInstallationMonitoringSection();
  assert.match(markup, /Warmtepomp 1/);
  assert.match(markup, /41 starts/);
  assert.doesNotMatch(markup, /Warmtepomp 2|HP2|Flowvergelijking duo|Opvallend vaak om en om/);
  const monitoring = getInstallationMonitoringModel();
  assert.equal(monitoring.cyclingAlertRecovered, true);
  assert.equal(monitoring.problems.some(({ key }) => ["flowMismatch", "alternatingCompressorStartsWarning", "hp2Failures"].includes(key)), false);
});

test("Duo retains zero-count HP2 rows and active Duo diagnostics", () => {
  setup("duo");
  const markup = renderSettingsInstallationMonitoringSection();
  assert.match(markup, /Warmtepomp 2/);
  assert.match(markup, /HP2 2 uur/);
  assert.match(markup, /HP2 72 uur/);
  assert.match(markup, /Flowvergelijking duo/);
  assert.equal(getInstallationMonitoringModel().cyclingAlertActive, true);
  assert.ok(getInstallationMonitoringModel().problems.some(({ key }) => key === "hp2Failures"));
});

test("Single calibration omits HP2 throughout measurement and results even with offsets but no HP2 sensors", () => {
  delete state.entities.hp2WaterIn;
  delete state.entities.hp2WaterOut;
  for (const resultReady of [false, true]) {
    const markup = renderHpWaterCalibrationWizard({ status: resultReady ? "DONE" : "MEASURING", running: !resultReady, resultReady });
    assert.match(markup, /HP1 water/);
    assert.doesNotMatch(markup, /HP2 water/);
  }
});

test("Duo calibration retains HP2 in measurement and results", () => {
  setup("duo");
  for (const resultReady of [false, true]) {
    const markup = renderHpWaterCalibrationWizard({ status: resultReady ? "DONE" : "MEASURING", running: !resultReady, resultReady });
    assert.match(markup, /HP2 water in/);
    assert.match(markup, /HP2 water uit/);
  }
});

test("calibration summary follows topology in both languages", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    for (const topology of ["single", "duo"]) {
      setup(topology);
      const summary = getSettingsServiceModel().tasks.find(({ key }) => key === "hp-water-calibration").summary;
      assert.equal(summary.includes("HP2"), topology === "duo");
      assert.doesNotMatch(summary, /\{heatPumps\}/);
    }
  }
});

test("Single manual service hides HP2 controls and metrics while retaining the CM100 start gate", () => {
  state.entities.cm100Active = entity(false);
  const task = getSettingsServiceModel().tasks.find(({ key }) => key === "manual-hp");
  const markup = task.renderCard();
  assert.match(markup, /manualHp1Mode/);
  assert.doesNotMatch(markup, /manualHp2Mode|manualHp2Level|Warmtepomp 2/);
  assert.equal(task.openDisabled, true);
});

test("Duo manual service retains both pump controls", () => {
  setup("duo");
  const markup = getSettingsServiceModel().tasks.find(({ key }) => key === "manual-hp").renderCard();
  assert.match(markup, /manualHp1Mode/);
  assert.match(markup, /manualHp2Mode/);
  assert.match(markup, /manualHp2Level/);
});

test("explicit topology controls ODU editors, limiter, offsets and runtime even with stale opposite-topology entities", () => {
  for (const topology of ["single", "duo", "single"]) {
    state.entities.installationTopology = entity(topology);
    const duo = topology === "duo";
    for (const indexes of [getOduSettingsHpIndexes, getOduDefrostHpIndexes, getOduRuntimeFrequencyHpIndexes, getOduEepromDumpHpIndexes]) {
      assert.deepEqual(indexes(), duo ? [1, 2] : [1]);
    }
    assert.equal(Boolean(renderSettingsHeatPumpLimiterCard("Warmtepomp 2", "hp2")), duo);
    assert.equal(renderHpWaterSensorOffsetSettings().includes("HP2 water"), duo);
    assert.equal(renderSettingsCounterServiceSection().includes("<span>HP2</span>"), duo);
  }
});

test("Single flow inspector omits Duo meter choices and combined labels but retains outdoor flow", () => {
  const markup = renderSettingsSensorSelectionSection();
  assert.match(markup, /500/);
  assert.match(markup, /data-source-kind="outdoor"\s+data-source-state="available"\s+data-source-effective="true"/);
  assert.doesNotMatch(markup, /data-oq-field="outdoorUnitFlowMode"|data-source-kind="hp2"|Gecombineerd HP1\/HP2/);
});

test("Duo flow inspector retains meter selection and both flowmeters", () => {
  setup("duo");
  const markup = renderSettingsSensorSelectionSection();
  assert.match(markup, /data-oq-field="outdoorUnitFlowMode"/);
  assert.match(markup, /data-source-kind="hp2"/);
  assert.match(markup, /Gecombineerd HP1\/HP2/);
});

test("Single working status ignores unavailable HP2 counters and offers no hypothetical HP2 runtime candidate", () => {
  const current = replay.getControlWorkingCurrent(getHeatPumpPanels());
  assert.equal(current.hp2Available, false);
  assert.equal(current.hp2Starts, "n.v.t.");
  const optimizer = replay.getControlWorkingOptimizerModel({ primaryReason: "runtime_lead", source: "HP1", hp2Available: false });
  assert.deepEqual(optimizer.rows.map(({ option }) => option), ["HP1"]);
  assert.doesNotMatch(optimizer.summary, /gelijkwaardig/);
  assert.doesNotMatch(replay.renderControlReplayView(), /<span>HP2<\/span>|HP2 ·/);
});

test("historical HP2 source events stay visible after switching to Single", () => {
  const event = { event_type: "source_start", subject: "HP2", cm: 2, reason: "runtime_lead" };
  assert.match(replay.getDecisionEventCopy(event).title, /HP2/);
  const optimizer = replay.getControlWorkingOptimizerModel({ reasonCode: "runtime_lead", source: "HP2" });
  assert.ok(optimizer.rows.some(({ option, tone }) => option === "HP2" && tone === "selected"));
});

test("Single replay demand transitions omit two-pump options in both languages while Duo and historical HP2 retain them", () => {
  for (const locale of ["nl", "en"]) {
    setLocale(locale);
    for (const reasonCode of ["less_power", "demand_decreased"]) {
      const item = { reasonCode, source: "HP1", kind: "event", severity: "normal" };
      const model = replay.getControlWorkingOptimizerModel(item);
      assert.deepEqual(model.rows.map(({ option }) => option), ["HP1"]);
      assert.doesNotMatch(replay.renderControlWorkingDetails(item), /Twee warmtepompen|Two heat pumps/);

      const historical = replay.getControlWorkingOptimizerModel({ ...item, source: "HP2" });
      assert.equal(historical.rows[0].option, t("controlReplay.optTwoHp"));
      setup("duo");
      const duo = replay.getControlWorkingOptimizerModel(item);
      assert.equal(duo.rows[0].option, t("controlReplay.optTwoHp"));
      assert.deepEqual(replay.getControlWorkingOptimizerModel({ ...item, hp2Available: false }).rows.map(({ option }) => option), ["HP1"]);
      setup("single");
    }
    assert.equal(replay.getControlWorkingOptimizerModel({ reasonCode: "better_heat", source: "HP1" }), null);
    assert.equal(replay.getControlWorkingOptimizerModel({ reasonCode: "better_heat", source: "HP1 + HP2" }).rows.length, 3);
    assert.equal(replay.getControlWorkingOptimizerModel({ reasonCode: "boiler_assist", source: "HP1" }).rows[0].option, t("controlReplay.optOneHp"));
    setup("duo");
    assert.equal(replay.getControlWorkingOptimizerModel({ reasonCode: "better_heat", source: "HP1" }).rows.length, 3);
    assert.equal(replay.getControlWorkingOptimizerModel({ reasonCode: "boiler_assist", source: "HP1" }).rows[0].option, t("controlReplay.optOnlyHps"));
    setup("single");
  }
});

test("legacy topology inference accepts real HP2 hints but not shared dummy HP2 counters", () => {
  delete state.entities.installationTopology;
  assert.equal(getInstallationTopology(), "duo");
  assert.deepEqual(getOduSettingsHpIndexes(), [1, 2]);
  for (const key of ["hp2Power", "hp2ExcludeMinHz", "hp2WaterOut"]) delete state.entities[key];
  state.lastKnownInstallationTopology = "";
  assert.equal(getInstallationTopology(), "");
  assert.deepEqual(getOduSettingsHpIndexes(), [1]);
  assert.doesNotMatch(renderSettingsInstallationMonitoringSection(), /Warmtepomp 2|HP2/);
});
