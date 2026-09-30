import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import test from "node:test";
import { fileURLToPath } from "node:url";
import { build } from "esbuild";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  localStorage: { getItem: () => null },
  location: { pathname: "/" },
  setTimeout: globalThis.setTimeout,
  clearTimeout: globalThis.clearTimeout,
};

const { CURVE_POINTS, ENTITY_DEFS } = await import("../js/src/core/config.js");
const { INITIAL_SETTINGS_READY_KEY_MAP, SETTINGS_GROUP_KEY_MAP } = await import("../js/src/core/entity-sync.js");
const { state } = await import("../js/src/core/state.js");
const { normalizeNumber } = await import("../js/src/core/entity-store.js");
const { applySimpleCurveBatch, applySimpleCurvePoints, generateSimpleCurve, getCurvePointDraft, getSimpleCurveDraft, updateCurvePointDraft, updateSimpleCurveDraft } = await import("../js/src/core/simple-curve.js");
const { renderSettingsCurveInputs, renderSimpleCurvePreview } = await import("../js/src/settings/heating.js");
const { handleControlAction, submitSimpleCurveBatch } = await import("../js/src/core/control-actions.js");
const { setRenderCallback } = await import("../js/src/core/render-scheduler.js");

test("curve-invoer blijft via gedelegeerde input/focus/change-listeners behouden", async () => {
  const bundle = await build({
    bundle: true, write: false, format: "esm", platform: "node", define: { __OQ_PREVIEW__: "false" },
    stdin: {
      resolveDir: fileURLToPath(new URL(".", import.meta.url)),
      contents: `
        export { state } from "../js/src/core/state.js";
        export { setRenderCallback } from "../js/src/core/render-scheduler.js";
        export { renderSimpleCurvePreview } from "../js/src/settings/heating.js";
        export * as curveEventHandlers from "../js/src/core/entity-actions.js";
        export * as delegatedEvents from "../js/src/core/event-handlers.js";
      `,
    },
    plugins: [{ name: "test-assets", setup(plugin) {
      plugin.onResolve({ filter: /^virtual:embedded-assets$/ }, () => ({ path: "assets", namespace: "test-assets" }));
      plugin.onLoad({ filter: /.*/, namespace: "test-assets" }, () => ({
        contents: 'export const HP_GENERATION_IMAGE_V1 = "", HP_GENERATION_IMAGE_V2 = "", LOGO_MARKUP = "";',
      }));
    } }],
  });
  const { state, setRenderCallback, renderSimpleCurvePreview, curveEventHandlers, delegatedEvents } =
    await import(`data:text/javascript;base64,${Buffer.from(bundle.outputFiles[0].text).toString("base64")}`);
  const originalDocument = globalThis.document;
  const root = new EventTarget();
  const input = { type: "number", dataset: { oqCurvePointInput: "curve0" }, value: "40", closest: () => null };
  const dispatch = (type) => {
    const event = new Event(type);
    Object.defineProperty(event, "target", { value: input });
    root.dispatchEvent(event);
  };
  let renders = 0;
  state.entities = Object.fromEntries(CURVE_POINTS.map((point) => [point.key, { value: 38 }]));
  state.drafts = {};
  state.simpleCurveDraft = null;
  state.curvePointDraft = null;
  state.appView = "settings";
  delegatedEvents.setEventHandlers(curveEventHandlers);
  root.addEventListener("input", delegatedEvents.handleInput);
  root.addEventListener("change", delegatedEvents.handleChange);
  root.addEventListener("focusin", delegatedEvents.handleFocusChange);
  root.addEventListener("focusout", delegatedEvents.handleFocusChange);
  try {
    globalThis.document = { activeElement: input };
    setRenderCallback(() => { renders += 1; });
    dispatch("focusin");
    await new Promise((resolve) => setTimeout(resolve, 0));
    assert.equal(state.focusedField, "curve0");
    dispatch("input");
    assert.equal(state.curvePointDraft[2], 40);
    assert.equal(renders, 0);
    state.entities.curve0.value = 38;
    assert.match(renderSimpleCurvePreview(), /value="40\.0" data-oq-curve-point-input="curve0"/);
    globalThis.document.activeElement = null;
    dispatch("focusout");
    await new Promise((resolve) => setTimeout(resolve, 0));
    assert.equal(state.focusedField, "");
    dispatch("change");
    assert.equal(renders, 1);
    assert.equal(state.curvePointDraft[2], 40);
    assert.match(renderSimpleCurvePreview(), /Wijzigingen nog niet opgeslagen/);
  } finally {
    setRenderCallback(null);
    delegatedEvents.setEventHandlers({});
    globalThis.document = originalDocument;
  }
});

async function runDelayedCurveSave({ status = 202, edit = () => {}, failWrites = false } = {}) {
  const originalFetch = globalThis.fetch;
  const originals = [55, 50, 45, 42.5, 40, 37.5];
  const remote = new Map(CURVE_POINTS.map((point, index) => [point.key, originals[index]]));
  let releaseResponse;
  let requestStarted;
  let saveCompleted;
  const responseGate = new Promise((resolve) => { releaseResponse = resolve; });
  const started = new Promise((resolve) => { requestStarted = resolve; });
  const completed = new Promise((resolve) => { saveCompleted = resolve; });
  const response = (payload, code = 200) => ({ ok: code < 400, status: code, json: async () => payload });
  const writes = [];
  state.entities = Object.fromEntries(CURVE_POINTS.map((point, index) => [point.key, { value: originals[index] }]));
  state.drafts = {};
  state.inputDrafts = {};
  state.simpleCurveDraft = null;
  state.curvePointDraft = null;
  state.simpleCurveApplying = false;
  state.loadingEntities = false;
  state.appView = "overview";
  state.controlError = "";
  state.controlNotice = "";
  updateSimpleCurveDraft("level", 40);
  const submitted = getCurvePointDraft();
  try {
    setRenderCallback(() => {
      if (!state.simpleCurveApplying) saveCompleted();
    });
    globalThis.fetch = async (url, options = {}) => {
      const path = new URL(url, "http://controller.local").pathname;
      if (path === "/auth/status") return response({ csrf_token: "test-token" });
      if (path === "/openquatt/curve/apply") {
        requestStarted();
        await responseGate;
        if (status === 202) {
          const body = new URLSearchParams(options.body);
          for (const point of CURVE_POINTS) remote.set(point.key, Number(body.get(point.key)));
        }
        return response({ ok: status === 202, queued: status === 202 }, status);
      }
      if (options.method === "POST" && path.startsWith("/number/")) {
        const name = decodeURIComponent(path.split("/")[2] || "");
        const key = CURVE_POINTS.find((item) => name === ENTITY_DEFS[item.key].name)?.key;
        writes.push(name);
        if (failWrites) return response({}, 500);
        const value = Number(new URL(url, "http://controller.local").searchParams.get("value"));
        if (key) remote.set(key, value);
        return response({});
      }
      return response({ entities: Object.fromEntries([...remote].map(([key, value]) => [key, { value }])) });
    };
    handleControlAction("apply-simple-curve", {});
    await started;
    assert.equal(state.simpleCurveApplying, true);
    edit();
    releaseResponse();
    await completed;
    assert.equal(state.simpleCurveApplying, false);
    return { remote, submitted, writes };
  } finally {
    releaseResponse();
    setRenderCallback(null);
    globalThis.fetch = originalFetch;
  }
}

test("batchbevestiging behoudt een schuifconcept dat tijdens het echte verzoek is gewijzigd", { timeout: 3000 }, async () => {
  const { remote, submitted } = await runDelayedCurveSave({ edit: () => updateSimpleCurveDraft("level", 50) });
  assert.deepEqual(CURVE_POINTS.map((point) => remote.get(point.key)), submitted.map((point) => point.value));
  assert.deepEqual(state.simpleCurveDraft, { slope: 5, level: 50 });
  assert.deepEqual(state.curvePointDraft, generateSimpleCurve(5, 50).map((point) => point.value));
  assert.match(renderSimpleCurvePreview(), /Wijzigingen nog niet opgeslagen/);
});

test("batchbevestiging wist alleen een onaangeraakt concept", { timeout: 3000 }, async () => {
  await runDelayedCurveSave();
  assert.equal(state.simpleCurveDraft, null);
  assert.equal(state.curvePointDraft, null);
});

test("batchbevestiging behoudt ook nieuwere handmatige curvepunten", { timeout: 3000 }, async () => {
  await runDelayedCurveSave({ edit: () => updateCurvePointDraft("curve5", 44) });
  assert.equal(state.simpleCurveDraft, null);
  assert.equal(state.curvePointDraft[3], 44);
});

test("een geweigerde batch behoudt het nieuwere concept en geeft de save-state vrij", { timeout: 3000 }, async () => {
  const { writes } = await runDelayedCurveSave({ status: 409, edit: () => updateSimpleCurveDraft("level", 50) });
  assert.deepEqual(writes, []);
  assert.equal(state.simpleCurveDraft.level, 50);
  assert.match(state.controlError, /konden niet samen worden bevestigd/);
});

test("de oudere schrijfroutine behoudt nieuwere concepten na een vertraagde 404", { timeout: 3000 }, async () => {
  const { remote, submitted, writes } = await runDelayedCurveSave({ status: 404, edit: () => updateSimpleCurveDraft("level", 50) });
  assert.equal(writes.length, 6);
  assert.deepEqual(CURVE_POINTS.map((point) => remote.get(point.key)), submitted.map((point) => point.value));
  assert.equal(state.simpleCurveDraft.level, 50);
  assert.match(renderSimpleCurvePreview(), /Wijzigingen nog niet opgeslagen/);
});

test("een mislukte oudere schrijfroutine behoudt concepten en geeft de save-state vrij", { timeout: 3000 }, async () => {
  await runDelayedCurveSave({ status: 404, failWrites: true, edit: () => updateSimpleCurveDraft("level", 50) });
  assert.equal(state.simpleCurveDraft.level, 50);
  assert.match(state.controlError, /niet volledig worden toegepast of hersteld/);
});

test("een onverwachte renderfout laat simpleCurveApplying niet hangen", async () => {
  const originalConsoleError = console.error;
  const errors = [];
  state.entities = Object.fromEntries(CURVE_POINTS.map((point) => [point.key, { value: 40 }]));
  state.drafts = {};
  state.simpleCurveDraft = null;
  state.curvePointDraft = null;
  state.simpleCurveApplying = false;
  updateSimpleCurveDraft("level", 45);
  try {
    console.error = (...args) => { errors.push(args); };
    setRenderCallback(() => {
      if (state.simpleCurveApplying) throw new Error("curve render failed");
    });
    handleControlAction("apply-simple-curve", {});
    await Promise.resolve();
    assert.equal(state.simpleCurveApplying, false);
    assert.equal(state.simpleCurveDraft.level, 45);
    assert.match(state.controlError, /curve render failed/);
    assert.equal(errors.length, 1);
  } finally {
    console.error = originalConsoleError;
    setRenderCallback(null);
  }
});

test("batch haalt CSRF-token en schrijft curvepunten onder hetzelfde proxypad", async () => {
  const originalFetch = globalThis.fetch;
  const calls = [];
  window.location.pathname = "/controller/ui/";
  try {
    globalThis.fetch = async (url, options = {}) => {
      calls.push({ url, options });
      if (url === "/controller/ui/auth/status") {
        return { ok: true, json: async () => ({ csrf_token: "test-token" }) };
      }
      if (url === "/controller/ui/openquatt/curve/apply") {
        return { status: 202, json: async () => ({ ok: true, queued: true }) };
      }
      throw new Error(`unexpected request: ${url}`);
    };
    assert.equal(await submitSimpleCurveBatch(generateSimpleCurve(5, 40)), "accepted");
    assert.deepEqual(calls.map(({ url }) => url), [
      "/controller/ui/auth/status",
      "/controller/ui/openquatt/curve/apply",
    ]);
    const body = calls[1].options.body;
    assert.equal(body.get("csrf_token"), "test-token");
    assert.equal(body.get("curveM20"), "50.0");
  } finally {
    globalThis.fetch = originalFetch;
    window.location.pathname = "/";
  }
});

test("Simple genereert precies de zes canonieke curvepunten met begrenzing", () => {
  const points = generateSimpleCurve(5, 40);
  assert.deepEqual(points.map(({ key }) => key), CURVE_POINTS.map(({ key }) => key));
  assert.deepEqual(points.map(({ value }) => value), [50, 45, 40, 37.5, 35, 32.5]);
  assert.equal(generateSimpleCurve(15, 20)[0].value, 50);
  assert.equal(generateSimpleCurve(15, 20)[5].value, 20);
  assert.equal(generateSimpleCurve(Number.NaN, 40), null);
});

test("curvepunten behouden halve graden zonder geladen nummermetadata", () => {
  state.entities = { curve5: { value: 34 } };
  assert.equal(normalizeNumber("curve5", 44.5), 44.5);
  assert.equal(normalizeNumber("curve5", 70.5), 70);
});

test("één editor toont opgeslagen punten, basislijn en handmatige wijzigingen", () => {
  state.simpleCurveDraft = null;
  state.curvePointDraft = null;
  state.entities = Object.fromEntries(CURVE_POINTS.map((point, index) => [point.key, { value: [55, 50, 45, 42.5, 40, 37.5][index] }]));
  assert.deepEqual(getCurvePointDraft().map((point) => point.value), [55, 50, 45, 42.5, 40, 37.5]);
  assert.match(renderSimpleCurvePreview(), /Opgeslagen punten/);
  assert.deepEqual(getSimpleCurveDraft(), { slope: 5, level: 45 });
  assert.equal(updateSimpleCurveDraft("level", "40"), true);
  assert.deepEqual(getCurvePointDraft().map((point) => point.value), [50, 45, 40, 37.5, 35, 32.5]);
  assert.equal(updateCurvePointDraft("curve5", 44), true);
  assert.deepEqual(getCurvePointDraft().map((point) => point.value), [50, 45, 40, 44, 35, 32.5]);
  assert.equal(updateCurvePointDraft("curve5", 44.25), true);
  assert.equal(getCurvePointDraft()[3].value, 44.5);
  assert.match(renderSimpleCurvePreview(), /Wijzigingen nog niet opgeslagen/);
  assert.deepEqual(getSimpleCurveDraft(), { slope: 5, level: 40 });
  const markup = renderSettingsCurveInputs();
  assert.match(markup, /data-oq-action="apply-simple-curve"/);
  assert.match(markup, /oq-simple-curve-workspace/);
  assert.doesNotMatch(markup, /maxWater/);
  assert.doesNotMatch(markup, /data-oq-settings-advanced="curve-points"/);
  assert.equal((markup.match(/oq-helper-curve-svg/g) || []).length, 1);
  for (const point of CURVE_POINTS) assert.match(markup, new RegExp(`data-oq-curve-point-input="${point.key}"`));
  assert.equal(updateSimpleCurveDraft("slope", "6"), true);
  assert.deepEqual(getCurvePointDraft().map((point) => point.value), [52, 46, 40, 37, 34, 31]);
  state.curvePointDraft = null;
  state.simpleCurveDraft = null;
});

test("voorbeeld toont de installatiegrens zonder hogere opgeslagen curvepunten te verbergen", () => {
  assert.ok(INITIAL_SETTINGS_READY_KEY_MAP.heating.includes("maxWater"));
  assert.ok(SETTINGS_GROUP_KEY_MAP.heating.includes("maxWater"));
  state.curvePointDraft = null;
  state.simpleCurveDraft = { slope: 15, level: 40 };
  state.entities = { maxWater: { value: 60 } };
  const preview = renderSimpleCurvePreview();
  assert.match(preview, /value="70\.0" data-oq-curve-point-input="curveM20"/);
  assert.match(preview, /begrensd 60\.0°/);
  assert.match(preview, /stroke-dasharray="4 4"/);
  assert.match(preview, />70°C<\/text>/);
  assert.doesNotMatch(preview, /<text[^>]*>−20°C<\/text>/);
  assert.match(renderSettingsCurveInputs(), /Begrensd op 60 °C uit Installatie/);
  assert.equal(generateSimpleCurve(15, 40)[0].value, 70);
  state.simpleCurveDraft = { slope: 5, level: 54 };
  const line = renderSimpleCurvePreview().match(/<polyline points="([^"]+)" class="oq-simple-curve-line" \/>/)?.[1];
  assert.ok(line);
  const positions = line.split(" ").map((position) => position.split(",").map(Number));
  assert.equal(positions.length, 7);
  assert.equal(positions[0][1], positions[1][1]);
  assert.ok(positions[0][0] < positions[1][0] && positions[1][0] < positions[2][0]);
  assert.match(preview, /class="oq-simple-curve-chart oq-helper-curve-svg"/);
  assert.match(preview, /data-curve-key="curveM20"/);
  assert.doesNotMatch(preview, /is-zero/);
  state.simpleCurveDraft = null;
});

test("de gebouwde firmwarebundel bevat de diagnostische vertalingen", () => {
  const bundle = readFileSync(new URL("../js/openquatt-app.js", import.meta.url), "utf8");
  for (const label of ["Basisdoel", "Externe offset", "Kamercorrectie", "Effectief doel"]) {
    assert.ok(bundle.includes(label), `${label} ontbreekt in de compacte bundel`);
  }
});

test("Simple herstelt alle punten na een deels geaccepteerde maar onbevestigde schrijfopdracht", async () => {
  const originals = [55, 50, 45, 42.5, 40, 37.5];
  const remote = new Map(CURVE_POINTS.map((point, index) => [point.key, originals[index]]));
  const writes = [];
  const result = await applySimpleCurvePoints(generateSimpleCurve(5, 40), originals, async (key, value) => {
    writes.push([key, value]);
    remote.set(key, value);
    return writes.length !== 2;
  }, (key) => remote.get(key));
  assert.deepEqual(result, { applied: false, restored: true });
  assert.deepEqual(CURVE_POINTS.map((point) => remote.get(point.key)), originals);
  assert.equal(writes.length, 8);
});

test("Simple meldt onzeker herstel als een terugschrijfopdracht faalt", async () => {
  const originals = [55, 50, 45, 42.5, 40, 37.5];
  const remote = new Map(CURVE_POINTS.map((point, index) => [point.key, originals[index]]));
  let writes = 0;
  const result = await applySimpleCurvePoints(generateSimpleCurve(5, 40), originals, async (key, value) => {
    writes += 1;
    if (writes === 3) return false;
    remote.set(key, value);
    return writes !== 2;
  }, (key) => remote.get(key));
  assert.deepEqual(result, { applied: false, restored: false });
});

test("batch past alle zes curvepunten met één verzoek toe en bevestigt de teruglezing", async () => {
  const points = generateSimpleCurve(5, 40);
  const remote = new Map();
  let submissions = 0;
  let refreshes = 0;
  const result = await applySimpleCurveBatch(points, async (values) => {
    submissions += 1;
    for (const point of values) remote.set(point.key, point.value);
    return "accepted";
  }, async () => { refreshes += 1; }, (key) => remote.get(key), async () => {});
  assert.deepEqual(result, { applied: true, unsupported: false });
  assert.equal(submissions, 1);
  assert.equal(refreshes, 1);
});

test("batch controleert ook na een verloren antwoord en meldt onzekere gedeeltelijke toestand", async () => {
  const points = generateSimpleCurve(5, 40);
  const remote = new Map();
  const recovered = await applySimpleCurveBatch(points, async () => {
    for (const point of points) remote.set(point.key, point.value);
    throw new Error("lost acknowledgement");
  }, async () => {}, (key) => remote.get(key), async () => {});
  assert.equal(recovered.applied, true);

  remote.delete(points[5].key);
  let refreshes = 0;
  const uncertain = await applySimpleCurveBatch(points, async () => "accepted",
    async () => { refreshes += 1; }, (key) => remote.get(key), async () => {});
  assert.deepEqual(uncertain, { applied: false, unsupported: false });
  assert.equal(refreshes, 5);
  assert.equal(remote.has(points[5].key), false);
});

test("ontbrekende batchendpoint schakelt expliciet over naar de oudere route", async () => {
  const result = await applySimpleCurveBatch(generateSimpleCurve(5, 40), async () => "unsupported",
    async () => { throw new Error("refresh should not run"); }, () => NaN, async () => {});
  assert.deepEqual(result, { applied: false, unsupported: true });
});
