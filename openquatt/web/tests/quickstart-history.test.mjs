import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

const quickStartSource = await readFile(new URL("../js/src/features/quickstart.js", import.meta.url), "utf8");

test("Quick Start verwijst naar beheer van lokale historie", () => {
  assert.match(quickStartSource, /class="oq-quickstart-history"/);
  assert.match(quickStartSource, /data-oq-action="open-history-storage-modal"/);
});
