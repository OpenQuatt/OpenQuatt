import { getDerivedEfficiencyValue, getEntityNumericValue, getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";

export const MATRIX_TAP_COUNT = 5;
export const MATRIX_TAP_WINDOW_MS = 2500;
export const MATRIX_ACTIVE_MS = 45000;
export const MATRIX_ANOMALY_CHANCE = 0.2;
export const MATRIX_GLYPHS = "ｱｲｳｴｵｶｷｸｹｺｻｼ0123456789ABCDEFZ$#";
const MATRIX_RAIN_INTERVAL_MS = 100;

let matrixTapTimes = [];
let matrixRain = null;
let matrixOverlay = null;
let matrixRainTimer = 0;
let matrixTimers = [];
let matrixKeyHandler = null;
let matrixTypewriter = 0;
let matrixChoiceMade = false;
let matrixRainMode = "green";
let matrixRainTick = 0;
let matrixWakeArmed = false;
let matrixSecretDone = false;
let matrixOrigY = 0;
let matrixScrolled = false;
let matrixUserScrolled = false;
let matrixScrollHandler = null;
let matrixFailsafe = 0;
let matrixHadShift = false;
let matrixHadAgent = false;

export function filterMatrixTaps(times, now) {
  return (Array.isArray(times) ? times : []).filter((tap) => typeof tap === "number" && now - tap <= MATRIX_TAP_WINDOW_MS);
}

export function registerMatrixVersionTap(now = Date.now()) {
  if (isMatrixModeActive()) return;
  matrixTapTimes = filterMatrixTaps([...matrixTapTimes, now], now);
  if (matrixTapTimes.length >= MATRIX_TAP_COUNT) {
    matrixTapTimes = [];
    startMatrixMode();
  }
}

export function readMatrixSnapshot() {
  const cop = getDerivedEfficiencyValue("totalCop");
  const hz1 = getEntityNumericValue("hp1Freq");
  const hz2 = getEntityNumericValue("hp2Freq");
  const hz = [hz1, hz2].filter((v) => !Number.isNaN(v) && v > 0);
  const modeLabel = String(getEntityStateText("controlModeLabel", "")).toLowerCase();
  const flowRaw = getEntityNumericValue("flowSelected");
  const flow = Number.isNaN(flowRaw) ? getEntityNumericValue("hp1Flow") : flowRaw;
  const boilerActive = hasEntity("boilerActive") && isEntityActive("boilerActive");
  return {
    cop,
    hz: hz.length ? Math.max(...hz) : 0,
    hz1: Number.isNaN(hz1) ? 0 : hz1,
    hz2: Number.isNaN(hz2) ? 0 : hz2,
    flow: Number.isNaN(flow) ? Number.NaN : flow,
    duo: hasEntity("hp2Freq"),
    boilerActive,
    boilerOff: !boilerActive,
    cooling: modeLabel.includes("cm5") || modeLabel.includes("cooling") || modeLabel.includes("koeling"),
    defrost: isEntityActive("hp1Defrost") || isEntityActive("hp2Defrost"),
    cm0: modeLabel.includes("cm0"),
    controlMode: String(getEntityStateText("controlModeLabel", "")).trim() || "—",
  };
}

export function pickMatrixGlyph() {
  return MATRIX_GLYPHS[(Math.random() * MATRIX_GLYPHS.length) | 0];
}

const hzLabel = (v) => (v > 0 ? `${Math.round(v)} Hz` : "STANDBY");

export function buildMatrixTakeoverLines(snap) {
  const s = snap || readMatrixSnapshot();
  const lines = ["ODU LINK .............. ONLINE", `HP1 ................... ${hzLabel(s.hz1)}`];
  if (s.duo) lines.push(`HP2 ................... ${hzLabel(s.hz2)}`);
  lines.push(!Number.isNaN(s.flow) && s.flow > 0 ? `FLOW .................. ${(s.flow / 60).toFixed(1)} L/min` : "FLOW .................. —");
  lines.push(`COP ................... ${Number.isNaN(s.cop) ? "—" : s.cop.toFixed(1)}`);
  lines.push(`CONTROL MODE .......... ${s.controlMode || "—"}`);
  return lines.slice(0, 6);
}

// ACT III branchkeuze: defrost > twins > cop >= 5 > <30Hz > cm0 > cooling > generic.
// Boiler loopt als interrupt (Agent Boiler) vóór de truth, zodat beide grappen kunnen vallen.
export function pickTruthBranch(snap) {
  const s = snap || readMatrixSnapshot();
  if (s.defrost) return "defrost";
  if (s.duo && s.hz1 > 0 && s.hz1 === s.hz2) return "twins";
  if (!Number.isNaN(s.cop) && s.cop >= 5) return "cop";
  if (s.hz > 0 && s.hz < 30) return "lowhz";
  if (s.cm0) return "cm0";
  if (s.cooling) return "cooling";
  return "generic";
}

// Bonuspunch: laagtoeren (<30Hz) en hoge COP vallen in de praktijk vaak
// samen. Zonder dit zou de ene grap de andere altijd verbergen (cop won
// van lowhz). Max één bonus, zodat de run niet te lang wordt.
export function pickBonusPunch(snap, branch) {
  const s = snap || readMatrixSnapshot();
  const low = s.hz > 0 && s.hz < 30 && branch !== "lowhz";
  const cop = !Number.isNaN(s.cop) && s.cop >= 5 && branch !== "cop";
  if ((branch === "cop" || branch === "twins") && low) return "lowhz";
  if ((branch === "lowhz" || branch === "twins") && cop) return "cop";
  return null;
}

export function copPunch(snap) {
  const s = snap || {};
  const cop = copText(s, "5.8");
  const pool = ["Follow the green COP.", `COP ${cop}\n\nThis is getting ridiculous.`, "Efficiency detected.\n\nProbably illegal in another timeline."];
  if (s.duo && s.hz1 > 0 && s.hz1 === s.hz2) pool.push("The prophecy was true.");
  return pool[(Math.random() * pool.length) | 0];
}

const pickOf = (a, b) => (Math.random() < 0.5 ? a : b);
const copText = (snap, fallback) => (Number.isNaN(snap.cop) ? fallback : snap.cop.toFixed(1));
const callDone = (done) => () => done?.();

export function twinsPunch() {
  return pickOf("Two compressors.\nOne reality.", "The twins are awake.");
}

export function cm0Punch() {
  return pickOf("Nothing to control.", "The system is sleeping.\n\nDo not disturb.");
}

export function coolingPunch() {
  return pickOf("Welcome to the cool side.", "There is no heat.\n\nOnly cool.");
}

const MATRIX_GENERIC_POOL = [
  "I know Modbus.",
  "Free your heat pump.",
  "The Matrix has you.",
  "There is no cloud.",
  "Stay in the flow.",
  "The system is yours.",
  "Follow the green COP.",
  "Wake up, Quatt...",
];

export function pickGenericPunch() {
  return MATRIX_GENERIC_POOL[(Math.random() * MATRIX_GENERIC_POOL.length) | 0];
}

export function pickBoilerPunch() {
  const p = ["Do not engage.\nIt already looks expensive.", "Keep your eye on him.", "He's not supposed to be here.", "Typical."];
  return p[(Math.random() * p.length) | 0];
}

// ACT IV: ~20% kans op één van vijf anomalies (dus ~4% per specifieke grap)
export function pickAnomalyKind() {
  if (Math.random() >= MATRIX_ANOMALY_CHANCE) return -1;
  return (Math.random() * 5) | 0;
}

export function isMatrixModeActive() {
  if (typeof document === "undefined") return false;
  return Boolean(matrixRain || matrixOverlay);
}

export function isMatrixFailsafeArmed() {
  return matrixFailsafe !== 0;
}

function later(ms, fn) {
  matrixTimers.push(setTimeout(fn, ms));
}

// Sequentiele scene-runner: [[wachttijd, stap], ...]. Een stap zonder
// argument gaat automatisch verder; een stap met (next) roept next() zelf
// aan zodra zijn async werk (reveal/scramble) klaar is. Stoppen = timers
// clearen, daarna loopt geen enkele scene meer verder.
function runScene(steps) {
  let i = 0;
  const next = () => {
    if (!matrixOverlay || i >= steps.length) return;
    const [ms, fn] = steps[i++];
    later(ms, () => {
      if (!matrixOverlay) return;
      if (fn.length > 0) fn(next);
      else { fn(); next(); }
    });
  };
  next();
}

function matrixBody() {
  return typeof document !== "undefined" ? document.body : null;
}

function stopMatrixRain() {
  if (matrixRainTimer) { clearInterval(matrixRainTimer); matrixRainTimer = 0; }
}

export function stopMatrixMode() {
  matrixTimers.forEach((t) => clearTimeout(t));
  matrixTimers = [];
  if (matrixTypewriter) { clearInterval(matrixTypewriter); matrixTypewriter = 0; }
  if (matrixFailsafe) { clearTimeout(matrixFailsafe); matrixFailsafe = 0; }
  stopMatrixRain();
  matrixTapTimes = [];
  matrixChoiceMade = false;
  matrixRainMode = "green";
  matrixWakeArmed = false;
  matrixSecretDone = false;
  matrixScrolled = false;
  matrixUserScrolled = false;
  matrixOrigY = 0;
  matrixHadShift = false;
  matrixHadAgent = false;
  if (typeof window !== "undefined" && window.removeEventListener && matrixScrollHandler) {
    window.removeEventListener("wheel", matrixScrollHandler);
    window.removeEventListener("touchmove", matrixScrollHandler);
  }
  matrixScrollHandler = null;
  if (typeof document === "undefined") { matrixRain = null; matrixOverlay = null; matrixKeyHandler = null; return; }
  if (matrixKeyHandler) { document.removeEventListener("keydown", matrixKeyHandler); matrixKeyHandler = null; }
  if (matrixRain) { matrixRain.remove(); matrixRain = null; }
  if (matrixOverlay) { matrixOverlay.remove(); matrixOverlay = null; }
  if (document.body) document.body.classList.remove("oq-matrix-active", "oq-matrix-flash-red", "oq-matrix-boot", "oq-matrix-wave", "oq-matrix-spotlight", "oq-matrix-freeze");
  clearMatrixPulses();
}

function setRainMode(mode) {
  matrixRainMode = mode;
}

function flashRed() {
  if (!matrixBody()) return;
  setRainMode("red");
  matrixBody().classList.add("oq-matrix-flash-red");
  later(300, () => {
    if (matrixBody()) matrixBody().classList.remove("oq-matrix-flash-red");
    if (matrixRainMode === "red") setRainMode("green");
  });
}

function createMatrixRain() {
  const c = document.createElement("canvas");
  c.className = "oq-matrix-egg-rain";
  c.setAttribute("aria-hidden", "true");
  document.body.appendChild(c);
  const ctx = c.getContext("2d");
  if (!ctx) return c;
  const fontPx = 14;
  const w = window.innerWidth || 0;
  const h = window.innerHeight || 0;
  c.width = w; c.height = h;
  const cols = Math.max(16, Math.min(28, Math.floor(w / 56) || 16));
  const drops = Array.from({ length: cols }, () => Math.random() * (h / fontPx));
  ctx.font = `bold ${fontPx}px ui-monospace,SFMono-Regular,Menlo,Monaco,Consolas,monospace`;
  const tick = () => {
    matrixRainTick += 1;
    const mode = matrixRainMode;
    const fast = mode === "fast";
    const thin = mode === "thin";
    const frozen = mode === "freeze";
    const reverse = mode === "reverse";
    let dim = "rgba(0,133,77,0.45)";
    let bright = "#00854d";
    if (mode === "blue") { dim = "rgba(59,130,246,0.4)"; bright = "#60a5fa"; }
    else if (mode === "red") { dim = "rgba(239,68,68,0.45)"; bright = "#fca5a5"; }
    ctx.globalCompositeOperation = "destination-out";
    ctx.fillStyle = "rgba(0,0,0,0.18)";
    ctx.fillRect(0, 0, w, h);
    ctx.globalCompositeOperation = "source-over";
    const step = w / cols;
    for (let i = 0; i < cols; i++) {
      if (thin && ((i + matrixRainTick) % 2 === 0)) { drops[i] += fast ? 2 : 1; continue; }
      const x = i * step;
      const y = drops[i] * fontPx;
      ctx.fillStyle = dim;
      ctx.fillText(pickMatrixGlyph(), x, y - fontPx * 2);
      ctx.fillStyle = bright;
      ctx.fillText(pickMatrixGlyph(), x, y);
      if (drops[i] * fontPx > h && Math.random() > 0.976) drops[i] = 0;
      if (reverse) { drops[i] -= 1; if (drops[i] < 0) drops[i] = h / fontPx; }
      else if (frozen) { /* stilte voor de finale */ }
      else if (mode === "blue" && matrixRainTick % 2 === 0) drops[i] += 0.5;
      else drops[i] += fast ? 2 : 1;
    }
  };
  tick();
  matrixRainTimer = setInterval(tick, MATRIX_RAIN_INTERVAL_MS);
  return c;
}

function typewriteLine(target, text, speed = 35) {
  if (!target) return;
  let i = 0;
  target.textContent = "";
  if (matrixTypewriter) clearInterval(matrixTypewriter);
  matrixTypewriter = setInterval(() => {
    i += 1;
    target.textContent = text.slice(0, i) + (i < text.length ? "█" : "");
    if (i >= text.length) { clearInterval(matrixTypewriter); matrixTypewriter = 0; target.textContent = text; }
  }, speed);
}

function mainEl() { return matrixOverlay?.querySelector("[data-oq-matrix-main]"); }
function termEl() { return matrixOverlay?.querySelector("[data-oq-matrix-term]"); }

function setOverlayMode(mode) {
  if (!matrixOverlay) return;
  matrixOverlay.classList.remove("oq-matrix-egg-quote--bare", "oq-matrix-egg-quote--term", "oq-matrix-egg-quote--warn");
  matrixOverlay.classList.add(mode === "warn" ? "oq-matrix-egg-quote--warn" : mode === "term" ? "oq-matrix-egg-quote--term" : "oq-matrix-egg-quote--bare");
}

function showMain(text, mode = "bare") {
  const m = mainEl();
  const t = termEl();
  setOverlayMode(mode);
  if (t && mode === "bare") { t.hidden = true; }
  if (m) {
    m.textContent = text;
    if (m.removeAttribute) m.removeAttribute("data-oq-matrix-wake");
  }
}

function showTerm(lines, mode = "term") {
  const t = termEl();
  setOverlayMode(mode);
  if (t) { t.hidden = false; t.textContent = Array.isArray(lines) ? lines.join("\n") : String(lines ?? ""); }
}

function revealLines(lines, stepMs, done, pulses, scramble) {
  const t = termEl();
  if (!t) { if (done) later(stepMs, done); return; }
  t.hidden = false;
  setOverlayMode("term");
  t.textContent = "";
  let i = 0;
  const show = () => {
    t.textContent = lines.slice(0, i + 1).join("\n");
    const p = pulses && pulses[i];
    if (p) String(p).split(" ").forEach((n) => { if (n) pulseMatrixTarget(n); });
    i += 1;
    if (i < lines.length) later(stepMs, step);
    else if (done) later(stepMs, done);
  };
  const step = () => {
    if (scramble) {
      t.textContent = lines.slice(0, i).concat([scrambleText(lines[i])]).join("\n");
      later(180, show);
    } else show();
  };
  step();
}

// Pulseert een echt dashboard-element (alleen CSS-class, nooit writes).
export function pulseMatrixTarget(name, red = false) {
  if (typeof document === "undefined" || !document.querySelectorAll) return;
  const cls = red ? "oq-matrix-pulse--red" : "oq-matrix-pulse";
  document.querySelectorAll(`[data-oq-matrix-target="${name}"]`).forEach((el) => {
    el.classList.add(cls);
    later(750, () => { if (el.classList) el.classList.remove(cls); });
  });
}

function clearMatrixPulses() {
  if (typeof document === "undefined" || !document.querySelectorAll) return;
  document.querySelectorAll(".oq-matrix-pulse").forEach((el) => el.classList.remove("oq-matrix-pulse"));
  document.querySelectorAll(".oq-matrix-pulse--red").forEach((el) => el.classList.remove("oq-matrix-pulse--red"));
}

function dockOverlay() { if (matrixOverlay) matrixOverlay.classList.add("oq-matrix-egg-quote--docked"); }
function undockOverlay() { if (matrixOverlay) matrixOverlay.classList.remove("oq-matrix-egg-quote--docked"); }
function spotlightOn() { const b = matrixBody(); if (b) b.classList.add("oq-matrix-spotlight"); }
function spotlightOff() { const b = matrixBody(); if (b) b.classList.remove("oq-matrix-spotlight"); }
function waveOn() { const b = matrixBody(); if (b) b.classList.add("oq-matrix-wave"); }
function waveOff() { const b = matrixBody(); if (b) b.classList.remove("oq-matrix-wave"); }

// Puls-targets uitgelijnd met buildMatrixTakeoverLines().
export function takeoverPulses(snap) {
  const s = snap || readMatrixSnapshot();
  const pulses = [null, "hp1"];
  if (s.duo) pulses.push("hp2");
  pulses.push("flow", "cop", null);
  return pulses;
}

export function scrambleText(s) {
  return String(s).split("").map((ch) => (/[A-Z0-9]/.test(ch) ? pickMatrixGlyph() : ch)).join("");
}

function scrambleTo(el, text, done) {
  let f = 0;
  const frame = () => {
    if (!matrixOverlay) { if (done) done(); return; }
    f += 1;
    if (f >= 4) { if (el) el.textContent = text; if (done) done(); return; }
    if (el) el.textContent = scrambleText(text);
    later(100, frame);
  };
  if (!el) { if (done) later(300, done); return; }
  frame();
}

function glitchOnce() {
  if (!matrixOverlay) return;
  matrixOverlay.classList.add("oq-matrix-glitch");
  later(180, () => { if (matrixOverlay) matrixOverlay.classList.remove("oq-matrix-glitch"); });
}

function showScrambled(text, mode, holdMs, next) {
  const m = mainEl();
  setOverlayMode(mode || "bare");
  const t = termEl();
  if (t && (mode || "bare") === "bare") t.hidden = true;
  if (m && m.removeAttribute) m.removeAttribute("data-oq-matrix-wake");
  glitchOnce();
  scrambleTo(m, text, () => later(holdMs, () => { if (next) next(); }));
}

function scrollToHardware(done) {
  const next = () => { if (done) done(); };
  if (typeof document === "undefined" || !document.querySelector) { next(); return; }
  const t = document.querySelector('[data-oq-matrix-target="hardware-section"]');
  if (!t || !t.scrollIntoView || !t.getBoundingClientRect) { next(); return; }
  const r = t.getBoundingClientRect();
  const vh = (typeof window !== "undefined" && window.innerHeight) || 800;
  if (r.top >= vh * 0.12 && r.bottom <= vh * 0.92) { next(); return; }
  matrixScrolled = true;
  try { t.scrollIntoView({ behavior: "smooth", block: "center" }); }
  catch { t.scrollIntoView(); }
  later(1300, next);
}

function restoreScroll() {
  if (!matrixScrolled || matrixUserScrolled) return;
  try {
    if (typeof window !== "undefined" && window.scrollTo) window.scrollTo({ top: matrixOrigY, behavior: "smooth" });
  } catch { /* camera blijft staan */ }
}

// Bonus bij live-verandering tijdens de scene (geen polling, alleen resample).
export function detectRealityShift(a, b) {
  if (!a || !b) return null;
  const ha = Math.round(a.hz1 > 0 ? a.hz1 : a.hz);
  const hb = Math.round(b.hz1 > 0 ? b.hz1 : b.hz);
  if (hb > 0 && ha > 0 && Math.abs(hb - ha) >= 2) return { kind: "hz", from: `${ha} Hz`, to: `${hb} Hz`, target: "hp1" };
  if (!Number.isNaN(a.cop) && !Number.isNaN(b.cop) && Math.abs(b.cop - a.cop) >= 1 && b.cop > 0) return { kind: "cop", from: a.cop.toFixed(1), to: b.cop.toFixed(1), target: "cop" };
  return null;
}

function runRealityShift(shift, done) {
  matrixHadShift = true;
  setRainMode("fast");
  runScene([
    [0, () => showMain("REALITY SHIFT DETECTED.", "warn")],
    [900, () => { showMain(`${shift.kind === "hz" ? "HP1" : "COP"} ................. ${shift.from} → ${shift.to}`, "term"); pulseMatrixTarget(shift.target); }],
    [1400, () => { setRainMode("green"); showMain("The Matrix changed."); }],
    [2000, callDone(done)],
  ]);
}

// Ultra-rare toestands-special (vervangt de gewone anomaly als hij valt).
export function pickSpecialKind(snap) {
  const s = snap || readMatrixSnapshot();
  if (s.hz > 0 && Math.round(s.hz) === 42) return "answer42";
  return null;
}

function runAnswer42(snap, done) {
  runScene([
    [0, () => {
      showMain("FREQUENCY ............ 42 Hz", "term");
      pulseMatrixTarget("hp1");
      if (snap.duo) pulseMatrixTarget("hp2");
    }],
    [1100, () => showMain("The answer was here all along.")],
    [2000, callDone(done)],
  ]);
}

function pillMarkup() {
  return `<div class=oq-matrix-pill-row><button class="oq-matrix-pill oq-matrix-pill--blue" data-oq-matrix-pill="blue"><span>BLUE PILL</span><small>Return to comfort</small></button><button class="oq-matrix-pill oq-matrix-pill--red" data-oq-matrix-pill="red"><span>RED PILL</span><small>See how deep it goes</small></button></div>`;
}

export function startMatrixMode() {
  if (typeof document === "undefined" || !document.body) return "";
  if (isMatrixModeActive()) return "";
  stopMatrixMode();
  document.body.classList.add("oq-matrix-active", "oq-matrix-boot");
  later(500, () => { if (typeof document !== "undefined" && document.body) document.body.classList.remove("oq-matrix-boot"); });
  matrixRain = createMatrixRain();
  matrixOverlay = document.createElement("div");
  matrixOverlay.className = "oq-matrix-egg-quote oq-matrix-egg-quote--bare";
  matrixOverlay.setAttribute("role", "dialog");
  matrixOverlay.setAttribute("aria-label", "Matrix choice");
  matrixOverlay.innerHTML = `<p class="oq-matrix-egg-text" data-oq-matrix-main></p><pre class="oq-matrix-egg-terminal" data-oq-matrix-term hidden></pre>`;
  const main = matrixOverlay.querySelector("[data-oq-matrix-main]");
  typewriteLine(main, "FREE THE HEAT PUMP.", 45);
  document.body.appendChild(matrixOverlay);

  matrixKeyHandler = (e) => { if (e && e.key === "Escape") stopMatrixMode(); };
  document.addEventListener("keydown", matrixKeyHandler);

  matrixOverlay.addEventListener("click", (e) => {
    const pill = e.target.closest?.("[data-oq-matrix-pill]");
    if (pill && !matrixChoiceMade) {
      const choice = pill.dataset.oqMatrixPill;
      matrixChoiceMade = true;
      matrixTimers.forEach((t) => clearTimeout(t));
      matrixTimers = [];
      if (matrixTypewriter) { clearInterval(matrixTypewriter); matrixTypewriter = 0; }
      const choiceWrap = matrixOverlay.querySelector(".oq-matrix-choice");
      if (choiceWrap) choiceWrap.remove();
      if (choice === "blue") runBluePath();
      else runRedPath(readMatrixSnapshot());
      return;
    }
    // easter egg ín de easter egg: één keer op "Wake up, Quatt." klikken
    const wake = e.target.closest?.("[data-oq-matrix-wake]");
    if (wake && matrixWakeArmed && !matrixSecretDone) {
      matrixSecretDone = true;
      matrixWakeArmed = false;
      matrixTimers.forEach((t) => clearTimeout(t));
      matrixTimers = [];
      if (matrixTypewriter) { clearInterval(matrixTypewriter); matrixTypewriter = 0; }
      runSecretEgg();
    }
  });

  later(1800, () => {
    if (!matrixOverlay || matrixChoiceMade) return;
    const m = mainEl();
    if (m) m.textContent = "FREE THE HEAT PUMP.";
    setOverlayMode("bare");
    const wrap = document.createElement("div");
    wrap.className = "oq-matrix-choice";
    wrap.innerHTML = `<p class="oq-matrix-egg-text">You have a choice.</p>${pillMarkup()}`;
    m?.insertAdjacentElement?.("afterend", wrap);
  });

  later(8800, () => {
    if (matrixChoiceMade || !matrixOverlay) return;
    matrixChoiceMade = true;
    if (matrixTypewriter) { clearInterval(matrixTypewriter); matrixTypewriter = 0; }
    const cw = matrixOverlay.querySelector(".oq-matrix-choice");
    if (cw) cw.remove();
    runScene([
      [0, () => showMain("You hesitate.")],
      [1400, () => showMain("Choice is an illusion.")],
      [1400, () => showMain("RED PILL SELECTED")],
      [1100, () => runRedPath(readMatrixSnapshot())],
    ]);
  });

  // Aparte failsafe: zit NIET in matrixTimers, dus scene-transities
  // (die matrixTimers clearen) kunnen hem nooit per ongeluk verwijderen.
  if (matrixFailsafe) clearTimeout(matrixFailsafe);
  matrixFailsafe = setTimeout(stopMatrixMode, MATRIX_ACTIVE_MS);
  return "FREE THE HEAT PUMP.";
}

function runBluePath() {
  setRainMode("blue");
  runScene([
    [0, () => showMain("BLUE PILL SELECTED")],
    [1000, (next) => {
      showMain("RESTORING NORMALITY...", "term");
      revealLines(["FACTORY LIMITS ........ RESTORING", "CLOUD DEPENDENCY ...... RESTORING", "REMOTE CONTROL ........ RESTORING", "VENDOR LOCK-IN ........ RESTORING"], 450, next);
    }],
    [500, (next) => {
      showMain("REINSTALLING LIMITATIONS...", "term");
      revealLines(["[███       ] 31%", "[██████    ] 64%", "[█████████ ] 94%"], 450, next);
    }],
    [600, () => { showMain("ERROR 0x4F51", "warn"); glitchOnce(); setRainMode("fast"); }],
    [1000, () => { setRainMode("blue"); showMain(pickOf("NOTHING TO RESTORE.", "PACKAGE NOT FOUND.")); }],
    [1000, () => showMain("Nice try.")],
    [1000, () => showMain("OpenQuatt is already free.")],
    [1200, () => showMain("BLUE PILL REJECTED.")],
    [1500, () => fadeOutThenStop()],
  ]);
}

function runRedPath(snap) {
  const redSnap = snap || readMatrixSnapshot();
  matrixOrigY = (typeof window !== "undefined" && window.scrollY) || 0;
  matrixScrolled = false;
  matrixUserScrolled = false;
  matrixScrollHandler = () => { matrixUserScrolled = true; };
  if (typeof window !== "undefined" && window.addEventListener) {
    window.addEventListener("wheel", matrixScrollHandler, { passive: true });
    window.addEventListener("touchmove", matrixScrollHandler, { passive: true });
  }
  setRainMode("green");
  showMain("RED PILL SELECTED");
  flashRed();
  runScene([
    [600, (next) => {
      showMain("ACCESSING SYSTEM...", "term");
      revealLines(buildMatrixTakeoverLines(redSnap), 300, next, takeoverPulses(redSnap), true);
    }],
    [600, (next) => showScrambled("ACCESS GRANTED", "bare", 900, next)],
    [0, () => {
      waveOn();
      pulseMatrixTarget("hardware-section");
      ["hp1", "hp2", "flow", "cop", "boiler"].forEach((n) => pulseMatrixTarget(n));
    }],
    [1800, () => { waveOff(); dockOverlay(); showMain("LOCATING HARDWARE..."); }],
    [900, () => showMain("TRACING PHYSICAL LAYER...")],
    [0, (next) => scrollToHardware(next)],
    [0, () => { showMain("PHYSICAL LAYER DETECTED"); spotlightOn(); }],
    [1200, (next) => {
      showMain("SCANNING HARDWARE...", "term");
      revealLines(
        redSnap.duo
          ? ["HP1 ................. ONLINE", "HP2 ................. ONLINE", "CONTROLLER .......... YOURS"]
          : ["HP1 ................. ONLINE", "CONTROLLER .......... YOURS"],
        350, next, redSnap.duo ? ["hp1", "hp2", null] : ["hp1", null], true,
      );
    }],
    [700, (next) => {
      spotlightOff();
      const truthSnap = readMatrixSnapshot();
      const shift = detectRealityShift(redSnap, truthSnap);
      const afterShift = () => runTruthBody(truthSnap, () => {
        const fresh = readMatrixSnapshot();
        const afterAgent = () => maybeAnomaly(fresh, () => finishRed());
        if (fresh.boilerActive) runAgentBoiler(fresh, afterAgent);
        else afterAgent();
      });
      if (shift) runRealityShift(shift, afterShift);
      else afterShift();
    }],
  ]);
}

function runAgentBoiler(snap, done) {
  matrixHadAgent = true;
  setRainMode("fast");
  flashRed();
  runScene([
    [0, () => { showMain("WARNING", "warn"); pulseMatrixTarget("boiler", true); }],
    [800, () => { showMain("AGENT DETECTED.", "warn"); glitchOnce(); pulseMatrixTarget("boiler", true); }],
    [900, () => { showMain("AGENT DETECTED.", "term"); showTerm(["IDENTITY ............. BOILER", "STATUS ............... ACTIVE"]); }],
    [900, () => showMain("Agent Boiler has entered the system.")],
    [1400, () => showMain(pickBoilerPunch())],
    [2000, () => { setRainMode("green"); done?.(); }],
  ]);
}

// Max één secundaire observatie naast de hoofdbranch (scene-budget).
function bonusScene(snap, bonus, done, wait, efficient) {
  if (bonus === "lowhz") return [[wait, () => showMain("30 Hz was never the floor.")], [2000, callDone(done)]];
  if (bonus === "cop") {
    if (efficient) {
      const cop = copText(snap, "5.7");
      return [[wait, () => { showMain(`COP ................. ${cop}\n\nEfficient.\nSuspiciously efficient.`); pulseMatrixTarget("cop"); }], [2000, callDone(done)]];
    }
    return [[wait, () => { showMain(copPunch(snap)); pulseMatrixTarget("cop"); }], [2000, callDone(done)]];
  }
  return [[wait, callDone(done)]];
}

function runTruthBody(snap, done) {
  const branch = pickTruthBranch(snap);
  setRainMode("green");
  if (branch === "defrost") {
    runScene([
      [0, () => { showMain("DÉJÀ VU."); pulseMatrixTarget("hp1", true); if (snap.duo) pulseMatrixTarget("hp2", true); }],
      [900, () => showMain("A glitch in the Matrix detected.\n\nDEFROST SEQUENCE ACTIVE")],
      [2500, () => done?.()],
    ]);
    return;
  }
  if (branch === "twins") {
    const hz = Math.round(snap.hz1);
    const bonus = pickBonusPunch(snap, "twins");
    runScene([
      [0, (next) => {
        showMain("SCANNING COMPRESSORS...", "term");
        revealLines([`HP1 .................. ${hz} Hz`, `HP2 .................. ${Math.round(snap.hz2)} Hz`, "SYNCHRONIZATION ...... 100%"], 450, next, ["hp1", "hp2", null], true);
      }],
      [700, () => { showMain("THE TWINS ARE SYNCHRONIZED."); pulseMatrixTarget("hp1"); pulseMatrixTarget("hp2"); }],
      [1200, () => showMain(twinsPunch())],
      ...bonusScene(snap, bonus, done, 2000),
    ]);
    return;
  }
  if (branch === "cop") {
    const cop = copText(snap, "—");
    const bonus = pickBonusPunch(snap, "cop");
    runScene([
      [0, (next) => {
        showMain("ANALYZING EFFICIENCY...", "term");
        revealLines([`COP .................. ${cop}`, "EXPECTED ............. LESS", "THERMODYNAMICS ....... SUSPICIOUS"], 450, next, [null, null, null], true);
      }],
      [700, () => { showMain(copPunch(snap)); pulseMatrixTarget("cop"); }],
      ...bonusScene(snap, bonus, done, 2200),
    ]);
    return;
  }
  if (branch === "lowhz") {
    const hz = Math.round(snap.hz);
    const bonus = pickBonusPunch(snap, "lowhz");
    runScene([
      [0, (next) => {
        showMain("SEARCHING FOR LIMITS...", "term");
        revealLines(["FACTORY FLOOR ........ 30 Hz", `CURRENT FREQUENCY .... ${hz} Hz`, "LIMIT NOT FOUND."], 500, next, [null, snap.duo ? "hp1 hp2" : "hp1", null], true);
      }],
      [1000, () => showMain("30 Hz was never the floor.")],
      ...bonusScene(snap, bonus, done, 2200, true),
    ]);
    return;
  }
  if (branch === "cm0") {
    runScene([
      [0, (next) => {
        showMain("SCANNING DEMAND...", "term");
        revealLines(["THERMAL DEMAND ....... 0", "COMPRESSORS .......... ASLEEP"], 500, next);
      }],
      [700, () => showMain(cm0Punch())],
      [2000, callDone(done)],
    ]);
    return;
  }
  if (branch === "cooling") {
    runScene([
      [0, (next) => {
        showMain("HEATING .............. DENIED", "term");
        revealLines(["HEATING .............. DENIED", "COOLING .............. ACTIVE"], 500, next);
      }],
      [700, () => showMain(coolingPunch())],
      [2000, callDone(done)],
    ]);
    return;
  }
  runScene([
    [0, () => showMain(pickGenericPunch())],
    [2200, () => done?.()],
  ]);
}

function maybeAnomaly(snap, done) {
  // Scene-budget: shift + agent zijn samen al twee bonus-events;
  // een derde (special/anomaly) zou de run encyclopedisch maken.
  if (matrixHadShift && matrixHadAgent) { done?.(); return; }
  if (pickSpecialKind(snap) === "answer42") { runAnswer42(snap, done); return; }
  const kind = pickAnomalyKind();
  if (kind < 0) { done?.(); return; }
  runAnomaly(kind, snap, done);
}

function runAnomaly(kind, snap, done) {
  setRainMode("green");
  if (kind === 0) {
    runScene([
      [0, () => showMain("FOLLOW THE WHITE RABBIT.")],
      [1100, () => showMain("...")],
      [700, () => showMain("Wrong rabbit.")],
      [1100, () => showMain("FOLLOW THE GREEN COP.")],
      [2000, callDone(done)],
    ]);
  } else if (kind === 1) {
    runScene([
      [0, () => showMain("Knock, knock, Quatt.")],
      [1100, () => showMain("I know Modbus.")],
      [1100, () => { showMain("I know Modbus.", "term"); showTerm(["MODBUS REGISTER 2127 .... READ"]); }],
      [900, () => showMain("Whoa.")],
      [1500, () => done?.()],
    ]);
  } else if (kind === 2) {
    runScene([
      [0, () => showMain("SEARCHING FOR CLOUD...", "term")],
      [1100, () => showTerm(["CLOUD ............... NOT REQUIRED"])],
      [1100, () => showMain("Good.")],
      [1500, () => done?.()],
    ]);
  } else if (kind === 3) {
    runScene([
      [0, () => showMain("SCANNING OPERATOR...", "term")],
      [1000, () => showTerm(["OWNER ................ YOU", "ACCESS LEVEL .......... ROOT"])],
      [1100, () => showMain("The system is yours.")],
      [2000, callDone(done)],
    ]);
  } else {
    runScene([
      [0, () => showMain("SCANNING FIRMWARE...", "term")],
      [1000, () => showTerm(["PROPRIETARY CODE ...... NOT FOUND"])],
      [1100, () => showMain("As it should be.")],
      [2000, callDone(done)],
    ]);
  }
}

function finishRed() {
  undockOverlay();
  setRainMode("thin");
  const m = mainEl();
  const t = termEl();
  if (!m || !t) return;
  t.hidden = true;
  runScene([
    [0, () => showMain("THE SYSTEM IS YOURS.")],
    [1500, (next) => { showMain(""); typewriteLine(mainEl(), "FREE YOUR HEAT PUMP.", 40); later(1800, next); }],
    [0, () => {
      // Kwart seconde volledig stil: rain bevroren, scanline gepauzeerd,
      // alle pulses gedoofd. Daarna ineens reverse + terugscroll.
      setRainMode("freeze");
      const b = matrixBody();
      if (b) b.classList.add("oq-matrix-freeze");
      clearMatrixPulses();
    }],
    [250, (next) => {
      setRainMode("reverse");
      const b = matrixBody();
      if (b) b.classList.remove("oq-matrix-freeze");
      restoreScroll();
      const w = mainEl();
      setOverlayMode("bare");
      glitchOnce();
      scrambleTo(w, "Wake up, Quatt.", () => {
        if (w && w.setAttribute) w.setAttribute("data-oq-matrix-wake", "1");
        matrixWakeArmed = true;
        next();
      });
    }],
    [2600, () => { if (!matrixSecretDone) fadeOutThenStop(1400); }],
  ]);
}

function runSecretEgg() {
  setRainMode("green");
  runScene([
    [0, () => showMain("Seriously?")],
    [1200, () => showMain("You click everything, don't you?")],
    [1600, () => showMain("ACCESS LEVEL ......... DEVELOPER", "term")],
    [1400, () => showMain("Fine.")],
    [900, () => setRainMode("fast")],
    [500, () => hardCutThenStop()],
  ]);
}

// Finale: eerst overlay uit, dan UI normaal, rain als laatste
function fadeOutThenStop(rainHold = 0) {
  matrixWakeArmed = false;
  if (matrixOverlay) matrixOverlay.classList.add("oq-matrix-egg-hidden");
  later(rainHold, () => { if (matrixRain) matrixRain.classList.add("oq-matrix-egg-hidden"); });
  later(800 + rainHold, () => {
    if (typeof document !== "undefined" && document.body) document.body.classList.remove("oq-matrix-active");
  });
  later(2200 + rainHold, stopMatrixMode);
}

// Secret-cut: abrupt zwart (body blijft even zwart), dan normaal
function hardCutThenStop() {
  matrixWakeArmed = false;
  if (matrixOverlay) matrixOverlay.classList.add("oq-matrix-egg-hidden");
  if (matrixRain) matrixRain.classList.add("oq-matrix-egg-hidden");
  later(400, stopMatrixMode);
}
