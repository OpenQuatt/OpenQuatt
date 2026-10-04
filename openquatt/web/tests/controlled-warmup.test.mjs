import assert from "node:assert/strict";
import test from "node:test";
import { ENTITY_DEFS, SETTINGS_BACKUP_SECTIONS, WARMUP_SETTING_KEYS, WARMUP_STATE_KEYS } from "../js/src/core/config.js";
import { setLocale } from "../js/src/i18n/index.js";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { getWarmupStatusCopy, renderControlledWarmupField, renderSettingsHeatingSection } = await import("../js/src/settings/heating.js");
const { getSettingsGroupHydrationKeys } = await import("../js/src/core/entity-sync.js");

function reset(entities = {}) {
  Object.assign(state, { entities, drafts: {}, inputDrafts: {}, settingsAdvancedOpen: {}, loadingEntities: false, busyAction: "", settingsGroup: "heating" });
}
function number(value, uom = "°C") {
  return { value, state: String(value), min_value: 0.1, max_value: 120, step: 0.1, uom };
}
function enabledEntities() {
  return {
    warmupEnabled: { value: true, state: true }, roomSetpoint: number(20.5),
    warmupTrigger: number(1.5), warmupStep: number(0.1), warmupStepTime: number(45, "min"),
    warmupActive: { value: true, state: true }, warmupEffectiveTarget: number(18.1),
    warmupOffset: number(0.1), warmupStatus: { value: "Warming", state: "Warming" },
  };
}

test("old firmware hides warmup; disabled hides tuning fields", () => {
  reset();
  assert.equal(renderControlledWarmupField(), "");
  reset({ warmupEnabled: { value: false, state: false } });
  const markup = renderControlledWarmupField();
  assert.match(markup, /Gecontroleerd opwarmen/);
  assert.match(markup, /Uitgeschakeld/);
  assert.doesNotMatch(markup, /data-oq-field="warmupStep"/);
});

test("Power House exposes warmup; Heating Curve hides it", () => {
  for (const strategy of ["Power House", "Water Temperature Control (heating curve)"]) {
    reset({ ...enabledEntities(), strategy: { value: strategy, state: strategy } });
    const markup = renderSettingsHeatingSection();
    if (strategy !== "Power House") {
      assert.doesNotMatch(markup, /data-oq-field="warmupStep"/);
      continue;
    }
    assert.match(markup, /Gecontroleerd opwarmen/);
    assert.match(markup, /18,1 °C/);
    assert.match(markup, /Einddoel/);
    assert.match(markup, /Huidige aanwarmstap/);
    assert.match(markup, /Een bestaand tussendoel daalt niet bij afkoelen/);
    assert.match(markup, /20,5 °C/);
    assert.match(markup, /Opwarmen bezig/);
    assert.match(markup, /data-oq-field="warmupStep"/);
    assert.match(markup, /na 8 uur/);
    assert.doesNotMatch(markup, /data-oq-field="warmupMax/);
  }
});

test("locale switching translates status and numeric presentation without writes or draft loss", () => {
  reset(enabledEntities());
  state.inputDrafts.warmupStep = "0.2";
  const entities = structuredClone(state.entities);
  try {
    setLocale("en", { persist: false });
    const markup = renderControlledWarmupField();
    assert.match(markup, /Controlled warmup/);
    assert.match(markup, /Warmup active/);
    assert.match(markup, /18\.1 °C/);
    assert.match(markup, /Final target/);
    assert.match(markup, /Current warmup step/);
    assert.match(markup, /maximum warmup step is 0.5/);
    assert.match(markup, /does not decrease as the room cools/);
    assert.match(markup, /20\.5 °C/);
    assert.equal(getWarmupStatusCopy("Time limit reached"), "Maximum warmup duration reached");
    assert.equal(getWarmupStatusCopy("Source changed"), "Source changed");
    assert.equal(getWarmupStatusCopy("<script>"), "Status unknown");
    assert.deepEqual(state.entities, entities);
    assert.equal(state.inputDrafts.warmupStep, "0.2");
  } finally {
    setLocale("nl", { persist: false });
  }
  assert.match(renderControlledWarmupField(), /18,1 °C/);
});

test("missing diagnostics remain unknown instead of inventing idle or zero values", () => {
  reset({ warmupEnabled: { value: true, state: true } });
  const markup = renderControlledWarmupField();
  assert.match(markup, /Status onbekend/);
  assert.match(markup, /<strong>—<\/strong>/);
  assert.doesNotMatch(markup, /0,0 °C/);
});

test("warmup backup contains settings only and heating hydrates both setting and state entities", () => {
  const backup = SETTINGS_BACKUP_SECTIONS.find((section) => section.id === "warmup");
  assert.deepEqual(backup.keys, WARMUP_SETTING_KEYS);
  reset();
  const keys = getSettingsGroupHydrationKeys("heating");
  assert.ok(keys.includes("roomSetpoint"));
  for (const key of [...WARMUP_SETTING_KEYS, ...WARMUP_STATE_KEYS]) {
    assert.ok(ENTITY_DEFS[key]);
    assert.ok(keys.includes(key), key);
  }
  for (const key of WARMUP_STATE_KEYS) assert.ok(!backup.keys.includes(key));
});


test("warmup switch provides locale labels for live patches and preserves its copy", () => {
  for (const [locale, on, off, title] of [["nl", "Aan", "Uit", "Gecontroleerd opwarmen toestaan"], ["en", "On", "Off", "Allow controlled warmup"]]) {
    try {
      setLocale(locale, { persist: false });
      for (const enabled of [false, true]) {
        reset({ warmupEnabled: { value: enabled, state: enabled } });
        const markup = renderControlledWarmupField();
        assert.match(markup, new RegExp('data-on-label="' + on + '"'));
        assert.match(markup, new RegExp('data-off-label="' + off + '"'));
        assert.match(markup, new RegExp('aria-label="' + title + ': ' + (enabled ? on : off) + '"'));
        assert.match(markup, /data-on-copy="[^"]+" data-off-copy="[^"]+"/);
        assert.doesNotMatch(markup, /data-(?:on|off)-label="null"/);
      }
    } finally {
      setLocale("nl", { persist: false });
    }
  }
});
