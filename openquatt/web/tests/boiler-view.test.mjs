import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.localStorage = { getItem: () => null };

const { state } = await import("../js/src/core/state.js");
const {
  getBoilerPanelModel,
  getBoilerStatusModel,
  renderBoilerPanel,
} = await import("../js/src/views/heatpump.js");
const {
  BOILER_OPENTHERM_CAPABILITY,
  getBoilerOpenThermCapability,
  getSupportedBoilerConnectionOptions,
} = await import("../js/src/settings/boiler.js");
const { renderSettingsOpenThermCicSection } = await import("../js/src/settings/integrations.js");
const {
  ENTITY_DEFS,
  FAST_OVERVIEW_KEYS,
  INSTALLATION_MONITORING_STATE_KEYS,
  OVERVIEW_KEYS,
} = await import("../js/src/core/config.js");
const { INITIAL_SETTINGS_READY_KEY_MAP, SETTINGS_GROUP_KEY_MAP } = await import("../js/src/core/entity-sync.js");
const heatPumpCss = await readFile(new URL("../css/src/40-heatpump.css", import.meta.url), "utf8");
const boilerOpenThermYaml = await readFile(new URL("../../oq_boiler_opentherm.yaml", import.meta.url), "utf8");
const heatPumpQProfileYaml = await readFile(new URL("../../profiles/heatpump_controller_q.yaml", import.meta.url), "utf8");
const otSlaveYaml = await readFile(new URL("../../oq_ot_slave.yaml", import.meta.url), "utf8");
const commonSubstitutionsYaml = await readFile(new URL("../../oq_substitutions_common.yaml", import.meta.url), "utf8");
const quickStartSource = await readFile(new URL("../js/src/features/quickstart.js", import.meta.url), "utf8");
const quickStartActionsSource = await readFile(new URL("../js/src/features/quickstart-actions.js", import.meta.url), "utf8");
const installationSource = await readFile(new URL("../js/src/settings/installation.js", import.meta.url), "utf8");

function status(overrides = {}) {
  return getBoilerStatusModel({
    opentherm: true,
    linkAvailable: true,
    fault: false,
    dhwActive: false,
    flameOn: false,
    chActive: false,
    commandActive: false,
    commandValid: true,
    requestedPower: 0,
    blockReason: "no boiler heat request",
    ...overrides,
  });
}

test("boiler panel occupies exactly one heat-pump grid column", () => {
  const rule = heatPumpCss.match(/\.oq-overview-boiler\s*\{([^}]*)\}/);
  assert.ok(rule, "expected the boiler panel layout rule");
  assert.match(rule[1], /grid-column:\s*span 1\s*;/);
  assert.doesNotMatch(rule[1], /grid-column:\s*1\s*\/\s*-1\s*;/);
});

test("boiler status follows fault, link, DHW, flame and command priority", () => {
  assert.equal(status({ fault: true, dhwActive: true, flameOn: true }).code, "fault");
  assert.equal(status({ linkAvailable: false }).code, "offline");
  assert.equal(status({ dhwActive: true, flameOn: true }).code, "dhw");
  assert.equal(status({ flameOn: true, chActive: true }).code, "heating");
  assert.equal(status({ chActive: true }).text, "CV actief");
  assert.equal(status({ commandActive: true }).code, "starting");
  assert.equal(status({ commandValid: false, requestedPower: 1400, blockReason: "boiler command stale" }).code, "blocked");
  assert.equal(status().code, "idle");
});

test("OpenTherm boiler model uses actual fresh boiler telemetry and actual flame state", () => {
  const previousEntities = state.entities;
  const previousVisualMode = state.hpVisualMode;
  state.hpVisualMode = "schematic";
  state.entities = {
    boilerCvAssistEnabled: { value: true },
    boilerConnection: { value: "OpenTherm" },
    boilerHeatPower: { value: 1550 },
    boilerActive: { value: true },
    boilerCommandActive: { value: true },
    boilerCommandValid: { value: true },
    boilerCommandRequestedPower: { value: 1800 },
    boilerBlockReason: { value: "" },
    flowSelected: { value: 720 },
    hp1WaterOut: { value: 28.1 },
    supplyTemp: { value: 35.2 },
    otbLinkAvailable: { value: true },
    otbChActive: { value: true },
    otbFlameOn: { value: false },
    otbDhwActive: { value: false },
    otbReturnWaterTemp: { value: 31.4 },
    otbBoilerWaterTemp: { value: 42.8 },
    otbChPressure: { value: 1.6 },
    boilerCommandTargetTemperature: { value: 45 },
    otbRelativeModulation: { value: 37 },
  };

  try {
    const model = getBoilerPanelModel();
    assert.equal(model.transportText, "OpenTherm");
    assert.equal(model.returnTempText, "31.4 °C");
    assert.equal(model.supplyTempText, "42.8 °C");
    assert.equal(model.pressureText, "1.6 bar");
    assert.equal(model.modulationText, "37 %");
    assert.equal(model.active, true);
    assert.equal(model.flameOn, false);
    assert.equal(model.statusText, "CV actief");
    assert.doesNotMatch(model.boardClass, /has-flame/);

    const html = renderBoilerPanel();
    assert.match(html, /OpenTherm/);
    assert.match(html, /1\.6 bar/);
    assert.doesNotMatch(html, /oq-boiler-card has-flame/);
  } finally {
    state.entities = previousEntities;
    state.hpVisualMode = previousVisualMode;
  }
});

test("OpenTherm boiler model suppresses stale telemetry when the boiler link is down", () => {
  const previousEntities = state.entities;
  state.entities = {
    boilerConnection: { value: "OpenTherm" },
    boilerHeatPower: { value: 0 },
    boilerCommandValid: { value: true },
    boilerCommandRequestedPower: { value: 0 },
    flowSelected: { value: 700 },
    otbLinkAvailable: { value: false },
    otbReturnWaterTemp: { value: 31.4 },
    otbBoilerWaterTemp: { value: 42.8 },
    otbChPressure: { value: 1.6 },
    boilerCommandTargetTemperature: { value: 45 },
    otbRelativeModulation: { value: 37 },
  };

  try {
    const model = getBoilerPanelModel();
    assert.equal(model.statusCode, "offline");
    assert.equal(model.returnTempText, "—");
    assert.equal(model.supplyTempText, "—");
    assert.equal(model.pressureText, "—");
    assert.equal(model.targetText, "—");
    assert.equal(model.modulationText, "—");
  } finally {
    state.entities = previousEntities;
  }
});

test("OpenTherm boiler panel distinguishes DHW from CV heat delivery", () => {
  const previousEntities = state.entities;
  const previousVisualMode = state.hpVisualMode;
  state.hpVisualMode = "schematic";
  state.entities = {
    boilerCvAssistEnabled: { value: true },
    boilerConnection: { value: "OpenTherm" },
    boilerHeatPower: { value: 0 },
    boilerCommandActive: { value: false },
    boilerCommandValid: { value: true },
    boilerCommandRequestedPower: { value: 0 },
    boilerBlockReason: { value: "no boiler heat request" },
    flowSelected: { value: 0 },
    otbLinkAvailable: { value: true },
    otbChActive: { value: false },
    otbDhwActive: { value: true },
    otbFlameOn: { value: true },
    otbDhwTemp: { value: 52.3 },
  };

  try {
    const model = getBoilerPanelModel();
    assert.equal(model.statusCode, "dhw");
    assert.equal(model.statusText, "Tapwater");
    assert.equal(model.active, false);
    assert.equal(model.dhwActive, true);
    assert.equal(model.flameOn, true);
    assert.equal(model.heatText, "0 W");
    assert.equal(model.dhwTempText, "52.3 °C");

    const html = renderBoilerPanel();
    assert.match(html, /Ketel verwarmt tapwater/);
    assert.match(html, /Tapwater/);
    assert.match(html, /52\.3 °C/);
  } finally {
    state.entities = previousEntities;
    state.hpVisualMode = previousVisualMode;
  }
});

test("OpenTherm boiler panel prioritizes fault flags and keeps diagnostics separate", () => {
  const previousEntities = state.entities;
  state.entities = {
    boilerCvAssistEnabled: { value: true },
    boilerConnection: { value: "OpenTherm" },
    boilerHeatPower: { value: 900 },
    boilerCommandValid: { value: true },
    boilerCommandRequestedPower: { value: 1200 },
    flowSelected: { value: 600 },
    otbLinkAvailable: { value: true },
    otbChActive: { value: true },
    otbFlameOn: { value: true },
    otbLowWaterPressure: { value: true },
    otbDiagnosticIndication: { value: true },
  };

  try {
    const faultModel = getBoilerPanelModel();
    assert.equal(faultModel.statusCode, "fault");
    assert.equal(faultModel.statusText, "Storing");
    assert.equal(faultModel.fault, true);
    assert.equal(faultModel.diagnostic, true);
    assert.equal(faultModel.statusDetail, "");
    assert.match(faultModel.boardClass, /is-fault/);

    state.entities.otbLowWaterPressure.value = false;
    const diagnosticModel = getBoilerPanelModel();
    assert.equal(diagnosticModel.statusCode, "heating");
    assert.equal(diagnosticModel.fault, false);
    assert.equal(diagnosticModel.statusDetail, "Diagnostische melding beschikbaar");
  } finally {
    state.entities = previousEntities;
  }
});

test("OpenTherm boiler panel suppresses only an unavailable individual field", () => {
  const previousEntities = state.entities;
  state.entities = {
    boilerConnection: { value: "OpenTherm" },
    boilerHeatPower: { value: 0 },
    boilerCommandValid: { value: true },
    boilerCommandRequestedPower: { value: 0 },
    flowSelected: { value: 700 },
    otbLinkAvailable: { value: true },
    otbReturnWaterTemp: { value: 31.4 },
    otbBoilerWaterTemp: { value: null, state: "NA" },
    otbChPressure: { value: 1.6 },
    boilerCommandTargetTemperature: { value: 45 },
    otbRelativeModulation: { value: 37 },
  };

  try {
    const model = getBoilerPanelModel();
    assert.equal(model.statusCode, "idle");
    assert.equal(model.returnTempText, "31.4 °C");
    assert.equal(model.supplyTempText, "—");
    assert.equal(model.pressureText, "1.6 bar");
    assert.equal(model.targetText, "45.0 °C");
    assert.equal(model.modulationText, "37 %");
  } finally {
    state.entities = previousEntities;
  }
});

test("overview hydration includes requested boiler power", () => {
  assert.ok(OVERVIEW_KEYS.includes("boilerCommandRequestedPower"));
  assert.ok(FAST_OVERVIEW_KEYS.includes("boilerCommandRequestedPower"));
});

test("R1 boiler view keeps OpenTherm-only telemetry out of the panel", () => {
  const previousEntities = state.entities;
  const previousVisualMode = state.hpVisualMode;
  state.hpVisualMode = "schematic";
  state.entities = {
    boilerCvAssistEnabled: { value: true },
    boilerConnection: { value: "R1" },
    boilerHeatPower: { value: 1200 },
    boilerActive: { value: true },
    flowSelected: { value: 680 },
    hp1WaterOut: { value: 29.3 },
    supplyTemp: { value: 34.7 },
  };

  try {
    const model = getBoilerPanelModel();
    assert.equal(model.transportText, "Aan/uit R1");
    assert.equal(model.statusText, "CV actief");
    assert.equal(model.flameOn, false);
    assert.doesNotMatch(renderBoilerPanel(), /OpenTherm ketelwaarden/);
  } finally {
    state.entities = previousEntities;
    state.hpVisualMode = previousVisualMode;
  }
});

test("R1 diagnostics shows selection state without stale OpenTherm details", () => {
  const previousEntities = state.entities;
  state.entities = {
    boilerConnection: { value: "R1" },
    boilerCommandValid: { value: true },
    otbLinkAvailable: { value: false },
    otbFlameOn: { value: true },
    otbChPressure: { value: 1.6, uom: "bar" },
    otbLastResponseAge: { value: 4.2, uom: "s" },
  };

  try {
    const html = renderSettingsOpenThermCicSection();
    assert.match(html, /OpenTherm ketel \(OTB\)/);
    assert.match(html, /Niet geselecteerd/);
    assert.doesNotMatch(html, /Waterdruk/);
    assert.doesNotMatch(html, /Laatste response/);
    assert.doesNotMatch(html, />Vlam</);
  } finally {
    state.entities = previousEntities;
  }
});

test("integration diagnostics separates thermostat, boiler control, OTB and CiC", () => {
  const previousEntities = state.entities;
  state.entities = {
    otEnabled: { value: true },
    otLinkProblem: { value: false },
    otThermostatStatusValid: { value: true },
    otRoomTemp: { value: 20.8, uom: "°C" },
    boilerConnection: { value: "OpenTherm" },
    boilerCommandValid: { value: true },
    boilerCommandSource: { value: "Power House" },
    otbLinkAvailable: { value: true },
    otbFlameOn: { value: true },
    otbChPressure: { value: 1.6, uom: "bar" },
    cicPollingEnabled: { value: true },
    cicJsonFeedOk: { value: true },
  };

  try {
    const html = renderSettingsOpenThermCicSection();
    assert.match(html, /OpenTherm thermostaat \(OTT\)/);
    assert.match(html, /Statusbericht \(ID 0\) actueel/);
    assert.match(html, /Ketelregeling/);
    assert.match(html, /OpenTherm ketel \(OTB\)/);
    assert.match(html, /CiC-feed/);
    assert.match(html, /Waterdruk/);
    assert.match(html, /De aansluiting van de cv-ketel/);
    assert.match(html, /Instellingen → Installatie/);
    assert.doesNotMatch(html, /thermostaatbus, ketelaansturing/);
  } finally {
    state.entities = previousEntities;
  }
});

test("settings hydration loads boiler setup and diagnostics before rendering", () => {
  assert.ok(FAST_OVERVIEW_KEYS.includes("auxHeatSourcePresent"));
  assert.ok(FAST_OVERVIEW_KEYS.includes("boilerCvAssistEnabled"));
  assert.ok(FAST_OVERVIEW_KEYS.includes("boilerRatedHeatPower"));
  assert.ok(FAST_OVERVIEW_KEYS.includes("boilerConnection"));
  assert.ok(FAST_OVERVIEW_KEYS.includes("boilerFaultFallbackEnabled"));
  assert.ok(INITIAL_SETTINGS_READY_KEY_MAP.installation.includes("boilerConnection"));
  assert.ok(INITIAL_SETTINGS_READY_KEY_MAP.installation.includes("auxHeatSourcePresent"));
  assert.ok(INITIAL_SETTINGS_READY_KEY_MAP.installation.includes("boilerRatedHeatPower"));
  assert.ok(INITIAL_SETTINGS_READY_KEY_MAP.installation.includes("boilerFaultFallbackEnabled"));
  assert.ok(INITIAL_SETTINGS_READY_KEY_MAP.installation.includes("otbLinkAvailable"));
  assert.ok(INITIAL_SETTINGS_READY_KEY_MAP.installation.includes("otbConnectionAutoSelected"));
  assert.ok(INITIAL_SETTINGS_READY_KEY_MAP.installation.includes("otbConnectionMismatch"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.installation.includes("boilerConnection"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.installation.includes("otbLinkAvailable"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.installation.includes("otbConnectionAutoSelected"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.installation.includes("otbConnectionMismatch"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.integrations.includes("boilerCommandValid"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.integrations.includes("otbChPressure"));
  assert.ok(INITIAL_SETTINGS_READY_KEY_MAP.service.includes("boilerFaultFallbackEnabled"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.service.includes("boilerFaultFallbackEnabled"));
  assert.equal(
    ENTITY_DEFS.acknowledgeHpIncidents?.name,
    "Acknowledge recovered HP incidents",
  );
  assert.ok(INSTALLATION_MONITORING_STATE_KEYS.includes("acknowledgeHpIncidents"));
  assert.match(
    quickStartActionsSource,
    /if \(stepId === "boiler"\)[\s\S]*?"boilerFaultFallbackEnabled"/,
  );
  assert.match(
    quickStartActionsSource,
    /if \(stepId === "confirm"\)[\s\S]*?"boilerFaultFallbackEnabled"/,
  );
});

test("auxiliary heat source setting remains editable on legacy firmware", () => {
  assert.match(
    installationSource,
    /const sourcePresenceKey = separateSourcePolicyAvailable[\s\S]*?"auxHeatSourcePresent"[\s\S]*?: "boilerCvAssistEnabled"/,
  );
  assert.match(
    installationSource,
    /renderSettingsCompactSwitchControl\(sourcePresenceKey, "Warmtebron aangesloten"/,
  );
  assert.match(
    installationSource,
    /sourcePresent && separateSourcePolicyAvailable && assistSettingAvailable/,
  );
});

test("fallback heating setting explains its guarded scope", () => {
  assert.match(installationSource, /Overnemen wanneer de warmtepomp niet beschikbaar is/);
  assert.match(installationSource, /wanneer geen warmtepomp veilig beschikbaar is/);
  assert.match(installationSource, /koude opstart onder 5 °C/);
  assert.match(installationSource, /koude opstart van 5 tot 12 °C/);
  assert.match(commonSubstitutionsYaml, /oq_hp_cold_start_min_c: "5\.0"/);
  assert.match(commonSubstitutionsYaml, /oq_hp_cold_start_assist_release_c: "12\.0"/);
  assert.match(installationSource, /na een veilige stop/);
  assert.match(installationSource, /Een korte communicatiedip telt niet als uitval/);
});

test("auxiliary heat source copy names common examples and explains hybrid heating", () => {
  assert.match(installationSource, /cv-ketel, elektrische cv-ketel \(e-cv\) of doorstroomverwarmer/);
  assert.match(installationSource, /Hybride verwarmen bij vermogenstekort/);
  assert.match(installationSource, /het beschikbare warmtepompvermogen niet genoeg is/);
});

test("fault fallback is editable in Installation and the shared Quick Start boiler fields", () => {
  const fallbackSwitchMatches = installationSource.match(
    /renderSettingsCompactSwitchControl\(\s*"boilerFaultFallbackEnabled"/g,
  ) || [];
  const servicePanelSource = installationSource.match(
    /function renderInstallationMonitoringSystemPanel[\s\S]+?\n  export function getInstallationMonitoringCount/,
  )?.[0] || "";

  assert.match(
    installationSource,
    /renderBoilerCvFields\("oq-settings-grid oq-settings-boiler-simple-grid", true\)/,
  );
  assert.match(
    quickStartSource,
    /renderBoilerCvFields\("oq-settings-grid oq-settings-grid--quickstart oq-settings-boiler-simple-grid", true\)/,
  );
  assert.match(
    quickStartSource,
    /\["Overnemen wanneer de warmtepomp niet beschikbaar is", isEntityActive\("boilerFaultFallbackEnabled"\) \? "Aan" : "Uit"\]/,
  );
  assert.match(servicePanelSource, /renderInstallationMonitoringStatusRow/);
  assert.doesNotMatch(servicePanelSource, /boilerFaultFallbackEnabled/);
  assert.doesNotMatch(servicePanelSource, /renderSettingsCompactSwitchControl/);
  assert.equal(fallbackSwitchMatches.length, 1);
});

test("Quick Start blocks R1 after a boiler answers the safe OpenTherm probe", () => {
  assert.match(installationSource, /OpenTherm-ketel gevonden/);
  assert.match(installationSource, /Kies OpenTherm \(OTB\)/);
  assert.match(quickStartSource, /nextDisabled:\s*boilerConnectionMismatch/);
});

test("onboarding auto-selects a detected OpenTherm boiler and explains the choice", () => {
  assert.match(
    boilerOpenThermYaml,
    /should_auto_select_opentherm\(\s*result,\s*id\(oq_setup_complete\)\)/,
  );
  assert.match(boilerOpenThermYaml, /call\.set_option\("OpenTherm"\)/);
  assert.match(installationSource, /boilerConnection === "OpenTherm"/);
  assert.match(installationSource, /isEntityActive\("otbConnectionAutoSelected"\)/);
  assert.match(installationSource, /OpenTherm-ketel gedetecteerd/);
  assert.match(installationSource, /automatisch als ketelaansluiting geselecteerd/);
  assert.match(
    installationSource,
    /sourcePresent \|\| boilerConnectionMismatch \|\| boilerConnectionAutoSelected/,
  );
});

test("firmware publishes boiler connection mismatch transitions immediately", () => {
  assert.match(
    boilerOpenThermYaml,
    /oq_boiler_connection_mismatch_state\) = true;\s+id\(oq_boiler_connection_mismatch\)\.publish_state\(true\);/,
  );
  assert.match(
    boilerOpenThermYaml,
    /oq_boiler_connection_mismatch_state\) = false;\s+id\(oq_boiler_connection_mismatch\)\.publish_state\(false\);/,
  );
  assert.match(
    heatPumpQProfileYaml,
    /oq_boiler_connection_mismatch_state\) = false;\s+id\(oq_boiler_connection_mismatch\)\.publish_state\(false\);/,
  );
});

test("Quick Start keeps the mismatch remedy visible when the source is disconnected", () => {
  assert.match(
    installationSource,
    /\(sourcePresent \|\| boilerConnectionMismatch \|\| boilerConnectionAutoSelected\) && boilerConnectionAvailable \? renderSettingsFieldCard/,
  );
  assert.match(installationSource, /OpenTherm-ketel gevonden/);
});

test("R1 setup explains its bounded OpenTherm startup check", () => {
  assert.match(installationSource, /OT-controle bij opstart actief/);
});

test("installation keeps OpenTherm selectable when the supported boiler link is offline", () => {
  const capability = getBoilerOpenThermCapability({ linkEntityPresent: true });
  assert.equal(capability, BOILER_OPENTHERM_CAPABILITY.SUPPORTED);
  assert.deepEqual(
    getSupportedBoilerConnectionOptions(["R1", "OpenTherm"], capability),
    ["R1", "OpenTherm"],
  );
});

test("installation does not silently present R1 while OpenTherm capability is unresolved", () => {
  const capability = getBoilerOpenThermCapability();
  assert.equal(capability, BOILER_OPENTHERM_CAPABILITY.UNKNOWN);
  assert.match(installationSource, /Beschikbaarheid controleren/);
  assert.match(installationSource, /aansluitingskeuze is tijdelijk geblokkeerd/);
});

test("installation offers only R1 after OpenTherm capability is confirmed absent", () => {
  const capability = getBoilerOpenThermCapability({ linkEntityConfirmedMissing: true });
  assert.equal(capability, BOILER_OPENTHERM_CAPABILITY.UNSUPPORTED);
  assert.deepEqual(
    getSupportedBoilerConnectionOptions(["R1", "OpenTherm"], capability),
    ["R1"],
  );
});

test("DHW permission stays enabled without a user-facing setting", () => {
  assert.match(boilerOpenThermYaml, /^    dhw_enable: true$/m);
  assert.doesNotMatch(boilerOpenThermYaml, /^    dhw_enable:\n\s+id: oq_otb_dhw_enable$/m);
  assert.doesNotMatch(quickStartSource, /tapwater levert/i);
  assert.equal(Object.hasOwn(ENTITY_DEFS, "boilerProvidesDhw"), false);
  assert.equal(INITIAL_SETTINGS_READY_KEY_MAP.installation.includes("boilerProvidesDhw"), false);
  assert.equal(SETTINGS_GROUP_KEY_MAP.installation.includes("boilerProvidesDhw"), false);
  assert.ok(SETTINGS_GROUP_KEY_MAP.integrations.includes("otbDhwActive"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.integrations.includes("otbDhwPresent"));
});

test("thermostat slave uses real OTB flame state without changing R1 compatibility", () => {
  assert.match(
    otSlaveYaml,
    /const bool slave_flame_on\s*=\s*\n\s*otb_selected \? boiler_flame_on : ch_active;/,
  );
  assert.match(otSlaveYaml, /set_slave_flame_on\(slave_flame_on\);/);
});
