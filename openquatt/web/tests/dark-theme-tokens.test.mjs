import assert from "node:assert/strict";
import { readdir, readFile } from "node:fs/promises";
import test from "node:test";

const cssDir = new URL("../css/src/", import.meta.url);
const cssFiles = (await readdir(cssDir)).filter((name) => name.endsWith(".css")).sort();
const cssSources = await Promise.all(cssFiles.map(async (name) => [name, await readFile(new URL(name, cssDir), "utf8")]));
const tokensCss = Object.fromEntries(cssSources)["00-tokens.css"];

function getDarkTokenBlock() {
  const start = tokensCss.indexOf(".oq-helper-shell--dark,\n.oq-helper-modal-backdrop--dark {");
  assert.notEqual(start, -1, "dark token block not found in 00-tokens.css");
  return tokensCss.slice(start, tokensCss.indexOf("\n}", start));
}

test("every --oq-theme-* token used by a component has a dark value", () => {
  const darkBlock = getDarkTokenBlock();
  const defined = new Set([...darkBlock.matchAll(/^\s*(--oq-theme-[\w-]+):/gm)].map(([, name]) => name));
  const missing = [];
  for (const [file, source] of cssSources) {
    for (const [, name] of source.matchAll(/var\((--oq-theme-[\w-]+)/g)) {
      if (!defined.has(name)) missing.push(`${file}: ${name}`);
    }
  }
  assert.deepEqual([...new Set(missing)], []);
});

test("dark theme overrides the accent tokens that light mode uses for text and soft surfaces", () => {
  const darkBlock = getDarkTokenBlock();
  assert.match(darkBlock, /--oq-helper-accent-deep:/);
  assert.match(darkBlock, /--oq-helper-accent-soft:/);
});
