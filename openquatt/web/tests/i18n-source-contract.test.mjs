import assert from "node:assert/strict";
import { readdir, readFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { parse } from "acorn";
import en from "../js/src/i18n/en.js";
import {
  LOCALE_STORAGE_KEY,
  getLocale,
  resetLocaleForTests,
  setLocale,
  t,
} from "../js/src/i18n/index.js";
import nl from "../js/src/i18n/nl.js";

const testsDir = path.dirname(fileURLToPath(import.meta.url));
const sourceRoot = path.join(testsDir, "..", "js", "src");
const productionFolders = ["core", "features", "settings", "views"];

const DUTCH_TEXT_PATTERN = /\b(?:aan|uit|geen|niet|alleen|onbekend|onbekende|gereed|bezig|laden|fout|fouten|mislukt|storing|storingen|waarschuwing|waarschuwingen|instelling|instellingen|installatie|verwarmen|verwarming|koelen|koeling|ketel|warmtepomp|temperatuur|vermogen|druk|stroom|bron|bronnen|wachtwoord|bestand|bestanden|historie|diagnose|voltooid|bevestigd|bevestigen|selecteer|kies|openen|sluiten|annuleren|verwijderen|wissen|herstellen|beschikbaar|actief|inactief|vandaag|gisteren|dagen|uren|minuten|seconden|terug|volgende|altijd|nooit|lager|hoger|waarde|waarden|velden|overgeslagen|ongeldig|de|het|een|je|jouw|wordt|worden|kan|kunnen|voor|van|met|zonder|naar|om|nog|opnieuw|eerst|huidige|actuele|bij|onder|boven|tussen|vanaf|zodra|zolang|terwijl|daarna|hiervoor|hieronder|hierboven|moet|mag|blijft|staat|heeft|konden|kon|vereist|aanbevolen|afgerond|ontbreekt|opgehaald|ververst|verstuurd|opgeslagen|gewist|verwijderd|verwacht|overslaan|inlaat|uitlaat|aanvoer|retour|buitenunit|binnenunit|bodemplaat|gegevens|systeem|overzicht|resultaten|keuze|regeling|toestemming|probleem|melding|oudste|nieuwste|gemiddeld|totaal|periode|starten|stoppen|doel|toegepast|toepassen|wijzigen|gewijzigd|toevoegen|proberen|herladen|vernieuwen|bewaren)\b/i;

const INTENTIONAL_INTERNAL_VALUES = new Map([
  ["core/config.js", new Set([
    "Trendopslag",
    "Trendhistorie opslaan in flash",
    "Trendhistorie nu opslaan",
    "Beslisloghistorie bewaren",
    "Beslisloghistorie nu opslaan",
    "Beslisloghistorie wissen",
    "Lifetime energiehistorie opslaan",
    "Uurdetail bewaren",
    "Lifetime energiehistorie nu opslaan",
    "Lifetime energiehistorie wissen",
  ])],
  ["features/control-replay-view.js", new Set(["CV-ketel", "Koeling"])],
  ["features/device-context.js", new Set(["onbekend"])],
  ["features/matrix-easter-egg.js", new Set(["koeling"])],
  ["views/overview.js", new Set([
    "Onbekend",
    "koeling",
    "koeling toegestaan, wacht op kamertemperatuur boven koel-setpoint",
    "gereed",
    "gereed om te koelen",
  ])],
]);

async function collectJavaScriptFiles(directory) {
  const entries = await readdir(directory, { withFileTypes: true });
  const nested = await Promise.all(entries.map((entry) => {
    const entryPath = path.join(directory, entry.name);
    if (entry.isDirectory()) return collectJavaScriptFiles(entryPath);
    return entry.isFile() && entry.name.endsWith(".js") ? [entryPath] : [];
  }));
  return nested.flat();
}

function walk(node, visitor, parent = null, parentKey = "") {
  if (!node || typeof node !== "object") return;
  visitor(node, parent, parentKey);
  Object.entries(node).forEach(([key, child]) => {
    if (key === "start" || key === "end" || key === "loc") return;
    if (Array.isArray(child)) child.forEach((item) => walk(item, visitor, node, key));
    else walk(child, visitor, node, key);
  });
}

function flattenCatalogue(value, prefix = "", result = new Map()) {
  Object.entries(value).forEach(([key, entry]) => {
    const fullKey = prefix ? `${prefix}.${key}` : key;
    if (typeof entry === "string") result.set(fullKey, entry);
    else flattenCatalogue(entry, fullKey, result);
  });
  return result;
}

function getStringValue(node) {
  if (node.type === "Literal" && typeof node.value === "string") return node.value;
  if (node.type === "TemplateElement") return node.value.raw;
  return null;
}

test("Dutch and English catalogues have identical key sets", () => {
  const nlKeys = [...flattenCatalogue(nl).keys()].sort();
  const enKeys = [...flattenCatalogue(en).keys()].sort();
  assert.deepEqual(enKeys, nlKeys);
});

test("locale selection persists and updates the document language", () => {
  const originalDocument = globalThis.document;
  const originalLocalStorage = globalThis.localStorage;
  const values = new Map();
  globalThis.document = { documentElement: { lang: "" } };
  globalThis.localStorage = {
    getItem(key) {
      return values.get(key) ?? null;
    },
    setItem(key, value) {
      values.set(key, value);
    },
  };

  try {
    resetLocaleForTests();
    assert.equal(getLocale(), "nl");
    assert.equal(setLocale("en"), "en");
    assert.equal(values.get(LOCALE_STORAGE_KEY), "en");
    assert.equal(globalThis.document.documentElement.lang, "en-GB");
    assert.equal(t("header.panelTitle"), "Display and system");

    resetLocaleForTests();
    assert.equal(getLocale(), "en");
  } finally {
    resetLocaleForTests();
    if (originalDocument === undefined) delete globalThis.document;
    else globalThis.document = originalDocument;
    if (originalLocalStorage === undefined) delete globalThis.localStorage;
    else globalThis.localStorage = originalLocalStorage;
  }
});

test("translation calls reference catalogue keys and production UI has no hardcoded Dutch copy", async () => {
  const catalogueKeys = new Set(flattenCatalogue(nl).keys());
  const files = (await Promise.all(
    productionFolders.map((folder) => collectJavaScriptFiles(path.join(sourceRoot, folder))),
  )).flat();
  const missingKeys = [];
  const hardcodedCopy = [];

  for (const file of files) {
    const source = await readFile(file, "utf8");
    const relativeFile = path.relative(sourceRoot, file);
    const ast = parse(source, { ecmaVersion: "latest", sourceType: "module" });
    walk(ast, (node, parent, parentKey) => {
      if (node.type === "CallExpression" && node.callee?.type === "Identifier" && node.callee.name === "t") {
        const key = node.arguments[0]?.value;
        if (typeof key === "string" && !catalogueKeys.has(key)) {
          missingKeys.push(`${relativeFile}: ${key}`);
        }
      }

      const value = getStringValue(node);
      if (!value || !DUTCH_TEXT_PATTERN.test(value)) return;
      if (parent?.type === "Property" && parentKey === "key" && !parent.computed) return;
      if (parent?.type === "ImportDeclaration" || parent?.type === "ExportNamedDeclaration") return;
      if (parent?.type === "CallExpression" && parent.callee?.name === "t") return;
      if (relativeFile === "features/heating-strategy-advice.js" && value.includes("openquatt.github.io")) return;
      if (INTENTIONAL_INTERNAL_VALUES.get(relativeFile)?.has(value)) return;
      hardcodedCopy.push(`${relativeFile}: ${JSON.stringify(value)}`);
    });
  }

  assert.deepEqual(missingKeys, [], `Missing translation keys:\n${missingKeys.join("\n")}`);
  assert.deepEqual(hardcodedCopy, [], `Hardcoded Dutch UI copy:\n${hardcodedCopy.join("\n")}`);
});

test("catalogue object literals do not contain duplicate keys", async () => {
  const duplicates = [];
  for (const locale of ["nl", "en"]) {
    const file = path.join(sourceRoot, "i18n", `${locale}.js`);
    const source = await readFile(file, "utf8");
    const ast = parse(source, { ecmaVersion: "latest", sourceType: "module" });
    walk(ast, (node) => {
      if (node.type !== "ObjectExpression") return;
      const seen = new Set();
      node.properties.forEach((property) => {
        if (property.type !== "Property" || property.computed) return;
        const key = property.key.type === "Identifier" ? property.key.name : property.key.value;
        if (seen.has(key)) duplicates.push(`${locale}: ${key}`);
        seen.add(key);
      });
    });
  }
  assert.deepEqual(duplicates, [], `Duplicate catalogue keys:\n${duplicates.join("\n")}`);
});
