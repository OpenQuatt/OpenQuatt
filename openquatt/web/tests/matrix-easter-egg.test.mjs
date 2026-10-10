import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  innerWidth: 1200,
  innerHeight: 800,
  localStorage: { getItem: () => null },
  matchMedia: () => ({ matches: false }),
};

const matrix = await import("../js/src/features/matrix-easter-egg.js");
const { state } = await import("../js/src/core/state.js");
const { handleShellAction } = await import("../js/src/features/shell-actions.js");

function setEntities(entities) {
  state.lastKnownInstallationTopology = "";
  state.entities = {
    installationTopology: { value: entities.hp2Freq ? "duo" : "single" },
    hp1Mode: { value: "Heating" },
    hp1Defrost: { value: false },
    ...(entities.hp2Freq ? { hp2Mode: { value: "Standby" }, hp2Defrost: { value: false } } : {}),
    ...entities,
  };
  state.drafts = {};
}

function createClassList() {
  const names = new Set();
  return {
    add: (...all) => { all.forEach((name) => names.add(name)); },
    remove: (...all) => { all.forEach((name) => names.delete(name)); },
    contains: (name) => names.has(name),
  };
}

function createElementStub(tag, hooks = {}) {
  const childStubs = new Map();
  let storedHtml = "";
  return {
    tag,
    className: "",
    classList: createClassList(),
    attributes: {},
    children: [],
    removed: false,
    hidden: false,
    textContent: "",
    dataset: {},
    handlers: {},
    get innerHTML() { return storedHtml; },
    set innerHTML(value) { storedHtml = String(value); },
    setAttribute(name, value) { this.attributes[name] = value; },
    removeAttribute(name) { delete this.attributes[name]; },
    appendChild(child) { this.children.push(child); },
    addEventListener(type, handler) { this.handlers[type] = handler; },
    removeEventListener(type) { delete this.handlers[type]; },
    insertAdjacentElement(pos, el) { this.children.push(el); return el; },
    remove() { this.removed = true; },
    closest() { return null; },
    querySelector(selector) {
      if (!childStubs.has(selector)) {
        childStubs.set(selector, createElementStub("div"));
      }
      return childStubs.get(selector);
    },
    querySelectorAll() { return []; },
    getContext: () => hooks.getContext?.() ?? null,
  };
}

function createCanvasContextStub() {
  return {
    calls: [],
    fillText(...args) { this.calls.push(["text", ...args]); },
    fillRect(...args) { this.calls.push(["rect", ...args]); },
  };
}

function installDomStubs() {
  const listeners = new Map();
  const nodes = [];
  const body = { classList: createClassList(), children: [], appendChild(child) { this.children.push(child); } };
  const originalDocument = globalThis.document;
  const originalSetTimeout = globalThis.setTimeout;
  const originalClearTimeout = globalThis.clearTimeout;
  const originalSetInterval = globalThis.setInterval;
  const originalClearInterval = globalThis.clearInterval;
  const timeouts = [];
  const intervals = [];
  const canvasCtx = createCanvasContextStub();
  globalThis.document = {
    body,
    createElement: (tag) => {
      const node = createElementStub(tag, tag === "canvas" ? { getContext: () => canvasCtx } : {});
      nodes.push(node);
      return node;
    },
    addEventListener: (type, handler) => { listeners.set(type, handler); },
    removeEventListener: (type) => { listeners.delete(type); },
  };
  globalThis.setTimeout = (handler) => {
    timeouts.push(handler);
    return timeouts.length;
  };
  globalThis.clearTimeout = () => {};
  globalThis.setInterval = (handler) => {
    intervals.push(handler);
    return intervals.length;
  };
  globalThis.clearInterval = () => {};
  return {
    body,
    nodes,
    listeners,
    timeouts,
    intervals,
    canvasCtx,
    restore() {
      matrix.stopMatrixMode();
      if (originalDocument === undefined) {
        delete globalThis.document;
      } else {
        globalThis.document = originalDocument;
      }
      globalThis.setTimeout = originalSetTimeout;
      globalThis.clearTimeout = originalClearTimeout;
      globalThis.setInterval = originalSetInterval;
      globalThis.clearInterval = originalClearInterval;
      setEntities({});
    },
  };
}

test("matrix tapreeks heeft vijf snelle taps nodig", () => {
  assert.equal(matrix.filterMatrixTaps([0, 500, 1000, 1500], 1500).length, 4);
  assert.equal(matrix.filterMatrixTaps([0, 500, 1000, 1500, 2000], 2000).length, 5);
  assert.equal(matrix.filterMatrixTaps([0, 500, 1000, 1500, 5000], 5000).length, 1);
});

test("matrix snapshot leest boilerActive en control mode", () => {
  setEntities({
    totalCop: { value: 5.6 },
    hp1Freq: { value: 23 },
    hp2Freq: { value: 0 },
    boilerActive: { state: "on" },
    controlModeLabel: { state: "CM1" },
  });
  const snapshot = matrix.readMatrixSnapshot();
  assert.ok(Number.isNaN(snapshot.cop));
  assert.equal(snapshot.hz, 23);
  assert.equal(snapshot.boilerActive, true);
  assert.equal(snapshot.boilerOff, false);
  assert.equal(snapshot.controlMode, "CM1");
  setEntities({
    totalCop: { value: 3.2 },
    hp1Freq: { value: 0 },
    controlModeLabel: { state: "Heating" },
  });
  const idle = matrix.readMatrixSnapshot();
  assert.equal(idle.boilerActive, false);
  assert.equal(idle.boilerOff, true);
  setEntities({});
});

test("matrix legacy helpers zijn verwijderd (Director's Cut)", () => {
  assert.equal(matrix.collectMatrixQuotes, undefined);
  assert.equal(matrix.selectMatrixQuote, undefined);
});

test("vijf logo-taps via de shell-action activeren matrix mode", (t) => {
  const dom = installDomStubs();
  t.after(() => dom.restore());
  setEntities({});

  for (let i = 0; i < 4; i += 1) {
    assert.equal(handleShellAction("matrix-version-tap", {}), true);
    assert.equal(matrix.isMatrixModeActive(), false);
  }
  assert.equal(handleShellAction("matrix-version-tap", {}), true);
  assert.equal(matrix.isMatrixModeActive(), true);
});

test("matrix failsafe overleeft de pil-keuze (scene-transitie)", (t) => {
  const dom = installDomStubs();
  t.after(() => dom.restore());
  setEntities({});
  assert.equal(matrix.isMatrixFailsafeArmed(), false);
  matrix.startMatrixMode();
  assert.equal(matrix.isMatrixFailsafeArmed(), true);
  // Alleen de intro-timer (pills tonen) afwerken, failsafe/no-choice laten staan.
  const intro = dom.timeouts.shift();
  intro();
  const overlay = dom.nodes.find((node) => (node.className || "").includes("oq-matrix-egg-quote"));
  assert.ok(overlay && overlay.handlers.click);
  const pillTarget = { closest: (sel) => (sel === "[data-oq-matrix-pill]" ? { dataset: { oqMatrixPill: "red" } } : null) };
  overlay.handlers.click({ target: pillTarget });
  // Rode keuze loopt én de 45 s-failsafe staat nog steeds gewapend.
  assert.equal(matrix.isMatrixModeActive(), true);
  assert.equal(matrix.isMatrixFailsafeArmed(), true);
  // Escape ruimt ook de failsafe op.
  dom.listeners.get("keydown")({ key: "Escape" });
  assert.equal(matrix.isMatrixFailsafeArmed(), false);
  assert.equal(matrix.isMatrixModeActive(), false);
});

test("matrix mode bouwt overlay zonder kicker en ruimt alles op", (t) => {
  const dom = installDomStubs();
  t.after(() => dom.restore());
  setEntities({ totalCop: { value: 4.1 } });

  matrix.registerMatrixVersionTap(0);
  assert.equal(matrix.isMatrixModeActive(), false);
  matrix.registerMatrixVersionTap(400);
  matrix.registerMatrixVersionTap(800);
  matrix.registerMatrixVersionTap(1200);
  matrix.registerMatrixVersionTap(1600);
  assert.equal(matrix.isMatrixModeActive(), true);
  assert.equal(dom.body.classList.contains("oq-matrix-active"), true);
  const quote = dom.nodes.find((node) => (node.className || "").includes("oq-matrix-egg-quote"));
  assert.ok(quote);
  assert.equal(quote.attributes.role, "dialog");
  assert.ok(quote.innerHTML.includes("data-oq-matrix-main"));
  assert.ok(!quote.innerHTML.includes("Matrix mode"));
  assert.ok(quote.className.includes("oq-matrix-egg-quote--bare"));

  dom.listeners.get("keydown")({ key: "Escape" });
  assert.equal(matrix.isMatrixModeActive(), false);
  assert.equal(dom.body.classList.contains("oq-matrix-active"), false);
  assert.equal(dom.listeners.has("keydown"), false);
});

test("matrix rain tekent vallende Digital-rain-glyphs", (t) => {
  const dom = installDomStubs();
  t.after(() => dom.restore());
  setEntities({});

  matrix.startMatrixMode();
  const rain = dom.nodes.find((node) => node.className === "oq-matrix-egg-rain");
  assert.ok(rain);
  assert.equal(dom.body.children.includes(rain), true);
  assert.ok(dom.intervals.length >= 1);
  const rainTick = [...dom.intervals].find((handler) => {
    const before = dom.canvasCtx.calls.length;
    handler();
    const produced = dom.canvasCtx.calls.slice(before).some((call) => call[0] === "text");
    if (!produced) {
      dom.canvasCtx.calls.length = before;
      return false;
    }
    return true;
  });
  assert.ok(rainTick);
  const afterFirst = dom.canvasCtx.calls.length;
  const firstGlyphs = dom.canvasCtx.calls.filter((call) => call[0] === "text").map((call) => call[1]);
  const firstYs = dom.canvasCtx.calls.filter((call) => call[0] === "text").map((call) => call[3]);
  assert.ok(firstYs.length >= 16);
  assert.ok(firstGlyphs.every((glyph) => matrix.MATRIX_GLYPHS.includes(glyph)));
  rainTick();
  const secondYs = dom.canvasCtx.calls.slice(afterFirst).filter((call) => call[0] === "text").map((call) => call[3]);
  assert.ok(secondYs.length >= 16);
  assert.ok(Math.max(...secondYs) > Math.max(...firstYs));
});

test("matrix glyphs komen uit het Digital-rain-set", () => {
  assert.ok(matrix.MATRIX_GLYPHS.includes("0"));
  assert.ok(matrix.MATRIX_GLYPHS.length > 16);
  for (let i = 0; i < 20; i += 1) {
    const glyph = matrix.pickMatrixGlyph();
    assert.equal(typeof glyph, "string");
    assert.equal([...glyph].length, 1);
    assert.ok(matrix.MATRIX_GLYPHS.includes(glyph));
  }
});

test("matrix takeover pulses liggen uitgelijnd met de takeover lines", () => {
  setEntities({ hp1Freq: { value: 23 }, flowSelected: { value: 1008 }, totalCop: { value: 5.4 }, controlModeLabel: { state: "CM1" } });
  const snap = matrix.readMatrixSnapshot();
  const lines = matrix.buildMatrixTakeoverLines(snap);
  const pulses = matrix.takeoverPulses(snap);
  assert.equal(lines.length, pulses.length);
  assert.equal(pulses[0], null);
  assert.equal(pulses[1], "hp1");
  assert.ok(pulses.includes("flow"));
  assert.ok(pulses.includes("cop"));
  assert.equal(pulses[pulses.length - 1], null);
  setEntities({ hp1Freq: { value: 23 }, hp2Freq: { value: 23 }, totalCop: { value: 3 } });
  state.entities.hp2Freq = { value: 23 };
  assert.equal(matrix.takeoverPulses(matrix.readMatrixSnapshot())[2], "hp2");
  setEntities({});
});

test("matrix scramble behoudt spaties en punten", () => {
  const out = matrix.scrambleText("HP1 ................. 23 Hz");
  assert.equal([...out].length, [..."HP1 ................. 23 Hz"].length);
  assert.ok(out.includes("................."));
  assert.ok(out.includes(" "));
});

test("matrix reality shift triggert alleen bij duidelijke verandering", () => {
  const a = { hz: 23, hz1: 23, hz2: 0, cop: 4.1 };
  assert.equal(matrix.detectRealityShift(a, { hz: 24, hz1: 24, hz2: 0, cop: 4.1 }), null);
  const shift = matrix.detectRealityShift(a, { hz: 27, hz1: 27, hz2: 0, cop: 4.2 });
  assert.equal(shift.target, "hp1");
  assert.ok(shift.from.includes("23") && shift.to.includes("27"));
  const copShift = matrix.detectRealityShift(a, { hz: 23, hz1: 23, hz2: 0, cop: 5.5 });
  assert.equal(copShift.target, "cop");
  assert.equal(matrix.detectRealityShift(null, a), null);
});

test("matrix 42 Hz special is ultra-rare en exact", () => {
  assert.equal(matrix.pickSpecialKind({ hz: 42, hz1: 42, hz2: 0, cop: 4 }), "answer42");
  assert.equal(matrix.pickSpecialKind({ hz: 41.4, hz1: 41.4, hz2: 0, cop: 4 }), null);
  assert.equal(matrix.pickSpecialKind({ hz: 0, hz1: 0, hz2: 0, cop: 4 }), null);
});

test("matrix punch-varianten blijven strings", () => {
  assert.ok(String(matrix.twinsPunch()).length > 0);
  assert.ok(String(matrix.cm0Punch()).length > 0);
  assert.ok(String(matrix.coolingPunch()).length > 0);
  const prophecy = new Set(Array.from({ length: 40 }, () => matrix.copPunch({ cop: 5.8, duo: true, hz1: 27, hz2: 27 })));
  assert.ok(prophecy.has("The prophecy was true."));
});

test("matrix takeover toont echte live data met ODU en control mode", () => {
  setEntities({
    hp1Freq: { value: 23 },
    hp2Freq: { value: 23 },
    flowSelected: { value: 1008 },
    totalCop: { value: 5.4 },
    controlModeLabel: { state: "CM1" },
  });
  const lines = matrix.buildMatrixTakeoverLines(matrix.readMatrixSnapshot());
  const joined = lines.join("\n");
  assert.ok(joined.includes("ODU LINK") && joined.includes("ONLINE"));
  assert.ok(joined.includes("HP1") && joined.includes("23 Hz"));
  assert.ok(joined.includes("HP2") && joined.includes("23 Hz"));
  assert.ok(joined.includes("COP") && !joined.includes("5.4"));
  assert.ok(joined.includes("FLOW") && joined.includes("16.8"));
  assert.ok(joined.includes("CONTROL MODE") && joined.includes("CM1"));
  assert.ok(lines.length >= 5 && lines.length <= 6);
  setEntities({});
});

test("matrix truth branches volgen prioriteit defrost > twins > cop > lowhz", () => {
  setEntities({ hp1Defrost: { state: "on" }, hp1Freq: { value: 27 }, hp2Freq: { value: 27 }, totalCop: { value: 5.8 } });
  assert.equal(matrix.pickTruthBranch(matrix.readMatrixSnapshot()), "defrost");
  setEntities({ hp1Freq: { value: 27 }, hp2Freq: { value: 27 }, totalCop: { value: 5.8 } });
  state.entities.hp2Freq = { value: 27 };
  assert.equal(matrix.pickTruthBranch(matrix.readMatrixSnapshot()), "twins");
  setEntities({ hp1Freq: { value: 45 }, totalCop: { value: 5.8 } });
  assert.equal(matrix.pickTruthBranch(matrix.readMatrixSnapshot()), "cop");
  setEntities({ hp1Freq: { value: 23 }, totalCop: { value: 3.1 } });
  assert.equal(matrix.pickTruthBranch(matrix.readMatrixSnapshot()), "lowhz");
  setEntities({ controlModeLabel: { state: "CM0" } });
  assert.equal(matrix.pickTruthBranch(matrix.readMatrixSnapshot()), "cm0");
  setEntities({ controlModeLabel: { state: "Cooling" } });
  assert.equal(matrix.pickTruthBranch(matrix.readMatrixSnapshot()), "cooling");
  setEntities({ hp1Freq: { value: 55 }, totalCop: { value: 3.1 }, controlModeLabel: { state: "CM1" } });
  assert.equal(matrix.pickTruthBranch(matrix.readMatrixSnapshot()), "generic");
  setEntities({});
});

test("matrix bonuspunch toont lowhz en cop samen", () => {
  setEntities({ hp1Freq: { value: 23 }, totalCop: { value: 5.8 } });
  const snap = matrix.readMatrixSnapshot();
  assert.equal(matrix.pickTruthBranch(snap), "cop");
  assert.equal(matrix.pickBonusPunch(snap, "cop"), "lowhz");
  assert.equal(matrix.pickBonusPunch(snap, "lowhz"), "cop");
  setEntities({ hp1Freq: { value: 23 }, totalCop: { value: 3.1 } });
  assert.equal(matrix.pickBonusPunch(matrix.readMatrixSnapshot(), "lowhz"), null);
  setEntities({ hp1Freq: { value: 27 }, hp2Freq: { value: 27 }, totalCop: { value: 5.8 } });
  state.entities.hp2Freq = { value: 27 };
  assert.equal(matrix.pickBonusPunch(matrix.readMatrixSnapshot(), "twins"), "lowhz");
  assert.ok(String(matrix.copPunch({ cop: 5.8 })).length > 0);
  setEntities({});
});

test("matrix boiler punch en generic pool hebben variatie", () => {
  const generics = new Set(Array.from({ length: 30 }, () => matrix.pickGenericPunch()));
  assert.ok(generics.size >= 4);
  assert.ok(![...generics].some((g) => g === "There is no boiler."));
  const boilers = new Set(Array.from({ length: 20 }, () => matrix.pickBoilerPunch()));
  assert.ok(boilers.size >= 2);
});

test("matrix anomaly kans is ~20% met vijf varianten", () => {
  assert.equal(matrix.MATRIX_ANOMALY_CHANCE, 0.2);
  const origRandom = Math.random;
  Math.random = () => 0.5;
  try {
    assert.equal(matrix.pickAnomalyKind(), -1);
  } finally {
    Math.random = origRandom;
  }
  Math.random = () => 0.05;
  try {
    const kind = matrix.pickAnomalyKind();
    assert.ok(kind >= 0 && kind <= 4);
  } finally {
    Math.random = origRandom;
  }
});

test("matrix mode stopt automatisch na de ingestelde duur", (t) => {
  const dom = installDomStubs();
  t.after(() => dom.restore());
  setEntities({});

  matrix.startMatrixMode();
  assert.equal(matrix.isMatrixModeActive(), true);
  assert.ok(dom.timeouts.length >= 2);
  let guard = 0;
  while (dom.timeouts.length && guard < 120) {
    const batch = [...dom.timeouts];
    dom.timeouts.length = 0;
    batch.forEach((handler) => handler());
    guard += 1;
  }
  assert.equal(matrix.isMatrixModeActive(), false);
  assert.equal(dom.body.classList.contains("oq-matrix-active"), false);
});

test("matrix mode toont rain ook bij reduced motion (spec: geen variant)", (t) => {
  const dom = installDomStubs();
  t.after(() => {
    window.matchMedia = () => ({ matches: false });
    dom.restore();
  });
  window.matchMedia = () => ({ matches: true });
  setEntities({});

  matrix.startMatrixMode();
  assert.equal(matrix.isMatrixModeActive(), true);
  assert.equal(dom.nodes.some((node) => node.className === "oq-matrix-egg-rain"), true);
  assert.equal(dom.nodes.some((node) => (node.className || "").includes("oq-matrix-egg-quote")), true);
});
