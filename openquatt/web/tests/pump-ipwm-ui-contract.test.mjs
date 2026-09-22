import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { setLocale, t } from "../js/src/i18n/index.js";

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
  assert.match(renderer, /\[t\("settingsInstallation\.dtOduCode"\), technicalCode\]/);
  assert.match(renderer, /\[t\("settingsInstallation\.dtOduDesc"\), incident\.technicalDescription\]/);
  setLocale("nl", { persist: false, notify: false });
  assert.equal(t("settingsInstallation.dtOduCode"), "ODU-code");
  setLocale("en", { persist: false, notify: false });
  assert.equal(t("settingsInstallation.dtOduCode"), "ODU code");
  setLocale("nl", { persist: false, notify: false });
  assert.match(renderer, /getPumpIncidentContextRows\(incident, pumpContext\)/);
  assert.ok(renderer.indexOf("getIncidentDisplayLabel(incident)") < renderer.indexOf("details.map"));
});
