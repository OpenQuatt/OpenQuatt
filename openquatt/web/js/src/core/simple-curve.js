import { CURVE_POINTS } from "./config.js";
import { getEntityValue } from "./entity-store.js";
import { state } from "./state.js";

export function generateSimpleCurve(slope, level) {
  if (!Number.isFinite(slope) || !Number.isFinite(level) || slope < 0 || slope > 15 || level < 20 || level > 70) {
    return null;
  }
  return CURVE_POINTS.map((point) => ({
    ...point,
    value: Math.max(20, Math.min(70, Math.round((level - (slope * point.outdoor / 10)) * 2) / 2)),
  }));
}

export function getSimpleCurveDraft() {
  if (state.simpleCurveDraft) return state.simpleCurveDraft;
  const points = getCurvePointDraft();
  const atZero = points[2].value;
  const atMinusTen = points[1].value;
  return {
    slope: Number.isFinite(atZero) && Number.isFinite(atMinusTen)
      ? Math.max(0, Math.min(15, Math.round((atMinusTen - atZero) * 2) / 2)) : 5,
    level: Number.isFinite(atZero) ? Math.max(20, Math.min(70, atZero)) : 45,
  };
}

export function getCurvePointDraft() {
  const values = state.curvePointDraft;
  if (!values && state.simpleCurveDraft) return generateSimpleCurve(state.simpleCurveDraft.slope, state.simpleCurveDraft.level);
  return CURVE_POINTS.map((point, index) => {
    const raw = values ? values[index] : getEntityValue(point.key);
    return { ...point, value: raw === null || raw === undefined || raw === "" ? NaN : Number(raw) };
  });
}

export function updateSimpleCurveDraft(part, rawValue) {
  if (part !== "slope" && part !== "level") return false;
  const value = Number(rawValue);
  const current = getSimpleCurveDraft();
  const next = { ...current, [part]: value };
  if (!generateSimpleCurve(next.slope, next.level)) return false;
  state.simpleCurveDraft = next;
  state.curvePointDraft = generateSimpleCurve(next.slope, next.level).map((point) => point.value);
  return true;
}

export function updateCurvePointDraft(key, rawValue) {
  const index = CURVE_POINTS.findIndex((point) => point.key === key);
  if (index < 0 || rawValue === "" || rawValue === null || rawValue === undefined) return false;
  const numeric = Number(rawValue);
  if (!Number.isFinite(numeric)) return false;
  const value = Math.max(20, Math.min(70, Math.round(numeric * 2) / 2));
  const values = getCurvePointDraft().map((point) => point.value);
  if (values[index] === value) return false;
  values[index] = value;
  state.curvePointDraft = values;
  state.simpleCurveDraft = null;
  return true;
}

export async function applySimpleCurvePoints(points, originals, write, read) {
  let failed = false;
  for (const [index, point] of points.entries()) {
    if (originals[index] !== point.value && !(await write(point.key, point.value))) {
      failed = true;
      break;
    }
  }
  if (!failed) failed = points.some((point) => Number(read(point.key)) !== point.value);
  if (!failed) return { applied: true, restored: true };

  let restored = true;
  for (const [index, point] of CURVE_POINTS.entries()) {
    const accepted = await write(point.key, originals[index]);
    restored = accepted && restored;
  }
  if (restored) restored = CURVE_POINTS.every((point, index) => Number(read(point.key)) === originals[index]);
  return { applied: false, restored };
}

export async function applySimpleCurveBatch(points, submit, refresh, read, wait = (ms) => new Promise((resolve) => setTimeout(resolve, ms))) {
  let result;
  try {
    result = await submit(points);
  } catch (_error) {
    // The request may have reached the controller even when its reply was lost.
  }
  if (result === "unsupported") return { applied: false, unsupported: true };
  if (result === "rejected") return { applied: false, unsupported: false };

  for (let attempt = 0; attempt < 5; ++attempt) {
    try {
      await refresh();
    } catch (_error) {
      // Retry the read; a successful POST alone does not confirm the values.
    }
    if (points.every((point) => Number(read(point.key)) === point.value)) {
      return { applied: true, unsupported: false };
    }
    if (attempt < 4) await wait(150);
  }
  return { applied: false, unsupported: false };
}
