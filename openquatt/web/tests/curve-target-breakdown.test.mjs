import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };

const { state } = await import("../js/src/core/state.js");
const { CURVE_SETTING_KEYS } = await import("../js/src/core/config.js");
const { setLocale } = await import("../js/src/i18n/index.js");
const { renderSettingsCurveInputs } = await import("../js/src/settings/heating.js");

function setTargetEntities(overrides = {}) {
  state.entities = Object.fromEntries(Object.entries({
    curveBaseTarget: 29.2,
    curveModifier: 0.9,
    curveRoomTrim: 0,
    curveEffectiveTarget: 30,
    curveControlProfile: "Balanced",
    heatingSupplyTargetActiveSource: "curve",
    maxWater: 45,
    ...overrides,
  }).map(([key, value]) => [key, { value }]));
  state.drafts = {};
  state.simpleCurveDraft = null;
  state.curvePointDraft = null;
  setLocale("nl");
}

test("het effectieve doel blijft firmwaredata met zichtbare afronding, grens en getekende correcties", () => {
  setTargetEntities();
  const markup = renderSettingsCurveInputs();
  assert.match(markup, /<strong>29,2 °C<\/strong>/);
  assert.match(markup, /<strong>\+0,9 °C<\/strong>/);
  assert.match(markup, /<strong>30 °C<\/strong>/);
  assert.match(markup, /<strong>0 °C<\/strong>/);
  assert.match(markup, /Afronding na correcties: stappen van 0,5 °C/);
  assert.match(markup, /Maximaal 45 °C volgens Installatie/);
  assert.doesNotMatch(markup, /<strong>30,1 °C<\/strong>/);

  setTargetEntities({ curveModifier: -0.9, curveRoomTrim: -0.4, curveEffectiveTarget: 28 });
  const corrected = renderSettingsCurveInputs();
  assert.match(corrected, /<strong>-0,9 °C<\/strong>/);
  assert.match(corrected, /<strong>-0,4 °C<\/strong>/);
  assert.match(corrected, /<strong>28 °C<\/strong>/);
});

test("afrondingsstappen volgen het bevestigde profiel en wisselen mee met de taal", () => {
  for (const [profile, step] of [["Comfort", "0,25"], ["Balanced", "0,5"], ["Stable", "1"]]) {
    setTargetEntities({ curveControlProfile: profile });
    assert.ok(renderSettingsCurveInputs().includes(`stappen van ${step} °C`));
  }
  state.drafts.curveControlProfile = "Comfort";
  assert.match(renderSettingsCurveInputs(), /stappen van 1 °C/);
  setLocale("en");
  const english = renderSettingsCurveInputs();
  assert.match(english, /Rounding after corrections: steps of 1 °C/);
  assert.match(english, /<strong>\+0\.9 °C<\/strong>/);
  assert.match(english, /Maximum 45 °C from Installation/);
  setLocale("nl");
});

test("een extern aanvoerdoel krijgt geen uitleg die stooklijnafronding belooft", () => {
  assert.ok(CURVE_SETTING_KEYS.includes("heatingSupplyTargetActiveSource"));
  setTargetEntities({ heatingSupplyTargetActiveSource: "external", curveEffectiveTarget: 42.3 });
  const markup = renderSettingsCurveInputs();
  assert.match(markup, /Extern aanvoerdoel actief: vervangt de stooklijn en de correcties/);
  assert.match(markup, /<strong>42,3 °C<\/strong>/);
  assert.doesNotMatch(markup, /Afronding na correcties/);
  assert.match(markup, /Maximaal 45 °C/);
});

test("ontbrekende diagnostiek blijft onbekend zonder een verzonnen nul of afrondingsstap", () => {
  setTargetEntities({ curveControlProfile: "unknown", maxWater: "", curveRoomTrim: "", curveEffectiveTarget: Infinity });
  delete state.entities.curveModifier;
  const markup = renderSettingsCurveInputs();
  assert.match(markup, /Afronding na correcties volgens het regelprofiel/);
  assert.equal((markup.match(/<strong>—<\/strong>/g) || []).length, 3);
  assert.doesNotMatch(markup, /stappen van|Maximaal|Infinity/);
  delete state.entities.curveBaseTarget;
  assert.doesNotMatch(renderSettingsCurveInputs(), /oq-simple-curve-breakdown|oq-simple-curve-target-note/);
});
