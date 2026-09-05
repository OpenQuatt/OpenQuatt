import assert from "node:assert/strict";
import test from "node:test";
import {
  BUNDLE_SYMBOL_MANIFEST,
  compactHtmlTemplateWhitespace,
  minifyCssBundle,
  minifyJavaScriptBundle,
} from "../bundle-minifiers.mjs";
import {
  applyBundleSymbolPlan,
  createBundleSymbolManifest,
  createBundleSymbolPlan,
  validateUnusedCssSymbolReferences,
} from "../bundle-symbols.mjs";

test("HTML template whitespace is compacted without joining visible words", () => {
  const source = "const value = 'warm'; const markup = `\n  <section\n    class=\"card\"\n  >\n    Voor ${value}\n    <strong>na</strong>\n  </section>\n`;";
  const compact = compactHtmlTemplateWhitespace(source);
  assert.equal(
    compact,
    "const value = 'warm'; const markup = `\n  <section class=\"card\">\n    Voor ${value}\n    <strong>na</strong> </section>\n`;",
  );
  assert.equal(
    Function(`${compact}; return markup;`)(),
    '\n  <section class="card">\n    Voor warm\n    <strong>na</strong> </section>\n',
  );
  assert.equal(compactHtmlTemplateWhitespace(compact), compact);
});

test("HTML template compaction preserves quoted whitespace across interpolations", () => {
  const source = [
    "const kind = 'primary';",
    "const markup = `",
    "  <button",
    "    title=\"eerste",
    "      > tweede\"",
    "    class=\"${kind}",
    "      extra\"",
    "  >Label</button>",
    "`;",
  ].join("\n");
  const compact = compactHtmlTemplateWhitespace(source);
  assert.match(compact, /title=\"eerste\n      > tweede\"/);
  assert.match(compact, /class=\"\$\{kind\}\n      extra\"/);
  assert.match(compact, /<button title=/);
  assert.match(compact, />Label<\/button>\n`;/);
});

test("HTML template compaction handles unquoted interpolations and self-closing tags", () => {
  const source = [
    "const box = '0 0 10 10'; const path = 'M0 0h10';",
    "const markup = `",
    "  <svg",
    "    viewBox=${box}",
    "  >",
    "    <path",
    "      d=${path}",
    "    />",
    "  </svg>",
    "`;",
  ].join("\n");
  assert.equal(
    compactHtmlTemplateWhitespace(source),
    "const box = '0 0 10 10'; const path = 'M0 0h10';\nconst markup = `\n  <svg viewBox=${box}> <path d=${path} /> </svg>\n`;",
  );
});

test("HTML template compaction keeps self-closing syntax outside unquoted values", () => {
  const source = "const value = 'abc'; const markup = `<path data-x=${value} />`;";
  const compact = compactHtmlTemplateWhitespace(source);
  assert.equal(compact, source);
  assert.equal(Function(`${compact}; return markup;`)(), '<path data-x=abc />');
});

test("non-HTML and whitespace-preserving templates remain unchanged", () => {
  const source = [
    "const message = `eerste regel\n  tweede regel`;",
    "const pre = `\n  <pre>\n    vaste inspringing\n  </pre>\n`;",
    "const textarea = `\n  <textarea>\n    vaste inspringing\n  </textarea>\n`;",
    "const script = `\n  <script>\n    runExample();\n  </script>\n`;",
    "const style = `\n  <style>\n    .example { display: block; }\n  </style>\n`;",
  ].join("\n");
  assert.equal(compactHtmlTemplateWhitespace(source), source);
});

test("JavaScript and CSS bundle minifiers return executable compact output", async () => {
  const source = [
    'const name = "OpenQuatt";',
    'globalThis.__bundleCopy = "Temperatuur °C";',
    "globalThis.__bundleMarkup = `",
    "  <p>",
    "    Hallo ${name}",
    "  </p>",
    "`;",
  ].join("\n");
  const javascript = await minifyJavaScriptBundle(
    compactHtmlTemplateWhitespace(source),
    "test bundle",
  );
  Function(javascript)();
  assert.equal(globalThis.__bundleMarkup, "\n  <p>\n    Hallo OpenQuatt\n  </p>\n");
  assert.equal(globalThis.__bundleCopy, "Temperatuur °C");
  delete globalThis.__bundleMarkup;
  delete globalThis.__bundleCopy;
  assert.ok(!javascript.includes("\n"));

  assert.equal(
    minifyCssBundle(".card { color: rgb(255, 0, 0); margin: 0px 0px 0px 0px; }", "test.css"),
    ".card{color:red;margin:0}",
  );
  assert.equal(
    minifyCssBundle("@media (width <= 40rem) { .card { display: block; } }", "test.css"),
    "@media (max-width:40rem){.card{display:block}}",
  );
});

test("bundle symbol mappings are shared, deterministic and leave dynamic families readable", () => {
  const input = {
    productionCss: [
      ".oq-card { --oq-card-gap: 12px; gap: var(--oq-card-gap); }",
      ".oq-card--active { color: red; }",
    ].join("\n"),
    javascriptSource: [
      'const base = "oq-card";',
      'const dynamicClass = `oq-card--${tone}`;',
      'node.style.setProperty("--oq-card-gap", "8px");',
    ].join("\n"),
    dynamicClassFamilies: [["oq-card--", ["active"]]],
    stableClassNames: [],
    unusedCssSymbols: [],
  };
  const first = createBundleSymbolPlan(input);
  const second = createBundleSymbolPlan({
    ...input,
    productionCss: input.productionCss.split("\n").reverse().join("\n"),
    javascriptSource: input.javascriptSource.split("\n").reverse().join("\n"),
  });
  assert.deepEqual(createBundleSymbolManifest(first), createBundleSymbolManifest(second));

  const transformedCss = applyBundleSymbolPlan(input.productionCss, first);
  const transformedJavaScript = applyBundleSymbolPlan(input.javascriptSource, first);
  const mappedClass = first.classMappings[0][1];
  const mappedProperty = first.customPropertyMappings[0][1];
  assert.match(transformedCss, new RegExp(`\\.${mappedClass}\\b`));
  assert.match(transformedJavaScript, new RegExp(`"${mappedClass}"`));
  assert.ok(transformedCss.includes(".oq-card--active"));
  assert.ok(transformedJavaScript.includes("oq-card--${tone}"));
  assert.ok(transformedCss.includes(mappedProperty));
  assert.ok(transformedJavaScript.includes(mappedProperty));
});

test("generated symbols avoid existing class and custom-property names", () => {
  const plan = createBundleSymbolPlan({
    productionCss: ".oq-card { --oq-card-gap: 1px; }",
    javascriptSource: 'const value = "oq-card --oq-card-gap oq-a --oq-a";',
    dynamicClassFamilies: [],
    stableClassNames: [],
    unusedCssSymbols: [],
  });
  assert.equal(plan.classMappings[0][1], "oq-b");
  assert.equal(plan.customPropertyMappings[0][1], "--oq-b");
});

test("bundle symbol planning fails closed for unknown classes and dynamic suffixes", () => {
  assert.throws(
    () => createBundleSymbolPlan({
      productionCss: ".oq-orphan { color: red; }",
      javascriptSource: "",
      dynamicClassFamilies: [],
      stableClassNames: [],
      unusedCssSymbols: [],
    }),
    /neither statically referenced nor explicitly classified: oq-orphan/,
  );
  assert.throws(
    () => createBundleSymbolPlan({
      productionCss: ".oq-tone--new { color: red; }",
      javascriptSource: "const value = `oq-tone--${tone}`;",
      dynamicClassFamilies: [["oq-tone--", ["old"]]],
      stableClassNames: [],
      unusedCssSymbols: [],
    }),
    /Dynamic CSS class contract changed for oq-tone--/,
  );
  assert.throws(
    () => createBundleSymbolPlan({
      productionCss: ".oq-tone--new { color: red; }",
      javascriptSource: 'const exact = "oq-tone--new"; const dynamic = `oq-tone--${tone}`;',
      dynamicClassFamilies: [],
      stableClassNames: [],
      unusedCssSymbols: [],
    }),
    /Dynamic CSS class construction requires an explicit build contract: oq-tone--/,
  );
  assert.throws(
    () => createBundleSymbolPlan({
      productionCss: ".oq-tone--new { color: red; }",
      javascriptSource: 'const exact = "oq-tone--new"; setVariantClass(node, "oq-tone--", tone, ["new"]);',
      dynamicClassFamilies: [],
      stableClassNames: [],
      unusedCssSymbols: [],
    }),
    /Dynamic CSS class construction requires an explicit build contract: oq-tone--/,
  );
});

test("CSS symbols marked unused fail closed when runtime code starts using them", () => {
  assert.throws(
    () => validateUnusedCssSymbolReferences(
      'const className = "oq-old-card";',
      ["oq-old-card"],
      "test bundle",
    ),
    /CSS symbols marked unused are referenced by test bundle: oq-old-card/,
  );
});

test("external preview class consumers require an explicit stable contract", () => {
  const input = {
    productionCss: ".oq-preview-shared { display: block; }",
    javascriptSource: 'const markup = `<div class="oq-preview-shared"></div>`;',
    externalPreviewSource: 'const markup = `<div class="oq-preview-shared"></div>`;',
    dynamicClassFamilies: [],
    stableClassNames: [],
    unusedCssSymbols: [],
  };
  assert.throws(
    () => createBundleSymbolPlan(input),
    /without a stable contract: oq-preview-shared/,
  );
  const plan = createBundleSymbolPlan({ ...input, stableClassNames: ["oq-preview-shared"] });
  assert.deepEqual(plan.classMappings, []);
});

test("canonical symbol manifest excludes dynamic, stable and unused class contracts", () => {
  const mappedClasses = new Set(Object.keys(BUNDLE_SYMBOL_MANIFEST.classes));
  const mappedProperties = Object.values(BUNDLE_SYMBOL_MANIFEST.customProperties);
  assert.ok(mappedClasses.size > 900);
  assert.ok(mappedProperties.length > 200);
  BUNDLE_SYMBOL_MANIFEST.dynamicClasses.forEach((className) => assert.ok(!mappedClasses.has(className)));
  BUNDLE_SYMBOL_MANIFEST.stableClasses.forEach((className) => assert.ok(!mappedClasses.has(className)));
  BUNDLE_SYMBOL_MANIFEST.unusedCssSymbols.forEach((className) => assert.ok(!mappedClasses.has(className)));
  assert.equal(new Set(Object.values(BUNDLE_SYMBOL_MANIFEST.classes)).size, mappedClasses.size);
  assert.equal(new Set(mappedProperties).size, mappedProperties.length);
});
