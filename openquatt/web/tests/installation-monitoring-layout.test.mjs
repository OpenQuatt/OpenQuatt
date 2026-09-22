import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const installationSource = await readFile(new URL("../js/src/settings/installation.js", import.meta.url), "utf8");
const viewActionsSource = await readFile(new URL("../js/src/features/view-actions.js", import.meta.url), "utf8");
const stateSource = await readFile(new URL("../js/src/core/state-slices.js", import.meta.url), "utf8");
const serviceCss = await readFile(new URL("../css/src/12-settings-service.css", import.meta.url), "utf8");

test("compressor starts render as one compact table row with alarm windows emphasized", () => {
  const rowRenderer = installationSource.match(
    /export function renderInstallationMonitoringCompressorUnit[\s\S]+?return `<tr><th scope="row">[\s\S]+?<\/tr>`;/,
  )?.[0] || "";
  assert.match(rowRenderer, /<tr>/);
  assert.match(rowRenderer, /<th scope="row">/);
  assert.equal((rowRenderer.match(/class="is-alarm"/g) || []).length, 2);
  assert.doesNotMatch(rowRenderer, /oq-settings-monitoring-compressor-unit/);
});

test("compressor alarm limits use a persistent inline disclosure with exact number inputs", () => {
  assert.match(installationSource, /oq-starts/);
  assert.match(installationSource, /data-oq-action="toggle-compressor-limits"/);
  assert.match(installationSource, /renderSettingsMiniNumberField\("compressorStarts2hWarningLimit"/);
  assert.match(installationSource, /renderSettingsMiniNumberField\("compressorStarts72hWarningLimit"/);
  assert.doesNotMatch(installationSource, /renderSettingsSliderField/);
  assert.match(viewActionsSource, /"toggle-compressor-limits"[\s\S]*state\.compressorLimitsOpen/);
  assert.match(stateSource, /compressorLimitsOpen:\s*false/);
  assert.match(serviceCss, /\.oq-starts/);
  assert.match(serviceCss, /\.oq-start-fields/);
  assert.match(installationSource, /class="oq-start-editor"/);
  assert.match(installationSource, /t\("settingsInstallation\.limitsDone"\)/);
  assert.match(serviceCss, /\.oq-start-summary\s*\{[^}]*cursor:\s*pointer/);
});
