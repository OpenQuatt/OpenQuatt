import assert from "node:assert/strict";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";
import en from "../js/src/i18n/en.js";
import nl from "../js/src/i18n/nl.js";
import {
  COMPACT_I18N_STATS,
  compactI18nBundleSource,
  compactI18nSourcePlugin,
} from "../i18n-bundle.mjs";

const testsDir = path.dirname(fileURLToPath(import.meta.url));
const sourceRoot = path.join(testsDir, "..", "js", "src");

function flattenCatalogue(value, prefix = "", result = new Map()) {
  Object.entries(value).forEach(([key, entry]) => {
    const fullKey = prefix ? `${prefix}.${key}` : key;
    if (typeof entry === "string") result.set(fullKey, entry);
    else flattenCatalogue(entry, fullKey, result);
  });
  return result;
}

async function buildCompactFixture() {
  const source = compactI18nBundleSource(`
    import { optionLabel, setLocale, t } from "./i18n/index.js";
    import { renderOduDefrostModal } from "./features/odu-defrost.js";
    import { state } from "./core/state.js";
    import { renderMeasurementArchive } from "./settings/measurement-archive.js";
    import { renderBleSettings, renderBleModal, renderBleReadings } from "./settings/ble.js";
    export function readBle(locale, withSensors = false) {
      setLocale(locale, { persist: false, applyDocument: false, notify: false });
      state.entities = { roomTempSource: { option: ["BLE"] } };
      const sensors = withSensors ? [{ mac: "DA:11:22:33:44:01", name: "Test", use_dew_point: true }] : [];
      const room_mac = sensors[0]?.mac || "";
      const status = { sensors, room_mac, discovered: [], enabled: false };
      return renderBleSettings() + renderBleModal() + renderBleReadings(status, false, false)
        + (withSensors ? renderBleReadings(status, false, false, true, false, true) : "");
    }
    import { renderLearningExplanation } from "./views/learning-explanation.js";
    export function readMeasurementArchive(locale, code) {
      setLocale(locale, { persist: false, applyDocument: false, notify: false });
      state.entities = {
        measurementArchiveEnabled: { value: code === "recording" },
        measurementArchiveStatus: { value: code + "|1794873600|1794960000|42" },
      };
      return renderMeasurementArchive();
    }
    export function readLearningExplanation(locale) {
      setLocale(locale, { persist: false, applyDocument: false, notify: false });
      return renderLearningExplanation();
    }
    export function readDefrostInfo(locale, variant) {
      setLocale(locale, { persist: false, applyDocument: false, notify: false });
      state.systemModal = "odu-defrost-info-1";
      state.oduDefrostStatuses = { 1: { variant } };
      return renderOduDefrostModal();
    }
    export function readTranslations(locale, category) {
      setLocale(locale, { persist: false, applyDocument: false, notify: false });
      return {
        on: t("common.on"),
        automatic: optionLabel("Auto"),
        category: t(\`incidents.category.\${category}\`),
      };
    }
    export function readDynamicTranslations(locale, keys) {
      setLocale(locale, { persist: false, applyDocument: false, notify: false });
      return keys.map((key) => t(key));
    }
  `, path.join(sourceRoot, "compact-i18n-fixture.js"));
  const result = await build({
    stdin: { contents: source, resolveDir: sourceRoot, sourcefile: "compact-i18n-fixture.js" },
    bundle: true,
    format: "esm",
    platform: "node",
    target: "es2020",
    define: { __OQ_PREVIEW__: "false" },
    write: false,
    plugins: [compactI18nSourcePlugin()],
  });
  const output = result.outputFiles[0].text;
  const module = await import(`data:text/javascript;base64,${Buffer.from(output).toString("base64")}`);
  return { module, output };
}

test("compact i18n build keeps both locales and dynamic incident keys", async () => {
  const { module, output } = await buildCompactFixture();
  assert.deepEqual(module.readTranslations("nl", "fault"), {
    on: nl.common.on,
    automatic: nl.options.Auto,
    category: nl.incidents.category.fault,
  });
  assert.deepEqual(module.readTranslations("en", "fault"), {
    on: en.common.on,
    automatic: en.options.Auto,
    category: en.incidents.category.fault,
  });
  assert.doesNotMatch(output, /common\.on/);
  assert.doesNotMatch(output, /options\.Auto/);

  const nlEntries = flattenCatalogue(nl);
  const enEntries = flattenCatalogue(en);
  const dynamicKeys = [...nlEntries.keys()].filter((key) => key.startsWith("incidents."));
  assert.deepEqual(
    module.readDynamicTranslations("nl", dynamicKeys),
    dynamicKeys.map((key) => nlEntries.get(key)),
  );
  assert.deepEqual(
    module.readDynamicTranslations("en", dynamicKeys),
    dynamicKeys.map((key) => enEntries.get(key)),
  );
});

test("the compact production bundle localizes Bluetooth settings and scan controls", async () => {
  const { module } = await buildCompactFixture();
  for (const [locale, catalogue] of [["nl", nl], ["en", en]]) {
    const html = module.readBle(locale);
    for (const key of ["title", "integrationCopy", "sourceHint", "enable", "sensors", "empty", "scan"]) assert.ok(html.includes(catalogue.ble[key]));
    assert.ok(html.includes(catalogue.settingsIntegrations.integrationAdjust));
    assert.ok(html.includes(catalogue.settingsIntegrations.integrationCurrentStatus));
    assert.doesNotMatch(html, /ble\.[a-z]/);
    const modal = module.readBle(locale, true);
    for (const key of ["changeRoom", "clearRoom", "currentRoom", "temperatureShort", "humidityShort", "dewPoint", "lastMeasurement", "dewAssigned"]) {
      assert.ok(modal.includes(catalogue.ble[key]), `${locale}: ${key}`);
    }
    assert.doesNotMatch(modal, /ble\.[a-z]/);
  }
});

test("the compact production bundle resolves both learning guide translations", async () => {
  const { module } = await buildCompactFixture();
  for (const [locale, catalogue] of [["nl", nl], ["en", en]]) {
    const { intro, flowLabel, scenarioLabel, pauseCopy } = catalogue.learningExplanation;
    const html = module.readLearningExplanation(locale);
    assert.doesNotMatch(html, /learningExplanation\./);
    for (const label of [intro, flowLabel, scenarioLabel, pauseCopy]) assert.ok(html.includes(label));
  }
});

test("the compact production bundle localizes the measurement archive", async () => {
  const { module } = await buildCompactFixture();
  globalThis.window = { location: { pathname: "/" } };
  try {
    for (const [locale, catalogue] of [["nl", nl], ["en", en]]) {
      const { title, periodLabel, recording, off, date, download, downloadAll, fields, fieldsTemperature, fieldsHeat, fieldsOperation, fieldsWeather } = catalogue.measurementArchive;
      const activeHtml = module.readMeasurementArchive(locale, "recording");
      assert.doesNotMatch(activeHtml, /measurementArchive\./);
      for (const label of [title, periodLabel, recording, date, download, downloadAll, fields, fieldsTemperature, fieldsHeat, fieldsOperation, fieldsWeather]) assert.ok(activeHtml.includes(label));
      assert.ok(module.readMeasurementArchive(locale, "off").includes(off));
    }
  } finally {
    delete globalThis.window;
  }
});

test("compact i18n build uses a bounded runtime key index", () => {
  assert.equal(COMPACT_I18N_STATS.keys, flattenCatalogue(nl).size);
  assert.equal(COMPACT_I18N_STATS.dynamicKeys, 195);
  assert.ok(COMPACT_I18N_STATS.optionKeys > 50);
});

test("compiled defrost method help resolves NL/EN keys for supported variants", async () => {
  const { module } = await buildCompactFixture();
  for (const [locale, catalogue] of [["nl", nl], ["en", en]]) {
    for (const variant of [1, 2, 3, 4]) {
      const html = module.readDefrostInfo(locale, variant);
      assert.doesNotMatch(html, /oduDefrost\.mode\dInfo/);
      for (const mode of [0, 1, 3, 4]) {
        const copy = catalogue.oduDefrost[`mode${mode}Info`];
        assert.equal(html.includes(copy), mode !== 4 || variant !== 1);
      }
    }
  }
});
