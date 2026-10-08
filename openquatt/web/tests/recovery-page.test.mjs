import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import vm from "node:vm";

const source = readFileSync(new URL("../../../components/openquatt_recovery/recovery_page.h", import.meta.url), "utf8")
  .match(/<script>([\s\S]*?)<\/script>/)[1];
const status = (extra = {}) => ({ active: true, busy: false, error: "", generation: 1, csrf_token: "boot-a",
  expires_in_ms: 600000, capabilities: { wifi_reset: true }, ...extra });
const response = (payload, code = 200) => ({ status: code, ok: code >= 200 && code < 300, json: async () => payload });
const deferred = () => { let resolve; const promise = new Promise(r => { resolve = r; }); return { promise, resolve }; };

async function page() {
  const elements = new Map();
  let fetch = async () => response(status());
  const context = vm.createContext({
    document: { querySelector: key => { if (!elements.has(key)) elements.set(key, {}); return elements.get(key); } },
    fetch: (...args) => fetch(...args), AbortSignal, URLSearchParams, FormData,
    setInterval: () => {}, confirm: () => true,
  });
  vm.runInContext(source, context);
  await new Promise(setImmediate);
  return { context, elements, fetch: value => { fetch = value; }, value: name => vm.runInContext(name, context) };
}

test("recovery ignores status reads from before and during a newly accepted action", async () => {
  const p = await page();
  const old = deferred(), during = deferred(), post = deferred();
  let reads = 0;
  p.fetch(async (_url, options = {}) => options.method === "POST" ? post.promise
    : ++reads === 1 ? old.promise : reads === 2 ? during.promise : response(status({ busy: true })));
  const oldRead = p.context.refresh();
  const action = p.context.post("/recovery/end");
  const duringRead = p.context.refresh();
  old.resolve(response(status({ error: "persist_failed" })));
  await oldRead;
  assert.equal(p.value("waiting"), true);
  assert.equal(p.elements.get("#message").textContent, "Bezig…");
  post.resolve(response({ accepted: true }, 202));
  await action;
  during.resolve(response(status({ error: "persist_failed" })));
  await duringRead;
  assert.equal(p.value("waiting"), true);
  assert.equal(p.elements.get("#actions").disabled, true);
  assert.match(p.elements.get("#message").textContent, /Verzoek geaccepteerd/);
});

test("an error before acceptance cannot unlock an uncertain recovery action", async () => {
  const p = await page();
  const post = deferred();
  p.fetch(async (_url, options = {}) => options.method === "POST" ? post.promise
    : response(status({ error: "persist_failed" })));
  const action = p.context.post("/recovery/end");
  await p.context.refresh();
  assert.equal(p.value("waiting"), true);
  assert.equal(p.elements.get("#actions").disabled, true);
  post.resolve(response({}, 503));
  await action;
  assert.equal(p.value("waiting"), true);
  assert.match(p.elements.get("#message").textContent, /Actie niet bevestigd/);
});

test("another boot's error cannot unlock a previous accepted recovery action", async () => {
  const p = await page();
  p.fetch(async (_url, options = {}) => options.method === "POST" ? response({}, 202)
    : response(status({ csrf_token: "boot-b", error: "persist_failed" })));
  await p.context.post("/recovery/end");
  assert.equal(p.value("waiting"), true);
  assert.equal(p.elements.get("#actions").disabled, true);
  assert.match(p.elements.get("#message").textContent, /vorige actie is niet bevestigd/);
});

test("the accepted action's storage failure permits an explicit retry", async () => {
  const p = await page();
  p.fetch(async (_url, options = {}) => options.method === "POST" ? response({}, 202)
    : response(status({ error: "persist_failed" })));
  await p.context.post("/recovery/end");
  assert.equal(p.value("waiting"), false);
  assert.equal(p.elements.get("#actions").disabled, false);
  assert.match(p.elements.get("#message").textContent, /expliciet een nieuwe actie/);
});
