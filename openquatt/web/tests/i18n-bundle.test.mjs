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

test("compact i18n build uses a bounded runtime key index", () => {
  assert.equal(COMPACT_I18N_STATS.keys, 4370);
  assert.equal(COMPACT_I18N_STATS.dynamicKeys, 195);
  assert.ok(COMPACT_I18N_STATS.optionKeys > 50);
});
