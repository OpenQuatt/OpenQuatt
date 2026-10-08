import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import vm from "node:vm";
import test from "node:test";

test("preview firmware checks publish numeric revisions, including unchanged manifests and target changes", async () => {
  const window = {
    crypto: globalThis.crypto,
    location: { href: "http://localhost/dev.html", search: "" },
    addEventListener() {}, dispatchEvent() {}, setInterval() {}, setTimeout() {}, clearTimeout() {},
    localStorage: { getItem() { return null; }, setItem() {} },
  };
  const context = { window, document: { querySelector() { return null; }, querySelectorAll() { return []; } }, URL, URLSearchParams, CustomEvent: class {}, Event: class {}, console };
  for (const file of ["mock-scenarios.js", "mock-incident-scenarios.js", "mock-entity-defs.js", "mock-fixtures.js", "mock-device.js"]) {
    vm.runInNewContext(await readFile(new URL(`../js/${file}`, import.meta.url), "utf8"), context, { filename: file });
  }
  const readEntity = async () => {
    const body = new URLSearchParams({ detail: "all", entities: "firmwareUpdate\tupdate\tFirmware Update" });
    const response = await window.fetch("/openquatt/entities", { method: "POST", body: body.toString() });
    return (await response.json()).entities.firmwareUpdate;
  };
  const read = async () => (await window.fetch("/openquatt/firmware/metadata")).json();
  const initial = await readEntity();
  assert.equal(initial.manifest_revision, undefined, "upstream update JSON has no local metadata extension");
  assert.equal((await read()).manifest_revision, 0);
  assert.match((await read()).boot_id, /^[0-9a-f]{16}$/);
  for (let revision = 1; revision <= 2; revision += 1) {
    await window.fetch("/button/Check%20Firmware%20Updates/press", { method: "POST" });
    const entity = await readEntity();
    assert.equal((await read()).manifest_revision, revision);
    assert.equal(entity.latest_version, initial.latest_version);
  }
  await window.fetch("/select/Firmware%20Update%20Target/set?option=alternate%20topology", { method: "POST" });
  assert.equal((await read()).manifest_revision, 3);
  await window.fetch("/select/Firmware%20Update%20Target/set?option=current%20build", { method: "POST" });
  assert.equal((await read()).manifest_revision, 4);
  await window.fetch("/select/Firmware%20Update%20Channel/set?option=main", { method: "POST" });
  assert.equal((await read()).manifest_revision, 5);
  await window.fetch("/update/Firmware%20Update/install", { method: "POST" });
  assert.equal((await read()).manifest_revision, 5, "install/progress events are not manifest publications");
});
