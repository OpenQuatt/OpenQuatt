import { readFileSync, readdirSync } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { CSS_SOURCE_FILES } from "./css-source-list.mjs";

const webDir = path.dirname(fileURLToPath(import.meta.url));
// Keep the OpenQuatt namespace so generated selectors cannot style the
// separately loaded ESPHome fallback UI on the same page.
const CLASS_SYMBOL_PREFIX = "oq-";
const CUSTOM_PROPERTY_SYMBOL_PREFIX = "--oq-";
const SYMBOL_ALPHABET = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

export const UNUSED_CSS_SYMBOLS = Object.freeze([
  "is-expanded",
  "oq-helper-control-card",
  "oq-helper-control-copy",
  "oq-hp-tech-condensor-ref-flow",
  "oq-hp-tech-condensor-ref-line",
  "oq-settings-mqtt-topic",
]);

// These modifier classes are assembled from runtime values. They deliberately
// keep their readable names unless the runtime construction is replaced by a
// generated lookup as part of the same change.
export const DYNAMIC_CSS_CLASS_FAMILIES = Object.freeze([
  ["oq-advice-pill--", ["muted", "recommended", "required"]],
  ["oq-debug-recording-feedback--", ["error", "warning"]],
  ["oq-energy-history-balance-part--", ["boiler", "boiler-zero", "cooling", "heat", "input"]],
  ["oq-energy-history-bar--", ["boiler", "boiler-zero", "cooling", "heat", "input"]],
  ["oq-energy-history-chart--", ["day", "month"]],
  ["oq-energy-history-legend-dot--", ["boiler", "boiler-zero", "cooling", "heat", "input"]],
  ["oq-energy-history-period--", ["day", "month", "week", "year"]],
  ["oq-hp-tech-4way-port--", ["hotgas", "suction"]],
  ["oq-hp-tech-pipe--", ["expansion", "hotgas", "liquid", "return", "suction", "supply"]],
  ["oq-hp-tech-pump--", ["supply"]],
  ["oq-hp-tech-tooltip--", ["component", "return", "supply", "warm"]],
  ["oq-overview-board--", ["light"]],
  ["oq-overview-chip--", ["active", "danger", "dhw", "neutral", "offline", "waiting", "warning"]],
  ["oq-overview-controlpanel--", ["blue", "green", "orange", "sky", "violet"]],
  ["oq-overview-controlpanel-meta-item--", ["blue", "green", "loading", "orange"]],
  ["oq-overview-controlpanel-state--", ["blue", "green", "orange", "sky", "violet"]],
  ["oq-overview-energy-category--", ["blue", "orange"]],
  ["oq-overview-hp--", ["focus", "muted"]],
  ["oq-overview-hp-grid--", ["equal", "focus-hp1", "focus-hp2", "single"]],
  ["oq-overview-trend-dot--", ["blue", "green", "orange", "sky", "slate"]],
  ["oq-overview-trend-hover-dot--", ["blue", "green", "orange", "sky"]],
  ["oq-overview-trend-hover-row--", ["blue", "green", "orange", "sky", "slate"]],
  ["oq-overview-trend-line--", ["blue", "green", "orange", "sky", "slate"]],
  ["oq-overview-trend-pill--", ["green", "orange"]],
  ["oq-ph-concept-tooltip--", ["above", "below", "setpoint"]],
  ["oq-settings-backup-compare--", ["current-missing", "different", "missing", "optional-missing", "optional-unavailable", "same"]],
  ["oq-settings-backup-result-item--", ["error", "warning"]],
  ["oq-settings-mqtt-sensor-status--", ["disabled", "invalid", "valid"]],
  ["oq-settings-source-info--", ["circle", "error", "invalid", "valid"]],
  ["oq-webserver-log-entry--", ["debug", "error", "info", "plain", "verbose", "warning"]],
  ["oq-working-chart-segment--", ["assist", "cooling", "defrost", "demand", "dewpoint", "limited", "running", "safe", "standby"]],
  ["oq-working-entry--", ["aggregate", "attention", "fault", "limited", "normal", "span"]],
  ["oq-working-now--", ["attention", "fault", "limited"]],
  ["oq-working-optimizer-option--", ["limited", "selected"]],
  ["oq-working-pill--", ["attention", "context", "fault", "info", "limited", "normal"]],
].map(([prefix, values]) => Object.freeze([prefix, Object.freeze(values)])));

// These names are consumed outside the generated app bundle by the preview
// harness or by source-level smoke contracts, so they remain stable.
export const STABLE_CSS_CLASS_NAMES = Object.freeze([
  "oq-helper-hub",
  "oq-helper-hub-block",
  "oq-helper-hub-kicker",
  "oq-helper-hub-toggle",
  "oq-helper-status-grid",
  "oq-native-app",
]);

const EXTERNAL_PREVIEW_FILES = Object.freeze([
  "dev.html",
  "js/device-wrapper.js",
  "js/mock-device.js",
  "js/mock-fixtures.js",
  "js/mock-incident-scenarios.js",
  "js/mock-scenarios.js",
]);

function collectFiles(directory, predicate) {
  return readdirSync(directory, { withFileTypes: true })
    .flatMap((entry) => {
      const entryPath = path.join(directory, entry.name);
      return entry.isDirectory() ? collectFiles(entryPath, predicate) : [entryPath];
    })
    .filter((file) => !predicate || predicate(file))
    .sort((left, right) => left.localeCompare(right));
}

function extractCssClassNames(source) {
  return new Set([...source.matchAll(/\.(oq-[A-Za-z0-9_-]+)/g)].map((match) => match[1]));
}

function extractAllCssClassNames(source) {
  return new Set([...source.matchAll(/\.(-?[_A-Za-z][A-Za-z0-9_-]*)/g)].map((match) => match[1]));
}

function extractCustomPropertyNames(source) {
  return new Set([...source.matchAll(/--oq-[A-Za-z0-9_-]+/g)].map((match) => match[0]));
}

function extractAllCustomPropertyNames(source) {
  return new Set([...source.matchAll(/--[_A-Za-z][A-Za-z0-9_-]*/g)].map((match) => match[0]));
}

function extractDynamicClassPrefixes(source) {
  const prefixes = new Set();
  for (const pattern of [
    /(?<![A-Za-z0-9_-])(oq-[A-Za-z0-9_-]*--)\$\{/g,
    /(?<![A-Za-z0-9_-])(oq-[A-Za-z0-9_-]*--)(?=["'`]\s*\+)/g,
    /["'`](oq-[A-Za-z0-9_-]*--)["'`]/g,
  ]) {
    for (const match of source.matchAll(pattern)) {
      prefixes.add(match[1]);
    }
  }
  return prefixes;
}

function escapeRegularExpression(value) {
  return value.replace(/[.*+?^${}()|[\]\\]/g, "\\$&");
}

function hasExactSymbol(source, symbol) {
  return new RegExp(
    `(?<![A-Za-z0-9_-])${escapeRegularExpression(symbol)}(?![A-Za-z0-9_-])`,
  ).test(source);
}

export function validateUnusedCssSymbolReferences(source, unusedCssSymbols, label = "runtime bundle") {
  const referencedSymbols = [...unusedCssSymbols].filter((symbol) => hasExactSymbol(source, symbol));
  if (referencedSymbols.length) {
    throw new Error(
      `CSS symbols marked unused are referenced by ${label}: `
      + referencedSymbols.sort().join(", "),
    );
  }
}

function extractSymbolCounts(source, pattern) {
  const counts = new Map();
  for (const match of source.matchAll(pattern)) {
    counts.set(match[0], (counts.get(match[0]) || 0) + 1);
  }
  return counts;
}

function extractClassSymbolCounts(source) {
  return extractSymbolCounts(source, /(?<![A-Za-z0-9_-])oq-[A-Za-z0-9_-]+(?![A-Za-z0-9_-])/g);
}

function extractCustomPropertySymbolCounts(source) {
  return extractSymbolCounts(source, /--oq-[A-Za-z0-9_-]+/g);
}

function encodeSymbol(index) {
  let value = index;
  let encoded = "";
  do {
    encoded = `${SYMBOL_ALPHABET[value % SYMBOL_ALPHABET.length]}${encoded}`;
    value = Math.floor(value / SYMBOL_ALPHABET.length) - 1;
  } while (value >= 0);
  return encoded;
}

function createMappings(symbols, symbolCounts, prefix, reservedSymbols) {
  const rankedSymbols = [...symbols].sort((left, right) => {
    const leftScore = (symbolCounts.get(left) || 0) * Math.max(1, left.length - prefix.length - 1);
    const rightScore = (symbolCounts.get(right) || 0) * Math.max(1, right.length - prefix.length - 1);
    return rightScore - leftScore || left.localeCompare(right);
  });
  const generatedSymbols = new Set(reservedSymbols);
  let generatedIndex = 0;
  return rankedSymbols.map((symbol) => {
    let replacement;
    do {
      replacement = `${prefix}${encodeSymbol(generatedIndex)}`;
      generatedIndex += 1;
    } while (generatedSymbols.has(replacement));
    generatedSymbols.add(replacement);
    return Object.freeze([symbol, replacement]);
  });
}

function configuredDynamicClassNames(dynamicClassFamilies) {
  return new Set(dynamicClassFamilies.flatMap(([prefix, values]) => values.map((value) => `${prefix}${value}`)));
}

function validateDynamicClassContracts(cssClassNames, javascriptSource, dynamicClassFamilies) {
  const dynamicClassNames = configuredDynamicClassNames(dynamicClassFamilies);
  const configuredPrefixes = new Set(dynamicClassFamilies.map(([prefix]) => prefix));
  const unknownDynamicPrefixes = [...extractDynamicClassPrefixes(javascriptSource)].filter((prefix) => (
    !configuredPrefixes.has(prefix)
    && [...cssClassNames].some((className) => className.startsWith(prefix))
  ));
  if (unknownDynamicPrefixes.length) {
    throw new Error(
      `Dynamic CSS class construction requires an explicit build contract: `
      + unknownDynamicPrefixes.sort().join(", "),
    );
  }
  for (const [prefix, configuredValues] of dynamicClassFamilies) {
    const actualValues = [...cssClassNames]
      .filter((className) => className.startsWith(prefix))
      .map((className) => className.slice(prefix.length))
      .sort();
    if (!actualValues.length) {
      continue;
    }
    const expectedValues = [...configuredValues].sort();
    if (actualValues.join("\n") !== expectedValues.join("\n")) {
      throw new Error(
        `Dynamic CSS class contract changed for ${prefix}: expected [${expectedValues.join(", ")}], `
        + `received [${actualValues.join(", ")}].`,
      );
    }
    if (!javascriptSource.includes(prefix)) {
      throw new Error(`Dynamic CSS class prefix ${prefix} is not present in the JavaScript sources.`);
    }
  }
  return dynamicClassNames;
}

function validateStableClassContracts(cssClassNames, externalPreviewSource, stableClassNames) {
  const stable = new Set(stableClassNames);
  const externalClassNames = new Set(extractClassSymbolCounts(externalPreviewSource).keys());
  const externalClasses = [...cssClassNames].filter((className) => (
    externalClassNames.has(className)
  ));
  const unexpectedExternalClasses = externalClasses.filter((className) => !stable.has(className));
  if (unexpectedExternalClasses.length) {
    throw new Error(
      `Preview files reference CSS classes outside the generated bundle without a stable contract: `
      + unexpectedExternalClasses.sort().join(", "),
    );
  }
  const missingStableClasses = stableClassNames.filter((className) => !cssClassNames.has(className));
  if (missingStableClasses.length) {
    throw new Error(`Stable CSS class contracts are missing from production CSS: ${missingStableClasses.join(", ")}`);
  }
  return stable;
}

export function createBundleSymbolPlan({
  productionCss,
  javascriptSource,
  externalPreviewSource = "",
  dynamicClassFamilies = DYNAMIC_CSS_CLASS_FAMILIES,
  stableClassNames = STABLE_CSS_CLASS_NAMES,
  unusedCssSymbols = UNUSED_CSS_SYMBOLS,
}) {
  const cssClassNames = extractCssClassNames(productionCss);
  const allCssClassNames = extractAllCssClassNames(productionCss);
  const customPropertyNames = extractCustomPropertyNames(productionCss);
  const allCustomPropertyNames = extractAllCustomPropertyNames(productionCss);
  const unusedSymbols = new Set(unusedCssSymbols);
  const externalRuntimeSource = `${javascriptSource}\n${externalPreviewSource}`;
  validateUnusedCssSymbolReferences(externalPreviewSource, unusedSymbols, "external preview runtime");
  const dynamicClassNames = validateDynamicClassContracts(
    cssClassNames,
    javascriptSource,
    dynamicClassFamilies,
  );
  const stableClasses = validateStableClassContracts(
    cssClassNames,
    externalPreviewSource,
    stableClassNames,
  );
  const javascriptClassCounts = extractClassSymbolCounts(javascriptSource);
  const staticClasses = [...cssClassNames].filter((className) => (
    !unusedSymbols.has(className)
    && !dynamicClassNames.has(className)
    && !stableClasses.has(className)
  ));
  const unreferencedClasses = staticClasses.filter((className) => (
    !javascriptClassCounts.has(className)
  ));
  if (unreferencedClasses.length) {
    throw new Error(
      `Production CSS classes are neither statically referenced nor explicitly classified: `
      + unreferencedClasses.sort().join(", "),
    );
  }

  const unexpectedDynamicCustomProperties = javascriptSource.match(/--oq-(?:\$\{|["'`]\s*\+)/g) || [];
  if (unexpectedDynamicCustomProperties.length) {
    throw new Error("Dynamic --oq-* custom-property construction requires an explicit build contract.");
  }

  const cssClassCounts = extractClassSymbolCounts(productionCss);
  const classCounts = new Map(javascriptClassCounts);
  for (const [className, count] of cssClassCounts) {
    classCounts.set(className, (classCounts.get(className) || 0) + count);
  }
  const cssCustomPropertyCounts = extractCustomPropertySymbolCounts(productionCss);
  const javascriptCustomPropertyCounts = extractCustomPropertySymbolCounts(javascriptSource);
  const customPropertyCounts = new Map(javascriptCustomPropertyCounts);
  for (const [propertyName, count] of cssCustomPropertyCounts) {
    customPropertyCounts.set(propertyName, (customPropertyCounts.get(propertyName) || 0) + count);
  }
  const classMappings = createMappings(
    staticClasses,
    classCounts,
    CLASS_SYMBOL_PREFIX,
    new Set([...allCssClassNames, ...extractClassSymbolCounts(externalRuntimeSource).keys()]),
  );
  const customPropertyMappings = createMappings(
    customPropertyNames,
    customPropertyCounts,
    CUSTOM_PROPERTY_SYMBOL_PREFIX,
    new Set([...allCustomPropertyNames, ...extractAllCustomPropertyNames(externalRuntimeSource)]),
  );
  return Object.freeze({
    classMappings: Object.freeze(classMappings),
    customPropertyMappings: Object.freeze(customPropertyMappings),
    dynamicClassNames: Object.freeze([...dynamicClassNames].sort()),
    stableClassNames: Object.freeze([...stableClasses].sort()),
    unusedCssSymbols: Object.freeze([...unusedSymbols].sort()),
  });
}

export function applyBundleSymbolPlan(source, plan) {
  const replacements = new Map([...plan.customPropertyMappings, ...plan.classMappings]);
  return source.replace(
    /--oq-[A-Za-z0-9_-]+|(?<![A-Za-z0-9_-])oq-[A-Za-z0-9_-]+(?![A-Za-z0-9_-])/g,
    (symbol) => replacements.get(symbol) || symbol,
  );
}

export function createBundleSymbolManifest(plan) {
  return {
    version: 1,
    classes: Object.fromEntries(plan.classMappings),
    customProperties: Object.fromEntries(plan.customPropertyMappings),
    dynamicClasses: [...plan.dynamicClassNames],
    stableClasses: [...plan.stableClassNames],
    unusedCssSymbols: [...plan.unusedCssSymbols],
  };
}

export function loadCanonicalBundleSymbolPlan() {
  const productionCss = CSS_SOURCE_FILES
    .filter((sourceFile) => sourceFile !== "css/src/02-devtools.css")
    .map((sourceFile) => readFileSync(path.join(webDir, ...sourceFile.split("/")), "utf8"))
    .join("\n");
  const javascriptSource = [
    ...collectFiles(path.join(webDir, "js", "src"), (file) => file.endsWith(".js")),
    path.join(webDir, "assets", "openquatt-logo.svg"),
  ].map((file) => readFileSync(file, "utf8")).join("\n");
  const externalPreviewSource = EXTERNAL_PREVIEW_FILES
    .map((file) => readFileSync(path.join(webDir, ...file.split("/")), "utf8"))
    .join("\n");
  return createBundleSymbolPlan({ productionCss, javascriptSource, externalPreviewSource });
}
