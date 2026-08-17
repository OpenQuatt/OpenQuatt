import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import path from "node:path";
import test from "node:test";
import { fileURLToPath } from "node:url";

const testDir = path.dirname(fileURLToPath(import.meta.url));
const serviceSource = await readFile(path.join(testDir, "../js/src/settings/service.js"), "utf8");
const mockSource = await readFile(path.join(testDir, "../js/mock-device.js"), "utf8");
const devHtml = await readFile(path.join(testDir, "../dev.html"), "utf8");

test("CM100 handmatige warmtepomp gebruikt in UI en mock het bereik 0..20", () => {
  assert.equal(serviceSource.match(/Aangevraagde stand 0 tot en met 20\./g)?.length, 2);
  assert.match(mockSource, /\["Manual HP1 compressor level", 0, 0, 20, 1, ""\]/);
  assert.match(mockSource, /\["Manual HP2 compressor level", 0, 0, 20, 1, ""\]/);
  assert.match(devHtml, /mock-device\.js\?v=q-manual-hp-level-20-v1/);
  assert.match(devHtml, /openquatt-preview\.js\?v=q-manual-hp-level-20-v1/);
});
