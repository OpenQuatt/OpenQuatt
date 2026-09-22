import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { setLocale, t } from "../js/src/i18n/index.js";

// settings/installation.js cannot be imported in plain node (virtual build
// modules), so detail grouping is covered with source contracts, following
// the pump-ipwm-ui-contract pattern. Behaviour itself is tested through
// getLinkLossConsequenceForHeatPump in incident-monitoring.test.mjs.
const installationSource = await readFile(
  new URL("../js/src/settings/installation.js", import.meta.url),
  "utf8",
);

test("detailpagina groepeert een link-loss-stopgevolg onder de storingskaart", () => {
  assert.match(installationSource, /getLinkLossConsequenceForHeatPump/);
  const unitStart = installationSource.indexOf("function renderInstallationMonitoringHeatPumpUnit");
  const unitEnd = installationSource.indexOf(
    "function renderInstallationMonitoringStructuredHpPanel",
    unitStart,
  );
  assert.ok(unitStart >= 0 && unitEnd > unitStart);
  const unitRenderer = installationSource.slice(unitStart, unitEnd);
  // The consequence keeps no standalone card ...
  assert.match(unitRenderer, /shownIncidents/);
  assert.match(unitRenderer, /consequence\.id/);
  // ... and is nested inside the outage card instead.
  assert.match(unitRenderer, /linkLossConsequence\.copy/);
});

test("incidentdetail rendert een optionele gevolgtoelichting", () => {
  const start = installationSource.indexOf("export function renderInstallationMonitoringHpIncident");
  const end = installationSource.indexOf("function renderInstallationMonitoringHeatPumpUnit", start);
  assert.ok(start >= 0 && end > start);
  const renderer = installationSource.slice(start, end);
  assert.match(renderer, /consequenceNote/);
  assert.match(renderer, /\[t\("settingsInstallation\.dtConsequence"\), consequenceNote\]/);
  setLocale("nl", { persist: false, notify: false });
  assert.equal(t("settingsInstallation.dtConsequence"), "Gevolg");
  setLocale("en", { persist: false, notify: false });
  assert.equal(t("settingsInstallation.dtConsequence"), "Consequence");
  setLocale("nl", { persist: false, notify: false });
});

test("instellingenlijst toont de probleemtoelichting wanneer die er is", () => {
  assert.match(installationSource, /problem\.copy/);
});
