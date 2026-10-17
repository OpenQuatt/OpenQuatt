import assert from "node:assert/strict";
import { spawnSync } from "node:child_process";
import { copyFile, mkdir, mkdtemp, rm, writeFile } from "node:fs/promises";
import os from "node:os";
import path from "node:path";
import test from "node:test";

async function fixture(t, current, baseline) {
  const root = await mkdtemp(path.join(os.tmpdir(), "openquatt-web-quality-"));
  t.after(() => rm(root, { recursive: true, force: true }));
  const webDir = path.join(root, "web");
  const baseDir = path.join(root, "base");
  await mkdir(path.join(webDir, "js", "src"), { recursive: true });
  await mkdir(path.join(webDir, "css", "src"), { recursive: true });
  await mkdir(path.join(baseDir, "js"), { recursive: true });
  await copyFile(new URL("../check-web-quality.mjs", import.meta.url), path.join(webDir, "check-web-quality.mjs"));
  await writeFile(path.join(webDir, "web-budgets.mjs"), [
    'export const WEB_BUNDLE_BUDGETS = [{ file: "js/app.js", raw: 10000 }];',
    'export const WEB_BUNDLE_GZIP_GROWTH_LIMIT = { bytes: 10, ratio: 0.1 };',
  ].join("\n"));
  await writeFile(path.join(webDir, "js", "app.js"), current);
  if (baseline !== undefined) await writeFile(path.join(baseDir, "js", "app.js"), baseline);
  return { webDir, baseDir };
}

function check({ webDir, baseDir }, compare = true) {
  return spawnSync(process.execPath, [path.join(webDir, "check-web-quality.mjs")], {
    encoding: "utf8",
    timeout: 10000,
    env: {
      ...process.env,
      // An explicit built baseline must take precedence over legacy git lookup.
      OPENQUATT_WEB_BUNDLE_BASE_REF: compare ? "unavailable-base-ref" : "",
      OPENQUATT_WEB_BUNDLE_BASE_DIR: compare ? baseDir : "",
    },
  });
}

test("quality check compares generated bundles without requiring tracked bundles", async (t) => {
  const result = check(await fixture(t, "const answer = 42;", "const answer = 42;"));
  assert.equal(result.status, 0, result.stderr);
  assert.match(result.stdout, /delta \+0 B/);
});

test("quality check rejects excessive gzip growth against built baseline", async (t) => {
  const current = Array.from({ length: 200 }, (_, index) => `const value${index} = ${index};`).join("\n");
  const result = check(await fixture(t, current, "const answer = 42;"));
  assert.equal(result.status, 1, result.stderr);
  assert.match(result.stderr, /gzip growth:/);
});

test("quality check fails when a configured baseline bundle is missing", async (t) => {
  const result = check(await fixture(t, "const answer = 42;"));
  assert.equal(result.status, 1, result.stderr);
  assert.match(result.stderr, /ENOENT/);
});

test("quality check keeps absolute budgets without an explicit baseline", async (t) => {
  const result = check(await fixture(t, "x".repeat(10001)), false);
  assert.equal(result.status, 1, result.stderr);
  assert.match(result.stdout, /no base comparison/);
  assert.match(result.stderr, /raw: 10001 > 10000 bytes/);
});
