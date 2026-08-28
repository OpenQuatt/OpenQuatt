import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  location: { pathname: "/" },
  setTimeout: globalThis.setTimeout,
  clearTimeout: globalThis.clearTimeout,
  localStorage: {
    getItem: () => null,
  },
};

const { state } = await import("../js/src/core/state.js");
const { commitSelect, getNumberSettingValidationError } = await import("../js/src/core/entity-write-actions.js");
const { SETTINGS_GROUP_KEY_MAP } = await import("../js/src/core/entity-sync.js");
const { handleSystemAction } = await import("../js/src/features/system-actions.js");
const { renderControlModeOverrideBanner, renderSystemModal } = await import("../js/src/features/header-status.js");
const { renderSettingsCoolingSection } = await import("../js/src/settings/cooling.js");
const {
  renderHeatingCurveAdvancedFields,
  renderPowerHouseBaseFields,
  renderSettingsHeatPumpLimiterCard,
  renderSettingsFlowSection,
} = await import("../js/src/settings/heating.js");
const { renderSilentSettingsGrid } = await import("../js/src/settings/silent.js");
const {
  renderSettingsControlModeOverridePanel,
  renderSettingsCounterServiceSection,
} = await import("../js/src/settings/service.js");
const { renderSettingsElectricalCurrentLimitSection } = await import("../js/src/settings/electrical-limit.js");
const settingsCoreSource = await readFile(new URL("../js/src/settings/core.js", import.meta.url), "utf8");

function numberEntity(value, uom = "", extra = {}) {
  return {
    value,
    state: String(value),
    min_value: 0,
    max_value: 10000,
    step: 0.1,
    uom,
    ...extra,
  };
}

function resetSettingsState(entities = {}) {
  state.entities = entities;
  state.drafts = {};
  state.inputDrafts = {};
  state.settingsAdvancedOpen = {};
  state.loadingEntities = false;
  state.busyAction = "";
  state.controlError = "";
  state.controlNotice = "";
  state.systemModal = "";
  state.pendingControlModeOverride = "";
}

test("Power House toont de instelbare koude referentie met Quatt-standaard", () => {
  resetSettingsState({
    houseColdTemp: numberEntity(-10, "°C", { min_value: -25, max_value: 5, step: 0.5 }),
    housePower: numberEntity(4500, "W"),
  });

  const markup = renderPowerHouseBaseFields();

  assert.match(markup, /Koude referentietemperatuur/);
  assert.match(markup, /Bij Quatt is -10 °C de standaard/);
  assert.match(markup, /Nominaal woningvermogen/);
  assert.match(markup, /bij de koude referentietemperatuur hierboven/);
});

test("specialistische PID-instellingen staan standaard ingeklapt onder Geavanceerd", () => {
  resetSettingsState({
    heatingCurvePidKp: numberEntity(0.28),
    heatingCurvePidKi: numberEntity(0.0006),
    heatingCurvePidKd: numberEntity(0.2),
  });
  const curveMarkup = renderHeatingCurveAdvancedFields();
  assert.match(curveMarkup, /data-oq-settings-advanced="heating-curve"/);
  assert.doesNotMatch(curveMarkup, /data-oq-settings-advanced="heating-curve" open/);
  assert.match(curveMarkup, /Geavanceerde stooklijnafstelling/);

  state.settingsAdvancedOpen["heating-curve"] = true;
  assert.match(renderHeatingCurveAdvancedFields(), /data-oq-settings-advanced="heating-curve" open/);
  assert.match(renderHeatingCurveAdvancedFields(), /oq-settings-grid--pid/);

  resetSettingsState({
    coolingPidKp: numberEntity(3),
    coolingPidKi: numberEntity(0.12),
    coolingPidKd: numberEntity(0),
  });
  const coolingMarkup = renderSettingsCoolingSection();
  assert.match(coolingMarkup, /data-oq-settings-advanced="cooling"/);
  assert.match(coolingMarkup, /oq-settings-grid--pid/);

  resetSettingsState({
    flowKp: numberEntity(0.35),
    flowKi: numberEntity(0.05),
  });
  assert.match(renderSettingsFlowSection(), /data-oq-settings-advanced="flow"/);
});

test("start- en stopgrens kunnen elkaar niet passeren", () => {
  const entities = {
    boilerSupportStartThreshold: numberEntity(1000, "W"),
    boilerSupportStopThreshold: numberEntity(400, "W"),
  };

  assert.equal(getNumberSettingValidationError("boilerSupportStartThreshold", 400, entities), "De startgrens moet hoger zijn dan de stopgrens (400 W).");
  assert.equal(getNumberSettingValidationError("boilerSupportStopThreshold", 1000, entities), "De stopgrens moet lager zijn dan de startgrens (1000 W).");
  assert.equal(getNumberSettingValidationError("boilerSupportStartThreshold", 1001, entities), "");
  assert.equal(getNumberSettingValidationError("boilerSupportStopThreshold", 399, entities), "");
});

test("uitgesloten frequentiebereiken kunnen niet worden omgekeerd", () => {
  const entities = {
    hp1HeatingExcludeAMinHz: numberEntity(55, "Hz"),
    hp1HeatingExcludeAMaxHz: numberEntity(61, "Hz"),
  };

  assert.equal(
    getNumberSettingValidationError("hp1HeatingExcludeAMinHz", 62, entities),
    "De ondergrens mag niet hoger zijn dan de bovengrens (61 Hz).",
  );
  assert.equal(
    getNumberSettingValidationError("hp1HeatingExcludeAMaxHz", 54, entities),
    "De bovengrens mag niet lager zijn dan de ondergrens (55 Hz).",
  );
  assert.equal(getNumberSettingValidationError("hp1HeatingExcludeAMinHz", 0, entities), "");
  assert.equal(getNumberSettingValidationError("hp1HeatingExcludeAMaxHz", 55, entities), "");
});

test("nieuwe installatie- en service-entiteiten worden bij het juiste scherm geladen", () => {
  assert.ok(SETTINGS_GROUP_KEY_MAP.installation.includes("electricalCurrentLimit"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.installation.includes("boilerSupportStartThreshold"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.installation.includes("boilerSupportStopThreshold"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.installation.includes("hp1HeatingExcludeAMinHz"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.installation.includes("hp2CoolingExcludeBMaxHz"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.service.includes("controlModeOverride"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.service.includes("hp1RuntimeHours"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.service.includes("resetRuntimeCountersHp1Hp2"));
  assert.ok(!SETTINGS_GROUP_KEY_MAP.service.includes("resetCumulativeEnergyCounters"));
});

test("elektrische ingangsgrens respecteert Single en Duo maxima", () => {
  const limit = numberEntity(20, "A", { min_value: 10, max_value: 20, step: 0.5 });
  resetSettingsState({
    installationTopology: { value: "single", state: "single" },
    hpGeneration: { value: "V2", state: "V2" },
    electricalCurrentLimit: limit,
  });

  let markup = renderSettingsElectricalCurrentLimitSection();
  assert.match(markup, /Elektrische ingangsgrens/);
  assert.match(markup, /max="16"/);
  assert.match(markup, /circa 3650 W/);
  assert.match(markup, /Stooklijn en koelen gebruiken alleen de gemeten feedback/);
  assert.match(markup, /geen elektrische beveiliging/);

  resetSettingsState({
    installationTopology: { value: "duo", state: "duo" },
    hpGeneration: { value: "V2", state: "V2" },
    electricalCurrentLimit: limit,
  });
  markup = renderSettingsElectricalCurrentLimitSection();
  assert.match(markup, /max="20"/);
  assert.match(markup, /circa 4563 W/);

  resetSettingsState({
    installationTopology: { value: "duo", state: "duo" },
    hpGeneration: { value: "V2", state: "V2" },
    electricalCurrentLimit: { ...limit, value: 5, state: "5" },
  });
  markup = renderSettingsElectricalCurrentLimitSection();
  assert.match(markup, /value="10"/);
});

test("elektrische ingangsgrens staat voor ODU runtime", () => {
  const installationStart = settingsCoreSource.indexOf('activeGroup === "installation"');
  const installationEnd = settingsCoreSource.indexOf(': activeGroup === "service"', installationStart);
  const installationOrder = settingsCoreSource.slice(installationStart, installationEnd);
  const electricalLimitIndex = installationOrder.indexOf("renderSettingsElectricalCurrentLimitSection()");
  const oduRuntimeIndex = installationOrder.indexOf("renderSettingsOduRuntimeFrequencySection()");

  assert.ok(electricalLimitIndex >= 0);
  assert.ok(oduRuntimeIndex > electricalLimitIndex);
});

test("koelsterkte licht de lagere limiet tijdens stille modus toe", () => {
  const baseEntities = {
    coolingDemandMax: numberEntity(8, "", { min_value: 1, max_value: 10, step: 1 }),
    silentMax: numberEntity(6, "", { min_value: 1, max_value: 10, step: 1 }),
    silentModeOverride: { value: "Schedule", state: "Schedule" },
    silentActive: { value: false, state: "OFF" },
  };

  resetSettingsState(baseEntities);
  let markup = renderSettingsCoolingSection();
  assert.match(markup, /Tijdens stille modus wordt koelen begrensd op niveau 6/);
  assert.match(markup, /Deze maximale koelsterkte wordt dan niet volledig gebruikt/);

  resetSettingsState({
    ...baseEntities,
    silentActive: { value: true, state: "ON" },
  });
  markup = renderSettingsCoolingSection();
  assert.match(markup, /Stille modus is nu actief\. Koelen wordt begrensd op niveau 6/);

  resetSettingsState({
    ...baseEntities,
    silentModeOverride: { value: "Off", state: "Off" },
  });
  assert.doesNotMatch(renderSettingsCoolingSection(), /oq-settings-cooling-limit-warning/);

  resetSettingsState({
    ...baseEntities,
    coolingDemandMax: numberEntity(6, "", { min_value: 1, max_value: 10, step: 1 }),
  });
  assert.doesNotMatch(renderSettingsCoolingSection(), /oq-settings-cooling-limit-warning/);

  resetSettingsState({
    coolingDemandMax: baseEntities.coolingDemandMax,
    silentModeOverride: baseEntities.silentModeOverride,
  });
  assert.doesNotMatch(renderSettingsCoolingSection(), /oq-settings-cooling-limit-warning/);
});

test("frequentielimieten vervangen de oude levelvelden en blijven per modus gescheiden", () => {
  resetSettingsState({
    silentStartTime: { value: "22:00", state: "22:00" },
    silentEndTime: { value: "07:00", state: "07:00" },
    silentHeatingMaxHz: numberEntity(60, "Hz", { max_value: 120, step: 1 }),
    silentCoolingMaxHz: numberEntity(46, "Hz", { max_value: 120, step: 1 }),
    dayHeatingMaxHz: numberEntity(90, "Hz", { max_value: 120, step: 1 }),
    dayCoolingMaxHz: numberEntity(71, "Hz", { max_value: 120, step: 1 }),
    silentMax: numberEntity(6),
    dayMax: numberEntity(10),
  });

  const markup = renderSilentSettingsGrid();
  assert.match(markup, /Maximale compressorfrequentie verwarmen tijdens stille uren/);
  assert.match(markup, /Maximale compressorfrequentie koelen overdag/);
  assert.doesNotMatch(markup, /Maximaal niveau tijdens stille uren/);
});

test("compressorinstellingen tonen twee frequentiebereiken per modus met legacy fallback", () => {
  resetSettingsState({
    hp1HeatingExcludeAMinHz: numberEntity(55, "Hz", { max_value: 120, step: 1 }),
    hp1HeatingExcludeAMaxHz: numberEntity(61, "Hz", { max_value: 120, step: 1 }),
    hp1HeatingExcludeBMinHz: numberEntity(0, "Hz", { max_value: 120, step: 1 }),
    hp1HeatingExcludeBMaxHz: numberEntity(0, "Hz", { max_value: 120, step: 1 }),
    hp1CoolingExcludeAMinHz: numberEntity(0, "Hz", { max_value: 120, step: 1 }),
    hp1CoolingExcludeAMaxHz: numberEntity(0, "Hz", { max_value: 120, step: 1 }),
    hp1CoolingExcludeBMinHz: numberEntity(0, "Hz", { max_value: 120, step: 1 }),
    hp1CoolingExcludeBMaxHz: numberEntity(0, "Hz", { max_value: 120, step: 1 }),
  });
  let markup = renderSettingsHeatPumpLimiterCard("Warmtepomp 1", "hp1", "hp1ExcludedA", "hp1ExcludedB");
  assert.match(markup, /Verwarmen · bereik A vanaf/);
  assert.match(markup, /Koelen · bereik B tot en met/);
  assert.match(markup, /0-grens staat uit/);

  resetSettingsState({
    hp1ExcludedA: { value: "L4", state: "L4", option: ["None", "L4"] },
    hp1ExcludedB: { value: "None", state: "None", option: ["None", "L4"] },
  });
  markup = renderSettingsHeatPumpLimiterCard("Warmtepomp 1", "hp1", "hp1ExcludedA", "hp1ExcludedB");
  assert.match(markup, /Stand A/);
  assert.doesNotMatch(markup, /Verwarmen · bereik A vanaf/);
});

test("koelscherm licht de stille frequentiegrens toe", () => {
  resetSettingsState({
    coolingDemandMax: numberEntity(10, "", { min_value: 1, max_value: 10, step: 1 }),
    silentCoolingMaxHz: numberEntity(46, "Hz", { max_value: 120, step: 1 }),
    silentModeOverride: { value: "Schedule", state: "Schedule" },
    silentActive: { value: true, state: "ON" },
  });
  assert.match(renderSettingsCoolingSection(), /Koelen wordt begrensd op een compressorfrequentie van 46 Hz/);
});

test("koelscherm laadt de stille-moduslimiet en actuele status", () => {
  assert.ok(SETTINGS_GROUP_KEY_MAP.cooling.includes("silentModeOverride"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.cooling.includes("silentActive"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.cooling.includes("silentMax"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.cooling.includes("silentCoolingMaxHz"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.cooling.includes("coolingRestartMode"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.cooling.includes("coolingMinimumOffTime"));
});

test("koelherstart toont alleen de instelling van de gekozen modus", () => {
  const restartMode = {
    value: "Water temperature",
    state: "Water temperature",
    option: ["Water temperature", "Minimum off time"],
  };
  const baseEntities = {
    coolingRestartMode: restartMode,
    coolingRestartDelta: numberEntity(1, "°C", { min_value: 0, max_value: 5, step: 0.1 }),
    coolingMinimumOffTime: numberEntity(600, "s", { min_value: 240, max_value: 3600, step: 30 }),
  };

  resetSettingsState(baseEntities);
  let markup = renderSettingsCoolingSection();
  assert.match(markup, /Herstartvoorwaarde/);
  assert.match(markup, /helpt zo pendelgedrag te verminderen/);
  assert.match(markup, /vaste minimale uit-tijd van iedere compressor \(4 minuten\) blijft in beide modi altijd gelden/);
  assert.match(markup, /Herstartmarge watertemperatuur/);
  assert.doesNotMatch(markup, /Minimale uit-tijd koelen/);

  resetSettingsState({
    ...baseEntities,
    coolingRestartMode: {
      ...restartMode,
      value: "Minimum off time",
      state: "Minimum off time",
    },
  });
  markup = renderSettingsCoolingSection();
  assert.match(markup, /Minimale uit-tijd koelen/);
  assert.match(markup, /Bij Duo geldt dit voor beide warmtepompen/);
  assert.match(markup, /vaste minimale compressor-uit-tijd \(4 minuten\) voorbij is/);
  assert.doesNotMatch(markup, /Herstartmarge watertemperatuur/);
});

test("Service toont runtime, draaiurenreset en de bevestigde tijdelijke override", () => {
  resetSettingsState({
    controlModeOverride: {
      value: "Force CM1",
      state: "Force CM1",
      option: ["Auto", "Force CM0", "Force CM1", "Force CM98"],
    },
    hp1RuntimeHours: numberEntity(2854, "h"),
    hp2RuntimeHours: numberEntity(2761, "h"),
    runtimeLeadHp: { value: "HP2", state: "HP2" },
    resetRuntimeCountersHp1Hp2: { value: "", state: "" },
    resetCumulativeEnergyCounters: { value: "", state: "" },
  });

  const overrideMarkup = renderSettingsControlModeOverridePanel();
  assert.match(overrideMarkup, /Testmodus actief/);
  assert.match(overrideMarkup, /CM1 · alleen circulatie/);
  assert.match(overrideMarkup, /30 minuten/);
  assert.match(overrideMarkup, /Terug naar automatisch/);

  const counterMarkup = renderSettingsCounterServiceSection();
  assert.match(counterMarkup, /oq-settings-runtime-balance/);
  assert.match(counterMarkup, /<span>HP1<\/span>/);
  assert.match(counterMarkup, /<span>HP2<\/span>/);
  assert.match(counterMarkup, /93 h verschil/);
  assert.match(counterMarkup, /HP2 leidend/);
  assert.match(counterMarkup, /Balans resetten/);
  assert.match(counterMarkup, /oq-settings-runtime-reset/);
  assert.doesNotMatch(counterMarkup, /Energietellers resetten/);
  assert.doesNotMatch(counterMarkup, /open-energy-counter-reset-confirm/);
});

test("Single toont één compacte draaitijd zonder Duo-balans", () => {
  resetSettingsState({
    hp1RuntimeHours: numberEntity(2854, "h"),
    resetRuntimeCountersHp1: { value: "", state: "" },
  });

  const markup = renderSettingsCounterServiceSection();
  assert.match(markup, /oq-settings-runtime-balance is-single/);
  assert.match(markup, /<span>HP1<\/span>/);
  assert.doesNotMatch(markup, /<span>HP2<\/span>/);
  assert.doesNotMatch(markup, /h verschil/);
  assert.match(markup, /data-oq-action="open-runtime-reset-confirm"/);
});

test("runtime-reset kiest de beschikbare Duo-knop zonder klikfout", async () => {
  resetSettingsState({
    resetRuntimeCountersHp1Hp2: { value: "", state: "" },
  });
  state.systemModal = "runtime-reset-confirm";
  const requests = [];
  const originalFetch = globalThis.fetch;
  const originalLocation = globalThis.window.location;
  globalThis.window.location = { pathname: "/" };
  globalThis.fetch = async (url) => {
    requests.push(String(url));
    return { ok: true };
  };

  try {
    assert.equal(handleSystemAction("confirm-runtime-reset", {}), true);
    await new Promise((resolve) => setTimeout(resolve, 0));
    assert.equal(state.controlError, "");
    assert.equal(state.systemModal, "");
    assert.equal(requests.length, 1);
    assert.match(requests[0], /Reset%20Runtime%20Counters%20\(HP1%2BHP2\)\/press$/);
  } finally {
    globalThis.fetch = originalFetch;
    globalThis.window.location = originalLocation;
  }
});

test("CM-override wordt pas actief na bevestiging door de controller", async () => {
  resetSettingsState({
    controlModeOverride: {
      value: "Auto",
      state: "Auto",
      option: ["Auto", "Force CM0", "Force CM1", "Force CM98"],
    },
  });
  const requests = [];
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async (url, options = {}) => {
    requests.push({ url: String(url), method: options.method || "GET" });
    if (options.method === "POST") {
      return {
        ok: true,
        json: async () => ({ value: "Force CM1", state: "Force CM1" }),
      };
    }
    return {
      ok: true,
      json: async () => ({ value: "Force CM1", state: "Force CM1" }),
    };
  };

  try {
    await commitSelect("controlModeOverride", "Force CM1");
    assert.equal(state.entities.controlModeOverride.value, "Force CM1");
    assert.doesNotMatch(state.controlError, /CM Override kon niet worden bijgewerkt/);
    assert.ok(requests.length >= 2);
    assert.equal(requests[0].method, "POST");
    assert.equal(requests[1].method, "GET");
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test("onbevestigde terugkeer naar Auto houdt de vorige override zichtbaar", async () => {
  resetSettingsState({
    controlModeOverride: {
      value: "Force CM1",
      state: "Force CM1",
      option: ["Auto", "Force CM0", "Force CM1", "Force CM98"],
    },
  });
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async (_url, options = {}) => options.method === "POST"
    ? { ok: true }
    : { ok: false, status: 503 };

  try {
    await commitSelect("controlModeOverride", "Auto");
    assert.equal(state.entities.controlModeOverride.value, "Force CM1");
    assert.match(state.controlError, /controllerstatus kon niet worden bevestigd/);
    assert.equal(state.controlNotice, "");
    assert.match(renderControlModeOverrideBanner(), /role="alert"/);
    assert.match(renderControlModeOverrideBanner(), /controllerstatus kon niet worden bevestigd/);
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test("resetfout blijft zichtbaar in de energieteller-popup", () => {
  resetSettingsState({
    resetCumulativeEnergyCounters: { value: "", state: "" },
  });
  state.systemModal = "energy-counter-reset-confirm";
  state.controlError = "Energietellers resetten mislukt. HTTP 500";

  const markup = renderSystemModal();
  assert.match(markup, /role="alert"/);
  assert.match(markup, /Energietellers resetten mislukt\. HTTP 500/);
});
