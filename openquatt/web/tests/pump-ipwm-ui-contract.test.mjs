import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const installationSource = await readFile(
  new URL("../js/src/settings/installation.js", import.meta.url),
  "utf8",
);
const settingsCoreSource = await readFile(
  new URL("../js/src/settings/core.js", import.meta.url),
  "utf8",
);
const mockDeviceSource = await readFile(new URL("../js/mock-device.js", import.meta.url), "utf8");
const mockFixturesSource = await readFile(new URL("../js/mock-fixtures.js", import.meta.url), "utf8");
const entitySyncSource = await readFile(new URL("../js/src/core/entity-sync.js", import.meta.url), "utf8");

test("incidentdetail houdt de vriendelijke titel primair en rendert technische pompcontext", () => {
  const start = installationSource.indexOf("export function renderInstallationMonitoringHpIncident");
  const end = installationSource.indexOf("function renderInstallationMonitoringHeatPumpUnit", start);
  const renderer = installationSource.slice(start, end);
  assert.ok(start >= 0 && end > start);
  assert.match(renderer, /getIncidentDisplayLabel\(incident\)/);
  assert.match(renderer, /\["ODU-code", technicalCode\]/);
  assert.match(renderer, /\["ODU-omschrijving", incident\.technicalDescription\]/);
  assert.match(renderer, /getPumpIncidentContextRows\(incident, pumpContext\)/);
  assert.ok(renderer.indexOf("getIncidentDisplayLabel(incident)") < renderer.indexOf("details.map"));
});

test("per-HP iPWM-profiel staat in de installatie-instellingen met fail-closed uitleg", () => {
  const start = installationSource.indexOf("export function renderSettingsPumpIpwmSection");
  const end = installationSource.indexOf("export function renderBoilerCvFields", start);
  const renderer = installationSource.slice(start, end);
  assert.ok(start >= 0 && end > start);
  assert.match(renderer, /hp1PumpIpwmProfile/);
  assert.match(renderer, /hp2PumpIpwmProfile/);
  assert.match(renderer, /Onbekend \/ anders/);
  assert.match(renderer, /niet als pompvermogen meegerekend/);
  assert.match(settingsCoreSource, /renderSettingsPumpIpwmSection\(\)/);
  assert.match(mockDeviceSource, /setEntity\("select", "HP1 - Pump iPWM profile"/);
  assert.match(mockFixturesSource, /\["select", "HP2 - Pump iPWM profile"/);
  assert.match(entitySyncSource, /installation: \[\s*"hpGeneration",\s*\.\.\.PUMP_IPWM_PROFILE_KEYS/);
  assert.equal((entitySyncSource.match(/\.\.\.PUMP_IPWM_PROFILE_KEYS/g) || []).length, 2);
});
