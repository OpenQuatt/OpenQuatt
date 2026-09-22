import { readFile } from "node:fs/promises";
import path from "node:path";
import { parse } from "acorn";
import en from "./js/src/i18n/en.js";
import nl from "./js/src/i18n/nl.js";

const INDEX_SUFFIX = "/js/src/i18n/index.js";
const EN_IMPORT = 'import en from "./en.js";';
const NL_IMPORT = 'import nl from "./nl.js";';
const KEY_INDEX_MARKER = "const COMPACT_KEY_INDEX = null;";
const OPTION_INDEX_MARKER = "const COMPACT_OPTION_INDEX = null;";
const CATALOGUES_MARKER = "const CATALOGUES = { nl, en };";
const DYNAMIC_KEY_PREFIXES = ["incidents."];

function flattenCatalogue(value, prefix = "", result = new Map()) {
  Object.entries(value).forEach(([key, entry]) => {
    const fullKey = prefix ? `${prefix}.${key}` : key;
    if (typeof entry === "string") result.set(fullKey, entry);
    else flattenCatalogue(entry, fullKey, result);
  });
  return result;
}

function walk(node, visitor) {
  if (!node || typeof node !== "object") return;
  visitor(node);
  Object.entries(node).forEach(([key, child]) => {
    if (key === "start" || key === "end" || key === "loc") return;
    if (Array.isArray(child)) child.forEach((item) => walk(item, visitor));
    else if (child && typeof child === "object" && typeof child.type === "string") walk(child, visitor);
  });
}

const nlEntries = flattenCatalogue(nl);
const enEntries = flattenCatalogue(en);
const translationKeys = [...nlEntries.keys()].sort();
const enKeys = [...enEntries.keys()].sort();
if (translationKeys.length !== enKeys.length
    || translationKeys.some((key, index) => key !== enKeys[index])) {
  throw new Error("Compact i18n bundle requires identical Dutch and English catalogue keys");
}

const translationIds = new Map(translationKeys.map((key, index) => [key, index]));
const compactCatalogues = {
  nl: translationKeys.map((key) => nlEntries.get(key)),
  en: translationKeys.map((key) => enEntries.get(key)),
};
// Alle statische sleutels worden in bronmodules naar een numeriek ID herschreven.
// Alleen families die bewust vanuit firmwarecodes worden samengesteld hebben
// tijdens runtime nog een kleine string-naar-ID-index nodig.
const compactKeyIndex = Object.fromEntries(
  translationKeys
    .filter((key) => DYNAMIC_KEY_PREFIXES.some((prefix) => key.startsWith(prefix)))
    .map((key) => [key, translationIds.get(key)]),
);
const compactOptionIndex = Object.fromEntries(
  Object.keys(nl.options || {}).map((value) => [value, translationIds.get(`options.${value}`)]),
);

function replaceTranslationKeyLiterals(source) {
  const syntaxTree = parse(source, { ecmaVersion: "latest", sourceType: "module" });
  const edits = [];
  walk(syntaxTree, (node) => {
    if (node.type !== "Literal" || typeof node.value !== "string") return;
    const id = translationIds.get(node.value);
    if (id === undefined) return;
    edits.push({ start: node.start, end: node.end, replacement: String(id) });
  });
  return edits
    .sort((left, right) => right.start - left.start)
    .reduce((output, edit) => (
      `${output.slice(0, edit.start)}${edit.replacement}${output.slice(edit.end)}`
    ), source);
}

function replaceRequired(source, marker, replacement) {
  if (!source.includes(marker)) {
    throw new Error(`Compact i18n bundle marker is missing: ${marker}`);
  }
  return source.replace(marker, replacement);
}

export function compactI18nBundleSource(source, filename = "") {
  const normalizedFilename = filename.split(path.sep).join("/");
  let output = replaceTranslationKeyLiterals(source);
  if (!normalizedFilename.endsWith(INDEX_SUFFIX)) return output;

  output = replaceRequired(output, EN_IMPORT, "");
  output = replaceRequired(output, NL_IMPORT, "");
  output = replaceRequired(
    output,
    KEY_INDEX_MARKER,
    `const COMPACT_KEY_INDEX = ${JSON.stringify(compactKeyIndex)};`,
  );
  output = replaceRequired(
    output,
    OPTION_INDEX_MARKER,
    `const COMPACT_OPTION_INDEX = ${JSON.stringify(compactOptionIndex)};`,
  );
  output = replaceRequired(
    output,
    CATALOGUES_MARKER,
    `const CATALOGUES = ${JSON.stringify(compactCatalogues)};`,
  );
  return output;
}

export function compactI18nSourcePlugin(sourceTransform = (source) => source) {
  return {
    name: "openquatt-compact-i18n",
    setup(pluginBuild) {
      pluginBuild.onLoad({ filter: /\.js$/, namespace: "file" }, async (args) => ({
        contents: compactI18nBundleSource(sourceTransform(await readFile(args.path, "utf8")), args.path),
        loader: "js",
      }));
    },
  };
}

export const COMPACT_I18N_STATS = Object.freeze({
  keys: translationKeys.length,
  dynamicKeys: Object.keys(compactKeyIndex).length,
  optionKeys: Object.keys(compactOptionIndex).length,
});
