import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  clearTimeout: globalThis.clearTimeout,
  localStorage: { getItem: () => null },
  location: { pathname: "/" },
  setTimeout: globalThis.setTimeout,
};
globalThis.document = { activeElement: null };
const { state } = await import("../js/src/core/state.js");
const { AUX_RELAY_SETTING_KEYS, ENTITY_DEFS, SETTINGS_BACKUP_SCHEMA_VERSION } = await import("../js/src/core/config.js");
const { parseSettingsBackupPayload } = await import("../js/src/features/storage-history.js");

// Keep the real renderer; the aux relay section does not use embedded images.
const installationUrl = new URL("../js/src/settings/installation.js", import.meta.url);
let installationSource = await readFile(installationUrl, "utf8");
installationSource = installationSource.replace(
  /^import .* from "\.\.\/core\/embedded-assets.js";$/m,
  'const HP_GENERATION_IMAGE_V1 = "", HP_GENERATION_IMAGE_V2 = "";',
).replace(/(from\s+["'])(\.[^"']+)(["'])/g, (_, before, path, after) => `${before}${new URL(path, installationUrl).href}${after}`);
const { renderSettingsAuxRelaySection } = await import(`data:text/javascript;base64,${Buffer.from(installationSource).toString("base64")}`);

function auxEntities({ relayFunction, runOn, waitForSupply = false }) {
  return {
    auxRelayFunction: {
      value: relayFunction,
      option: ["Disabled", "Heating demand", "Cooling demand", "Heating or cooling demand", "External control"],
    },
    auxWaitForSupplyTemp: { value: waitForSupply },
    auxHeatingStartTemp: { value: 30 },
    auxCoolingStartTemp: { value: 18 },
    auxTempHysteresis: { value: 2 },
    auxRunOnMode: { value: runOn, option: ["Disabled", "Fixed time", "Until supply temp"] },
    auxRunOnTime: { value: 300 },
  };
}

function renderedFields(entities) {
  const previousEntities = state.entities;
  state.entities = entities;
  try {
    const html = renderSettingsAuxRelaySection();
    return {
      html,
      fields: [...html.matchAll(/data-oq-settings-field="([^"]+)"/g)].map((match) => match[1]),
    };
  } finally {
    state.entities = previousEntities;
  }
}

test("aux relay backup restore applies run-on settings before the function", () => {
  assert.equal(AUX_RELAY_SETTING_KEYS.at(-1), "auxRelayFunction");
  for (const key of ["auxRunOnTime", "auxRunOnMode"]) {
    assert.ok(AUX_RELAY_SETTING_KEYS.indexOf(key) >= 0, key);
    assert.ok(AUX_RELAY_SETTING_KEYS.indexOf(key) < AUX_RELAY_SETTING_KEYS.indexOf("auxRelayFunction"), key);
  }
  assert.equal(ENTITY_DEFS.auxRunOnMode.name, "Aux Relay Run-on");
  assert.equal(ENTITY_DEFS.auxRunOnTime.name, "Aux Relay Run-on Time");
});

test("legacy backups without run-on restore the aux relay function with run-on disabled", () => {
  const parse = (installation) => parseSettingsBackupPayload(JSON.stringify({
    schema_version: SETTINGS_BACKUP_SCHEMA_VERSION,
    settings: { installation },
  })).settings.installation;

  const legacy = parse({ auxRelayFunction: "Heating demand", auxWaitForSupplyTemp: false });
  assert.equal(legacy.auxRunOnMode, "Disabled");
  assert.ok(!Object.prototype.hasOwnProperty.call(legacy, "auxRunOnTime"));
  assert.equal(legacy.auxRelayFunction, "Heating demand");

  const current = parse({ auxRelayFunction: "Heating demand", auxRunOnMode: "Fixed time", auxRunOnTime: 600 });
  assert.equal(current.auxRunOnMode, "Fixed time");
  assert.equal(current.auxRunOnTime, 600);

  // Without a restored function the controller's run-on stays untouched, like any other absent setting.
  assert.ok(!Object.prototype.hasOwnProperty.call(parse({ hpGeneration: "V1" }), "auxRunOnMode"));
});

test("aux relay run-on fields follow the selected function and run-on mode", () => {
  assert.ok(!renderedFields(auxEntities({ relayFunction: "Disabled", runOn: "Fixed time" })).fields.includes("auxRunOnMode"));
  assert.ok(!renderedFields(auxEntities({ relayFunction: "External control", runOn: "Fixed time" })).fields.includes("auxRunOnMode"));

  const disabled = renderedFields(auxEntities({ relayFunction: "Heating demand", runOn: "Disabled" })).fields;
  assert.ok(disabled.includes("auxRunOnMode"));
  assert.ok(!disabled.includes("auxRunOnTime"));

  const fixed = renderedFields(auxEntities({ relayFunction: "Heating demand", runOn: "Fixed time" })).fields;
  assert.ok(fixed.includes("auxRunOnTime"));
  assert.ok(!fixed.includes("auxHeatingStartTemp"));

  const untilTemp = renderedFields(auxEntities({ relayFunction: "Heating demand", runOn: "Until supply temp" }));
  for (const key of ["auxRunOnTime", "auxHeatingStartTemp", "auxCoolingStartTemp", "auxTempHysteresis"]) {
    assert.ok(untilTemp.fields.includes(key), key);
  }
  // Without the supply gate the thresholds only end the run-on, so they must not promise a switch-on condition.
  assert.match(untilTemp.html, /stopt het nadraaien/);
  assert.doesNotMatch(untilTemp.html, /schakelt R2 pas in/);
});
