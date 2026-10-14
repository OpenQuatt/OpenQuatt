import assert from "node:assert/strict";
import test from "node:test";
import { ENTITY_DEFS, SETTINGS_BACKUP_SECTIONS, WARMUP_SETTING_KEYS, WARMUP_STATE_KEYS } from "../js/src/core/config.js";
import { setLocale } from "../js/src/i18n/index.js";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { location: { pathname: "/" }, localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { patchControlledWarmupField, renderControlledWarmupField, renderSettingsHeatingSection } = await import("../js/src/settings/heating.js");
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
    warmupActive: { value: true, state: true }, warmupIntermediateTarget: number(18.1), warmupBoost: number(0.1), warmupEffectiveTarget: number(18.2),
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
    assert.match(markup, /data-oq-warmup-reading="warmupEffectiveTarget">18,20 °C/);
    assert.match(markup, /Einddoel/);
    assert.match(markup, /Het tussendoel blijft 18,1 °C/);
    assert.match(markup, /data-oq-warmup-reading="warmupIntermediateTarget">18,10 °C/);
    assert.match(markup, /data-oq-warmup-reading="warmupBoost">0,10 °C/);
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
    assert.match(markup, /data-oq-warmup-reading="warmupEffectiveTarget">18\.20 °C/);
    assert.match(markup, /Final target/);
    assert.match(markup, /maximum boost is 0.5/);
    assert.match(markup, /intermediate target stays at 18\.1 °C/);
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
    nl: [/gemeten kamertemperatuur/, /2,5 °C verschil, dus een start/, /precies 1,5 °C, dus geen start/,
      /boost stijgt van 0,0 naar 0,1 °C/, /tussendoel blijft 18,1 °C/, /wordt de boost nul/],
    en: [/measured room temperature/, /2\.5 °C gap, so warmup starts/, /exactly 1\.5 °C, so no start/,
      /Boost rises from 0\.0 to 0\.1 °C/, /intermediate target stays at 18\.1 °C/, /boost resets to zero/],
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
    for (const key of ["roomTemp", "warmupIntermediateTarget", "warmupBoost", "warmupEffectiveTarget", "roomSetpoint"]) {
      assert.match(markup, new RegExp(`<strong data-oq-warmup-reading="${key}">—<\\/strong>`));
    }
    assert.doesNotMatch(markup, /0,0 °C/);
  }
});

test("warmup backup contains settings only and heating hydrates both setting and state entities", () => {
  const backup = SETTINGS_BACKUP_SECTIONS.find((section) => section.id === "warmup");
  assert.deepEqual(backup.keys, WARMUP_SETTING_KEYS);
  assert.deepEqual(WARMUP_STATE_KEYS, ["warmupActive", "warmupIntermediateTarget", "warmupBoost", "warmupEffectiveTarget", "warmupStatus", "warmupElapsed"]);
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


test("live boost patches preserve the fixed target and input drafts in both locales", () => {
  try {
    for (const locale of ["nl", "en"]) {
      setLocale(locale, { persist: false });
      reset(enabledEntities());
      state.inputDrafts.warmupStep = "0.2";
      const nodes = new Map([
        ["[data-oq-warmup-status]", { textContent: "" }],
        ["[data-oq-warmup-enabled]", { dataset: { oqWarmupEnabled: "true" } }],
        ...["warmupIntermediateTarget", "warmupBoost", "warmupEffectiveTarget"].map((key) =>
          [`[data-oq-warmup-reading="${key}"]`, { textContent: "" }]),
      ]);
      const root = { querySelector: (selector) => nodes.get(selector) };
      assert.equal(patchControlledWarmupField(root), true);
      const fixedNode = nodes.get('[data-oq-warmup-reading="warmupIntermediateTarget"]');
      const fixed = fixedNode.textContent;
      state.entities.warmupBoost = number(0.2);
      state.entities.warmupEffectiveTarget = number(18.3);
      assert.equal(patchControlledWarmupField(root), true);
      assert.equal(fixedNode.textContent, fixed);
      assert.equal(nodes.get('[data-oq-warmup-reading="warmupBoost"]').textContent, locale === "nl" ? "0,20 °C" : "0.20 °C");
      assert.equal(nodes.get('[data-oq-warmup-reading="warmupEffectiveTarget"]').textContent, locale === "nl" ? "18,30 °C" : "18.30 °C");
      assert.equal(state.inputDrafts.warmupStep, "0.2");
    }
  } finally {
    setLocale("nl", { persist: false });
  }
});
