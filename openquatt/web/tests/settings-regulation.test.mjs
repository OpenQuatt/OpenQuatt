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
const { commitSelect, disableRange, getNumberSettingValidationError } = await import("../js/src/core/entity-write-actions.js");
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
const entityActionsSource = await readFile(new URL("../js/src/core/entity-actions.js", import.meta.url), "utf8");

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
    hp1ExcludeMinHz: numberEntity(55, "Hz"),
    hp1ExcludeMaxHz: numberEntity(61, "Hz"),
  };

  assert.equal(
    getNumberSettingValidationError("hp1ExcludeMinHz", 62, entities),
    "De ondergrens mag niet hoger zijn dan de bovengrens (61 Hz).",
  );
  assert.equal(
    getNumberSettingValidationError("hp1ExcludeMaxHz", 54, entities),
    "De bovengrens mag niet lager zijn dan de ondergrens (55 Hz).",
  );
  assert.equal(getNumberSettingValidationError("hp1ExcludeMinHz", 0, entities), "");
  assert.equal(getNumberSettingValidationError("hp1ExcludeMaxHz", 55, entities), "");
});

test("nieuwe installatie- en service-entiteiten worden bij het juiste scherm geladen", () => {
  assert.ok(SETTINGS_GROUP_KEY_MAP.installation.includes("electricalCurrentLimit"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.installation.includes("boilerSupportStartThreshold"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.installation.includes("boilerSupportStopThreshold"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.installation.includes("hp1ExcludeMinHz"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.installation.includes("hp2ExcludeMaxHz"));
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
  assert.match(markup, /Maximale gezamenlijke netstroom/);
  assert.match(markup, /max="16"/);
  assert.match(markup, /Standaard voor deze installatie/);
  assert.match(markup, /16 A · Single/);
  assert.match(markup, /Indicatief vermogen/);
  assert.match(markup, /oq-settings-electrical-estimate/);
  assert.match(markup, /circa 3,7 kW/);
  assert.match(markup, /gezamenlijke elektrische belasting van de buitenunits/);
  assert.match(markup, /Stooklijnbedrijf en koelen gebruiken alleen de gemeten feedback/);
  assert.match(markup, /softwarematige regelgrens en geen elektrische beveiliging/);
  assert.match(markup, /Korte stroompieken boven de ingestelde waarde zijn niet volledig uit te sluiten/);

  resetSettingsState({
    installationTopology: { value: "duo", state: "duo" },
    hpGeneration: { value: "V2", state: "V2" },
    hp1Generation: { value: "V2", state: "V2" },
    hp2Generation: { value: "V2", state: "V2" },
    electricalCurrentLimit: limit,
  });
  markup = renderSettingsElectricalCurrentLimitSection();
  assert.match(markup, /max="26"/);
  assert.match(markup, /20 A · Duo V2/);
  assert.match(markup, /circa 4,6 kW/);

  resetSettingsState({
    installationTopology: { value: "duo", state: "duo" },
    hpGeneration: { value: "V1", state: "V1" },
    hp1Generation: { value: "V1", state: "V1" },
    hp2Generation: { value: "V1.5", state: "V1.5" },
    electricalCurrentLimit: limit,
  });
  markup = renderSettingsElectricalCurrentLimitSection();
  assert.match(markup, /max="20"/);
  assert.match(markup, /16 A · Duo V1\/V1\.5/);

  resetSettingsState({
    installationTopology: { value: "duo", state: "duo" },
    hpGeneration: { value: "V2", state: "V2" },
    electricalCurrentLimit: { ...limit, value: 5, state: "5" },
  });
  markup = renderSettingsElectricalCurrentLimitSection();
  assert.match(markup, /value="10"/);
});

test("elektrische ingangsgrens waarschuwt boven en relativeert onder de standaard", async () => {
  const { getElectricalLimitTopologyInfo } = await import("../js/src/settings/electrical-limit.js");
  const limit = numberEntity(16, "A", { min_value: 10, max_value: 20, step: 0.5 });

  resetSettingsState({
    installationTopology: { value: "duo", state: "duo" },
    hpGeneration: { value: "V1", state: "V1" },
    hp1Generation: { value: "V1", state: "V1" },
    hp2Generation: { value: "V1.5", state: "V1.5" },
    electricalCurrentLimit: limit,
  });
  let info = getElectricalLimitTopologyInfo();
  assert.equal(info.standardA, 16);
  assert.equal(info.absoluteMaxA, 20);

  // Direct onder de standaard: alleen neutrale melding, geen waarschuwing.
  state.inputDrafts.electricalCurrentLimit = "15.5";
  state.drafts.electricalCurrentLimit = 15.5;
  let markup = renderSettingsElectricalCurrentLimitSection();
  assert.match(markup, /Een lagere waarde kan het maximale verwarmings- en koelvermogen beperken/);
  assert.doesNotMatch(markup, /Hogere waarde dan de standaard elektrische aansluiting/);
  assert.match(markup, /Standaardwaarde herstellen/);

  // Op de standaard: geen waarschuwing en geen vermogensmelding.
  state.inputDrafts.electricalCurrentLimit = "16";
  state.drafts.electricalCurrentLimit = 16;
  markup = renderSettingsElectricalCurrentLimitSection();
  assert.doesNotMatch(markup, /Hogere waarde dan de standaard elektrische aansluiting/);
  assert.doesNotMatch(markup, /Een lagere waarde kan het maximale verwarmings-/);

  // Direct boven de standaard: inline waarschuwing met zwaardere-groep-eis.
  state.inputDrafts.electricalCurrentLimit = "16.5";
  state.drafts.electricalCurrentLimit = 16.5;
  markup = renderSettingsElectricalCurrentLimitSection();
  assert.match(markup, /Hogere waarde dan de standaard elektrische aansluiting/);
  assert.match(markup, /Alleen de installatieautomaat vervangen door een zwaarder exemplaar is niet voldoende/);

  // Duo V2 waarschuwt pas boven 20 A.
  resetSettingsState({
    installationTopology: { value: "duo", state: "duo" },
    hpGeneration: { value: "V2", state: "V2" },
    hp1Generation: { value: "V2", state: "V2" },
    hp2Generation: { value: "V2", state: "V2" },
    electricalCurrentLimit: limit,
  });
  info = getElectricalLimitTopologyInfo();
  assert.equal(info.standardA, 20);
  assert.equal(info.absoluteMaxA, 26);
  state.inputDrafts.electricalCurrentLimit = "20";
  state.drafts.electricalCurrentLimit = 20;
  assert.doesNotMatch(renderSettingsElectricalCurrentLimitSection(), /Hogere waarde dan de standaard/);

  // Duo V2 tot 26 A: boven 20 A volgt dezelfde waarschuwing en bevestiging.
  state.inputDrafts.electricalCurrentLimit = "26";
  state.drafts.electricalCurrentLimit = 26;
  markup = renderSettingsElectricalCurrentLimitSection();
  assert.match(markup, /max="26"/);
  assert.match(markup, /Hogere waarde dan de standaard elektrische aansluiting/);
  assert.match(markup, /boven de standaard 20 A voor een Duo V2/);

  // Boven het absolute maximum wordt afgekapt op 26 A.
  state.inputDrafts.electricalCurrentLimit = "30";
  state.drafts.electricalCurrentLimit = 30;
  assert.match(renderSettingsElectricalCurrentLimitSection(), /value="26"/);
});

test("elektrische ingangsgrens geeft geen verhoging vrij zonder bevestigde ODU-detectie", async () => {
  const { getElectricalLimitTopologyInfo } = await import("../js/src/settings/electrical-limit.js");
  const limit = numberEntity(16, "A", { min_value: 10, max_value: 26, step: 0.5 });

  // Duo met geconfigureerde V1.5 maar mislukte detectie: geen 20 A.
  resetSettingsState({
    installationTopology: { value: "duo", state: "duo" },
    hpGeneration: { value: "V1.5", state: "V1.5" },
    hp1Generation: { value: "Unknown", state: "Unknown" },
    hp2Generation: { value: "Unknown", state: "Unknown" },
    electricalCurrentLimit: limit,
  });
  let info = getElectricalLimitTopologyInfo();
  assert.equal(info.standardA, 16);
  assert.equal(info.absoluteMaxA, 16);
  assert.match(renderSettingsElectricalCurrentLimitSection(), /max="16"/);

  // Eén onbekende buitenunit blokkeert de verhoging eveneens.
  resetSettingsState({
    installationTopology: { value: "duo", state: "duo" },
    hpGeneration: { value: "V2", state: "V2" },
    hp1Generation: { value: "V2", state: "V2" },
    hp2Generation: { value: "Unknown", state: "Unknown" },
    electricalCurrentLimit: limit,
  });
  info = getElectricalLimitTopologyInfo();
  assert.equal(info.standardA, 20);
  assert.equal(info.absoluteMaxA, 20);

  // Familiemismatch (geconfigureerd V2, gedetecteerd V1) blijft op de standaard.
  resetSettingsState({
    installationTopology: { value: "duo", state: "duo" },
    hpGeneration: { value: "V2", state: "V2" },
    hp1Generation: { value: "V1", state: "V1" },
    hp2Generation: { value: "V1.5", state: "V1.5" },
    electricalCurrentLimit: limit,
  });
  info = getElectricalLimitTopologyInfo();
  assert.equal(info.absoluteMaxA, 20);
});

test("elektrische ingangsgrens geeft geen verhoging vrij bij onbekende generatie", async () => {
  const { getElectricalLimitTopologyInfo } = await import("../js/src/settings/electrical-limit.js");
  const limit = numberEntity(16, "A", { min_value: 10, max_value: 20, step: 0.5 });
  resetSettingsState({
    installationTopology: { value: "duo", state: "duo" },
    hpGeneration: { value: "", state: "" },
    electricalCurrentLimit: limit,
  });
  const info = getElectricalLimitTopologyInfo();
  assert.equal(info.standardA, 16);
  assert.equal(info.absoluteMaxA, 16);
  const markup = renderSettingsElectricalCurrentLimitSection();
  assert.match(markup, /max="16"/);
  assert.match(markup, /Standaard voor deze installatie/);

  resetSettingsState({
    installationTopology: { value: "single", state: "single" },
    hpGeneration: { value: "V1", state: "V1" },
    electricalCurrentLimit: limit,
  });
  const singleInfo = getElectricalLimitTopologyInfo();
  assert.equal(singleInfo.standardA, 16);
  assert.equal(singleInfo.absoluteMaxA, 16);
});

test("elektrische ingangsgrens leest de bevestigde waarde los van open drafts", async () => {
  const { getCommittedElectricalLimitRaw, getElectricalLimitChangePlan } = await import("../js/src/settings/electrical-limit.js");
  const limit = numberEntity(16, "A", { min_value: 10, max_value: 20, step: 0.5 });
  resetSettingsState({
    installationTopology: { value: "duo", state: "duo" },
    hpGeneration: { value: "V1", state: "V1" },
    hp1Generation: { value: "V1", state: "V1" },
    hp2Generation: { value: "V1.5", state: "V1.5" },
    electricalCurrentLimit: limit,
  });

  // Simuleer typen: de draft staat al op 17 terwijl 16 bevestigd is.
  state.inputDrafts.electricalCurrentLimit = "17";
  state.drafts.electricalCurrentLimit = 17;
  assert.equal(getCommittedElectricalLimitRaw(), 16);
  const plan = getElectricalLimitChangePlan("17", getCommittedElectricalLimitRaw(), 10);
  assert.equal(plan.requiresConfirmation, true);
  assert.equal(plan.fromA, 16);
  assert.equal(plan.toA, 17);
});

test("elektrische stroomaanduiding toont hele ampères zonder decimalen", async () => {
  const { formatDutchAmps } = await import("../js/src/settings/electrical-limit.js");
  assert.equal(formatDutchAmps(16), "16 A");
  assert.equal(formatDutchAmps(20), "20 A");
  assert.equal(formatDutchAmps(16.5), "16,5 A");
  assert.equal(formatDutchAmps(Number.NaN), "—");
});

test("elektrische ingangsgrens vereist bevestiging boven de standaard en annuleren herstelt", async () => {
  const { getElectricalLimitChangePlan } = await import("../js/src/settings/electrical-limit.js");
  const { renderSystemModal } = await import("../js/src/features/header-status.js");
  const { handleSystemAction } = await import("../js/src/features/system-actions.js");
  const limit = numberEntity(16, "A", { min_value: 10, max_value: 20, step: 0.5 });
  resetSettingsState({
    installationTopology: { value: "duo", state: "duo" },
    hpGeneration: { value: "V1", state: "V1" },
    hp1Generation: { value: "V1", state: "V1" },
    hp2Generation: { value: "V1.5", state: "V1.5" },
    electricalCurrentLimit: limit,
  });

  // Puur plan: boven de standaard is bevestiging vereist met oude en nieuwe waarde.
  let plan = getElectricalLimitChangePlan("20", 16, 10);
  assert.equal(plan.valid, true);
  assert.equal(plan.requiresConfirmation, true);
  assert.equal(plan.fromA, 16);
  assert.equal(plan.toA, 20);
  plan = getElectricalLimitChangePlan("16", 16, 10);
  assert.equal(plan.requiresConfirmation, false);
  plan = getElectricalLimitChangePlan("15.5", 16, 10);
  assert.equal(plan.requiresConfirmation, false);

  const originalFetch = globalThis.fetch;
  const posts = [];
  globalThis.fetch = async (url, options = {}) => {
    posts.push(String(url));
    return { ok: true, json: async () => ({ value: 20, state: "20" }) };
  };
  try {
    // Simuleer de bevestigingsstroom zoals requestElectricalLimitChange die opbouwt.
    state.inputDrafts.electricalCurrentLimit = "20";
    state.drafts.electricalCurrentLimit = 20;
    state.pendingElectricalLimit = { fromA: 16, toA: 20, standardA: 16 };
    state.systemModal = "electrical-limit-confirm";
    let modal = renderSystemModal();
    assert.match(modal, /Hogere elektrische ingangsgrens instellen\?/);
    assert.match(modal, /van <strong>16 A<\/strong> naar <strong>20 A<\/strong>/);
    assert.match(modal, /20 A instellen/);
    assert.match(modal, /OpenQuatt vervangt nooit de elektrische beveiliging/);

    // Annuleren sluit de dialoog en zet het veld terug op de bevestigde waarde.
    handleSystemAction("close-system-modal", {});
    assert.equal(state.systemModal, "");
    assert.equal(state.pendingElectricalLimit, null);
    assert.equal(state.inputDrafts.electricalCurrentLimit, undefined);
    const markup = renderSettingsElectricalCurrentLimitSection();
    assert.doesNotMatch(markup, /Hogere waarde dan de standaard elektrische aansluiting/);

    // Bevestigen schrijft de nieuwe waarde weg.
    state.pendingElectricalLimit = { fromA: 16, toA: 20, standardA: 16 };
    state.systemModal = "electrical-limit-confirm";
    await handleSystemAction("confirm-electrical-limit", {});
    assert.equal(state.systemModal, "");
    assert.ok(posts.some((url) => url.includes("value=20")));
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test("elektrische reset drukt de firmware-knop in en valt terug zonder button-entity", async () => {
  const { handleSystemAction } = await import("../js/src/features/system-actions.js");
  const limit = numberEntity(20, "A", { min_value: 10, max_value: 26, step: 0.5 });

  const originalFetch = globalThis.fetch;
  const posts = [];
  globalThis.fetch = async (url, options = {}) => {
    posts.push({ url: String(url), method: options.method || "GET" });
    return { ok: true, json: async () => ({ value: 16, state: "16" }) };
  };
  const waitForPosts = async () => {
    for (let i = 0; i < 100 && posts.length === 0; i++) {
      await new Promise((resolve) => setTimeout(resolve, 10));
    }
  };
  try {
    // Met button-entity: press-actie richting firmware (reset naar NaN).
    resetSettingsState({
      installationTopology: { value: "duo", state: "duo" },
      hpGeneration: { value: "V1", state: "V1" },
      hp1Generation: { value: "V1", state: "V1" },
      hp2Generation: { value: "V1.5", state: "V1.5" },
      electricalCurrentLimit: limit,
      electricalCurrentLimitReset: { value: "", state: "" },
    });
    await handleSystemAction("reset-electrical-limit-to-default", {});
    await waitForPosts();
    assert.ok(posts.some((post) => post.method === "POST" && post.url.includes("Reset%20electrical%20current%20limit")));
    assert.match(String(state.controlNotice || ""), /automatisch/);

    // Zonder button-entity (oude firmware): terugval op expliciet standaard zetten.
    posts.length = 0;
    resetSettingsState({
      installationTopology: { value: "duo", state: "duo" },
      hpGeneration: { value: "V1", state: "V1" },
      hp1Generation: { value: "V1", state: "V1" },
      hp2Generation: { value: "V1.5", state: "V1.5" },
      electricalCurrentLimit: limit,
    });
    await handleSystemAction("reset-electrical-limit-to-default", {});
    await waitForPosts();
    assert.ok(posts.some((post) => post.method === "POST" && post.url.includes("value=16")));
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test("elektrische backup-waarschuwing verschijnt alleen boven de standaard", async () => {
  const { getElectricalLimitBackupRestoreWarning } = await import("../js/src/settings/electrical-limit.js");
  resetSettingsState({
    installationTopology: { value: "duo", state: "duo" },
    hpGeneration: { value: "V1", state: "V1" },
    hp1Generation: { value: "V1", state: "V1" },
    hp2Generation: { value: "V1.5", state: "V1.5" },
    electricalCurrentLimit: numberEntity(16, "A", { min_value: 10, max_value: 26, step: 0.5 }),
  });

  const warning = getElectricalLimitBackupRestoreWarning({ installation: { electricalCurrentLimit: 20 } });
  assert.match(warning, /backup zet de elektrische ingangsgrens op 20 A/);
  assert.match(warning, /boven de standaard 16 A/);
  assert.equal(getElectricalLimitBackupRestoreWarning({ installation: { electricalCurrentLimit: 16 } }), "");
  assert.equal(getElectricalLimitBackupRestoreWarning({ installation: {} }), "");
  assert.equal(getElectricalLimitBackupRestoreWarning(null), "");
});

test("elektrische ingangsgrens staat voor ODU runtime", () => {  const installationStart = settingsCoreSource.indexOf('activeGroup === "installation"');
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
    silentMaxHz: numberEntity(67, "Hz", { min_value: 0, max_value: 120, step: 1 }),
    silentModeOverride: { value: "Schedule", state: "Schedule" },
    silentActive: { value: false, state: "OFF" },
  };

  resetSettingsState(baseEntities);
  let markup = renderSettingsCoolingSection();
  assert.match(markup, /Tijdens stille modus wordt koelen begrensd op een compressorfrequentie van 67 Hz/);

  resetSettingsState({
    ...baseEntities,
    silentActive: { value: true, state: "ON" },
  });
  markup = renderSettingsCoolingSection();
  assert.match(markup, /Stille modus is nu actief\. Koelen wordt begrensd op een compressorfrequentie van 67 Hz/);

  resetSettingsState({
    ...baseEntities,
    silentModeOverride: { value: "Off", state: "Off" },
  });
  assert.doesNotMatch(renderSettingsCoolingSection(), /oq-settings-cooling-limit-warning/);

  resetSettingsState({
    ...baseEntities,
    silentMaxHz: numberEntity(120, "Hz", { min_value: 0, max_value: 120, step: 1 }),
  });
  assert.doesNotMatch(renderSettingsCoolingSection(), /oq-settings-cooling-limit-warning/);

  resetSettingsState({
    coolingDemandMax: baseEntities.coolingDemandMax,
    silentModeOverride: baseEntities.silentModeOverride,
  });
  assert.doesNotMatch(renderSettingsCoolingSection(), /oq-settings-cooling-limit-warning/);
});

test("frequentielimieten gelden voor beide modi", () => {
  resetSettingsState({
    silentStartTime: { value: "22:00", state: "22:00" },
    silentEndTime: { value: "07:00", state: "07:00" },
    silentMaxHz: numberEntity(60, "Hz", { max_value: 120, step: 1 }),
    dayMaxHz: numberEntity(90, "Hz", { max_value: 120, step: 1 }),
  });

  const markup = renderSilentSettingsGrid();
  assert.match(markup, /Maximale compressorfrequentie tijdens stille uren/);
  assert.match(markup, /Maximale compressorfrequentie overdag/);
  assert.doesNotMatch(markup, /Maximaal niveau tijdens stille uren/);
});

test("compressorinstellingen tonen één tweepuntsbereik", () => {
  resetSettingsState({
    hp1ExcludeMinHz: numberEntity(55, "Hz", { max_value: 120, step: 1 }),
    hp1ExcludeMaxHz: numberEntity(61, "Hz", { max_value: 120, step: 1 }),
  });
  const markup = renderSettingsHeatPumpLimiterCard("Warmtepomp 1", "hp1");
  assert.match(markup, /Uitgesloten frequentiebereik/);
  assert.match(markup, /55–61 Hz/);
  assert.match(markup, /data-oq-range-role="min"/);
  assert.match(markup, /data-oq-range-role="max"/);
  assert.match(markup, /data-oq-action="disable-range"/);
  assert.match(markup, />20Hz</);
});

test("frequentiebereik accepteert 0 als uitschakelstand zonder terug te springen", () => {
  assert.match(entityActionsSource, /inputValue > 0 && inputValue < 20/);
  assert.match(entityActionsSource, /inputValue === 0[\s\S]*minInput\.value = maxInput\.value = "0";/);
  assert.match(entityActionsSource, /dataset\.oqRangeRole && Number\(event\.target\.value\) === 0/);
  assert.match(entityActionsSource, /minValue > 0 && maxValue > 0 && minValue > maxValue/);
  assert.match(entityActionsSource, /minValue > 0 && maxValue > 0 && maxValue < minValue/);
  assert.match(settingsCoreSource, /String\(getInputDraftValue\(fieldKey\) \?\? ""\)/);

  resetSettingsState({
    hp1ExcludeMinHz: numberEntity(0, "Hz", { max_value: 120, step: 1 }),
    hp1ExcludeMaxHz: numberEntity(61, "Hz", { max_value: 120, step: 1 }),
  });
  const markup = renderSettingsHeatPumpLimiterCard("Warmtepomp 1", "hp1");
  assert.match(markup, /data-oq-range-role="min"[\s\S]*?value="0"/);
  assert.match(markup, /data-oq-range-role="max"[\s\S]*?value="0"/);
});

test("uitschakelen vereist bevestigde nulwaarden voor beide grenzen", async () => {
  resetSettingsState({
    hp1ExcludeMinHz: numberEntity(55, "Hz", { max_value: 120, step: 1 }),
    hp1ExcludeMaxHz: numberEntity(61, "Hz", { max_value: 120, step: 1 }),
  });
  state.appView = "overview";
  const originalFetch = globalThis.fetch;
  const posts = [];
  globalThis.fetch = async (url, options = {}) => {
    const decodedUrl = decodeURIComponent(String(url));
    if (options.method === "POST" && decodedUrl.includes("/set?")) {
      posts.push(decodedUrl);
      return { ok: !decodedUrl.includes("Excluded frequency minimum"), status: 500 };
    }
    const value = decodedUrl.includes("Excluded frequency maximum") ? 0 : 55;
    return { ok: true, json: async () => ({ value, state: String(value) }) };
  };

  try {
    assert.equal(await disableRange("hp1ExcludeMinHz", "hp1ExcludeMaxHz"), false);
    assert.equal(posts.length, 2);
    assert.match(state.controlError, /HP1 - Excluded frequency minimum kon niet worden bijgewerkt/);
    assert.equal(state.controlNotice, "");
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test("koelscherm licht de stille frequentiegrens toe", () => {
  resetSettingsState({
    coolingDemandMax: numberEntity(10, "", { min_value: 1, max_value: 10, step: 1 }),
    silentMaxHz: numberEntity(46, "Hz", { max_value: 120, step: 1 }),
    silentModeOverride: { value: "Schedule", state: "Schedule" },
    silentActive: { value: true, state: "ON" },
  });
  assert.match(renderSettingsCoolingSection(), /Koelen wordt begrensd op een compressorfrequentie van 46 Hz/);
});

test("koelscherm laadt de stille-moduslimiet en actuele status", () => {
  assert.ok(SETTINGS_GROUP_KEY_MAP.cooling.includes("silentModeOverride"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.cooling.includes("silentActive"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.cooling.includes("silentMaxHz"));
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
