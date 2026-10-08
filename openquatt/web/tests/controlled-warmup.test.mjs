import assert from "node:assert/strict";
import test from "node:test";
import { ENTITY_DEFS, SETTINGS_BACKUP_SECTIONS, WARMUP_SETTING_KEYS, WARMUP_STATE_KEYS } from "../js/src/core/config.js";
import { setLocale } from "../js/src/i18n/index.js";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { renderControlledWarmupField, renderSettingsHeatingSection } = await import("../js/src/settings/heating.js");
const { getSettingsGroupHydrationKeys } = await import("../js/src/core/entity-sync.js");

function reset(entities = {}) {
  Object.assign(state, { entities, drafts: {}, inputDrafts: {}, settingsAdvancedOpen: {}, loadingEntities: false, busyAction: "", settingsGroup: "heating" });
}
function number(value, uom = "°C") {
  return { value, state: String(value), min_value: 0.1, max_value: 120, step: 0.1, uom };
}
function enabledEntities() {
  return {
    warmupEnabled: { value: true, state: true }, roomSetpoint: number(20.5), roomTemp: number(18.06),
    warmupTrigger: number(1.5), warmupStep: number(0.1), warmupStepTime: number(45, "min"),
    warmupActive: { value: true, state: true }, warmupEffectiveTarget: number(18.1),
  };
}

test("old firmware hides warmup; disabled hides tuning fields", () => {
  reset();
  assert.equal(renderControlledWarmupField(), "");
  reset({ warmupEnabled: { value: false, state: false } });
  const markup = renderControlledWarmupField();
  assert.match(markup, /Geleidelijk opwarmen/);
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
    assert.match(markup, /Geleidelijk opwarmen/);
    assert.match(markup, /data-oq-warmup-reading="roomTemp">18,06 °C/);
    assert.match(markup, /data-oq-warmup-reading="warmupEffectiveTarget">18,10 °C/);
    assert.match(markup, /Einddoel/);
    assert.match(markup, /Een bestaand tussendoel daalt niet bij afkoelen/);
    assert.match(markup, /data-oq-warmup-reading="roomSetpoint">20,50 °C/);
    assert.match(markup, /Opwarmen in stappen/);
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
    assert.match(markup, /Gradual warmup/);
    assert.match(markup, /Warming up in steps/);
    assert.match(markup, /data-oq-warmup-reading="roomTemp">18\.06 °C/);
    assert.match(markup, /data-oq-warmup-reading="warmupEffectiveTarget">18\.10 °C/);
    assert.match(markup, /Final target/);
    assert.match(markup, /maximum warmup step is 0.5/);
    assert.match(markup, /does not decrease as the room cools/);
    assert.match(markup, /data-oq-warmup-reading="roomSetpoint">20\.50 °C/);
    assert.deepEqual(state.entities, entities);
    assert.equal(state.inputDrafts.warmupStep, "0.2");
  } finally {
    setLocale("nl", { persist: false });
  }
  assert.match(renderControlledWarmupField(), /data-oq-warmup-reading="roomTemp">18,06 °C/);
});

test("inactive limiter has neutral copy without inventing a cause", () => {
  reset({ ...enabledEntities(), warmupActive: { value: false, state: false } });
  assert.match(renderControlledWarmupField(), /Niet actief/);
  assert.doesNotMatch(renderControlledWarmupField(), /Wacht op een setpointverhoging/);
});

test("warmup explanations distinguish thermostat changes, measured room steps and timeout with numeric examples", () => {
  const examples = {
    nl: [
      /vóór de thermostaat vanuit de nachtstand omhoog gaat/,
      /al op 20,5 °C[\s\S]+gewoon verder naar 20,5 °C[\s\S]+niet alsnog in kleine stappen/,
      /niet de gemeten kamertemperatuur/,
      /van 17 naar 20,5 °C[\s\S]+verhoging van 3,5 °C/,
      /Van 19 naar 20,5 °C[\s\S]+precies 1,5 °C[\s\S]+dus geen start/,
      /meet je 18,0 °C[\s\S]+tussendoel 18,1 °C/,
      /volgt direct 18,2 °C; je hoeft niet op de ingestelde tijd te wachten/,
      /na 45 minuten nog 18,0 °C[\s\S]+van 0,1 naar 0,2 °C[\s\S]+nieuwe tussendoel 18,2 °C/,
    ],
    en: [
      /before the thermostat temperature rises from its night setting/,
      /already set to 20\.5 °C[\s\S]+continues normal heating towards 20\.5 °C[\s\S]+not converted to small steps/,
      /not the measured room temperature/,
      /raising 17 to 20\.5 °C[\s\S]+increase of 3\.5 °C/,
      /Raising 19 to 20\.5 °C[\s\S]+exactly 1\.5 °C[\s\S]+warmup does not start/,
      /reading of 18\.0 °C[\s\S]+intermediate target of 18\.1 °C/,
      /18\.2 °C follows immediately; you do not need to wait/,
      /18\.0 °C after 45 minutes[\s\S]+from 0\.1 to 0\.2 °C[\s\S]+target becomes 18\.2 °C/,
    ],
  };
  try {
    reset(enabledEntities());
    for (const [locale, patterns] of Object.entries(examples)) {
      setLocale(locale, { persist: false });
      const markup = renderControlledWarmupField();
      for (const pattern of patterns) assert.match(markup, pattern);
    }
  } finally {
    setLocale("nl", { persist: false });
  }
});

test("missing diagnostics remain unknown instead of inventing idle or zero values", () => {
  for (const diagnostic of [undefined, {}, { value: null, state: null }, { state: "unknown" }]) {
    reset({ warmupEnabled: { value: true, state: true }, warmupActive: diagnostic });
    const markup = renderControlledWarmupField();
    assert.match(markup, /Status onbekend/);
    for (const key of ["roomTemp", "warmupEffectiveTarget", "roomSetpoint"]) {
      assert.match(markup, new RegExp(`<strong data-oq-warmup-reading="${key}">—<\\/strong>`));
    }
    assert.doesNotMatch(markup, /0,0 °C/);
  }
});

test("warmup backup contains settings only and heating hydrates both setting and state entities", () => {
  const backup = SETTINGS_BACKUP_SECTIONS.find((section) => section.id === "warmup");
  assert.deepEqual(backup.keys, WARMUP_SETTING_KEYS);
  assert.deepEqual(WARMUP_STATE_KEYS, ["warmupActive", "warmupEffectiveTarget", "warmupStatus", "warmupElapsed"]);
  assert.ok(!ENTITY_DEFS.warmupOffset);
  reset();
  const keys = getSettingsGroupHydrationKeys("heating");
  assert.ok(keys.includes("roomSetpoint"));
  assert.ok(keys.includes("roomTemp"));
  for (const key of [...WARMUP_SETTING_KEYS, ...WARMUP_STATE_KEYS]) {
    assert.ok(ENTITY_DEFS[key]);
    assert.ok(keys.includes(key), key);
  }
  for (const key of WARMUP_STATE_KEYS) assert.ok(!backup.keys.includes(key));
});


test("warmup switch provides locale labels for live patches and preserves its copy", () => {
  for (const [locale, on, off, title] of [["nl", "Aan", "Uit", "Geleidelijk opwarmen inschakelen"], ["en", "On", "Off", "Enable gradual warmup"]]) {
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
