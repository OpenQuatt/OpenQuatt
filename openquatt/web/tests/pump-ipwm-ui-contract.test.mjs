import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const installationSource = await readFile(
  new URL("../js/src/settings/installation.js", import.meta.url),
  "utf8",
);

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
