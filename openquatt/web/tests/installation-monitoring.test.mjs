import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  localStorage: {
    getItem: () => null,
  },
};

const { state } = await import("../js/src/core/state.js");
const { getInstallationMonitoringModel } = await import("../js/src/core/installation-monitoring.js");
const { PUMP_IPWM_PROFILE_KEYS, SETTINGS_BACKUP_SECTIONS, SETTINGS_KEYS } = await import("../js/src/core/config.js");

test("actieve PT1000-leesfout verschijnt in de installatiebewaking", () => {
  state.entities = {
    pt1000ReadProblem: { value: true, state: "ON" },
  };

  const monitoring = getInstallationMonitoringModel();

  assert.equal(monitoring.active, true);
  assert.deepEqual(monitoring.problems, [{
    key: "pt1000ReadProblem",
    label: "PT1000-aanvoersensor geeft geen geldige meting",
  }]);
});

test("actieve aanvoertemperatuurfallback verschijnt in de installatiebewaking", () => {
  state.entities = {
    waterSupplyTempFallbackActive: { value: true, state: "ON" },
  };

  const monitoring = getInstallationMonitoringModel();

  assert.equal(monitoring.active, true);
  assert.deepEqual(monitoring.problems, [{
    key: "waterSupplyTempFallbackActive",
    label: "Aanvoertemperatuur gebruikt de warmtepompuitlaat als fallback",
  }]);
});

test("ongeldige OTT-status geeft geen melding wanneer alleen de kamertemperatuur via OTT komt", () => {
  state.entities = {
    otEnabled: { value: true, state: "ON" },
    otThermostatStatusValid: { value: false, state: "OFF" },
    otLinkProblem: { value: false, state: "OFF" },
    roomTempSource: { value: "OT thermostat", state: "OT thermostat" },
  };

  const monitoring = getInstallationMonitoringModel();

  assert.equal(monitoring.active, false);
  assert.deepEqual(monitoring.problems, []);
});

test("ongeldige OTT-status geeft geen melding wanneer alleen het kamer-setpoint via OTT komt", () => {
  state.entities = {
    otEnabled: { value: true, state: "ON" },
    otThermostatStatusValid: { value: false, state: "OFF" },
    otLinkProblem: { value: false, state: "OFF" },
    roomSetpointSource: { value: "OT thermostat", state: "OT thermostat" },
  };

  const monitoring = getInstallationMonitoringModel();

  assert.equal(monitoring.active, false);
  assert.deepEqual(monitoring.problems, []);
});

test("ongeldige OTT-status geeft geen melding wanneer beide kamerwaarden via OTT komen", () => {
  state.entities = {
    otEnabled: { value: true, state: "ON" },
    otThermostatStatusValid: { value: false, state: "OFF" },
    otLinkProblem: { value: false, state: "OFF" },
    roomTempSource: { value: "OT thermostat", state: "OT thermostat" },
    roomSetpointSource: { value: "OT thermostat", state: "OT thermostat" },
  };

  const monitoring = getInstallationMonitoringModel();

  assert.equal(monitoring.active, false);
  assert.deepEqual(monitoring.problems, []);
});

test("ongeldige OTT-status meldt ontbrekende warmtetoestemming", () => {
  state.entities = {
    otEnabled: { value: true, state: "ON" },
    otThermostatStatusValid: { value: false, state: "OFF" },
    otLinkProblem: { value: false, state: "OFF" },
    heatingEnableSource: { value: "OT thermostat", state: "OT thermostat" },
  };

  const monitoring = getInstallationMonitoringModel();

  assert.equal(monitoring.active, true);
  assert.deepEqual(monitoring.problems, [{
    key: "otThermostatStatusInvalid",
    label: "Geen actuele warmtetoestemming van OpenTherm-thermostaat",
  }]);
});

test("ongeldige OTT-status meldt ontbrekende koeltoestemming", () => {
  state.entities = {
    otEnabled: { value: true, state: "ON" },
    otThermostatStatusValid: { value: false, state: "OFF" },
    otLinkProblem: { value: false, state: "OFF" },
    coolingEnableSource: { value: "OT thermostat", state: "OT thermostat" },
  };

  const monitoring = getInstallationMonitoringModel();

  assert.equal(monitoring.active, true);
  assert.deepEqual(monitoring.problems, [{
    key: "otThermostatStatusInvalid",
    label: "Geen actuele koeltoestemming van OpenTherm-thermostaat",
  }]);
});

test("ongeldige OTT-status combineert ontbrekende verwarmings- en koeltoestemming", () => {
  state.entities = {
    otEnabled: { value: true, state: "ON" },
    otThermostatStatusValid: { value: false, state: "OFF" },
    otLinkProblem: { value: false, state: "OFF" },
    heatingEnableSource: { value: "OT thermostat", state: "OT thermostat" },
    coolingEnableSource: { value: "OT thermostat", state: "OT thermostat" },
  };

  const monitoring = getInstallationMonitoringModel();

  assert.equal(monitoring.active, true);
  assert.deepEqual(monitoring.problems, [{
    key: "otThermostatStatusInvalid",
    label: "Geen actuele verwarmings- en koeltoestemming van OpenTherm-thermostaat",
  }]);
});

test("actuele OTT-status geeft geen melding voor toestemming via OTT", () => {
  state.entities = {
    otEnabled: { value: true, state: "ON" },
    otThermostatStatusValid: { value: true, state: "ON" },
    otLinkProblem: { value: false, state: "OFF" },
    heatingEnableSource: { value: "OT thermostat", state: "OT thermostat" },
    coolingEnableSource: { value: "OT thermostat", state: "OT thermostat" },
  };

  const monitoring = getInstallationMonitoringModel();

  assert.equal(monitoring.active, false);
  assert.deepEqual(monitoring.problems, []);
});

test("ongeldige OTT-status geeft geen melding wanneer OTT niet als bron is geselecteerd", () => {
  state.entities = {
    otEnabled: { value: true, state: "ON" },
    otThermostatStatusValid: { value: false, state: "OFF" },
    otLinkProblem: { value: false, state: "OFF" },
    roomTempSource: { value: "HA input", state: "HA input" },
    roomSetpointSource: { value: "MQTT", state: "MQTT" },
    heatingEnableSource: { value: "Disabled", state: "Disabled" },
    coolingEnableSource: { value: "Disabled", state: "Disabled" },
  };

  const monitoring = getInstallationMonitoringModel();

  assert.equal(monitoring.active, false);
  assert.deepEqual(monitoring.problems, []);
});

test("OTT-linkprobleem blijft zichtbaar wanneer alleen de kamerwaarden via OTT komen", () => {
  state.entities = {
    otEnabled: { value: true, state: "ON" },
    otThermostatStatusValid: { value: false, state: "OFF" },
    otLinkProblem: { value: true, state: "ON" },
    roomTempSource: { value: "OT thermostat", state: "OT thermostat" },
    roomSetpointSource: { value: "OT thermostat", state: "OT thermostat" },
  };

  const monitoring = getInstallationMonitoringModel();

  assert.equal(monitoring.active, true);
  assert.deepEqual(monitoring.problems, [{
    key: "otLinkProblem",
    label: "OpenTherm-verbinding meldt een probleem",
  }]);
});

test("OTT-linkprobleem heeft voorrang op een ongeldige toestemmingsstatus", () => {
  state.entities = {
    otEnabled: { value: true, state: "ON" },
    otThermostatStatusValid: { value: false, state: "OFF" },
    otLinkProblem: { value: true, state: "ON" },
    heatingEnableSource: { value: "OT thermostat", state: "OT thermostat" },
    coolingEnableSource: { value: "OT thermostat", state: "OT thermostat" },
  };

  const monitoring = getInstallationMonitoringModel();

  assert.equal(monitoring.active, true);
  assert.deepEqual(monitoring.problems, [{
    key: "otLinkProblem",
    label: "OpenTherm-verbinding meldt een probleem",
  }]);
});

test("een verouderd incidentsnapshot wordt niet als actuele warmtepompstatus getoond", () => {
  state.entities = {};
  state.incidentMonitoringSnapshot = {
    valid: true,
    system: {
      controlMode: 2,
      action: "none",
    },
    heatPumps: [{
      index: 1,
      linkState: "healthy",
      runState: "stop_unconfirmed",
      availability: "available",
      incidents: [],
    }],
    incidents: [],
  };
  state.incidentMonitoringError = "Incident monitoring HTTP 401";

  const monitoring = getInstallationMonitoringModel();

  assert.equal(monitoring.active, true);
  assert.equal(monitoring.incidentMonitoringStale, true);
  assert.equal(monitoring.incidentMonitoring, undefined);
  assert.equal(monitoring.title, "Warmtepompstatus wordt vernieuwd");
  assert.match(monitoring.copy, /Oude incidentgegevens worden niet als actueel getoond/);
  assert.deepEqual(monitoring.problems, [{
    key: "incident-monitoring-stale",
    label: "Warmtepompstatus wordt opnieuw opgehaald",
    severity: "attention",
    source: "incident_manager",
  }]);

  state.incidentMonitoringSnapshot = null;
  state.incidentMonitoringError = "";
});

test("pomp-iPWM-profielen zijn per HP gesynchroniseerd en worden meegenomen in backup", () => {
  assert.deepEqual(PUMP_IPWM_PROFILE_KEYS, ["hp1PumpIpwmProfile", "hp2PumpIpwmProfile"]);
  assert.ok(PUMP_IPWM_PROFILE_KEYS.every((key) => SETTINGS_KEYS.includes(key)));
  const installationKeys = SETTINGS_BACKUP_SECTIONS.find(({ id }) => id === "installation").keys;
  assert.ok(PUMP_IPWM_PROFILE_KEYS.every((key) => installationKeys.includes(key)));
});
