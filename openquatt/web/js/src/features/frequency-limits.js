import { formatValue, getEntityValue, parseLooseNumber } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";
import { getInstallationTopology } from "./device-context.js";

function readFrequency(key, useDrafts = false) {
  const entity = state.entities[key];
  const value = parseLooseNumber(useDrafts ? getEntityValue(key) : entity?.value ?? entity?.state);
  return Number.isFinite(value) && value >= 0 && value <= 120 ? Math.round(value) : null;
}

export function getAutomaticFrequencyTables(hp, read = getEntityValue) {
  const tables = String(read(`${hp}AutomaticFrequencies`) || "").split("|");
  return [0, 1].map((index) => {
    const frequencies = tables[index]?.split(",").map(Number) || [];
    return frequencies.every((hz) => Number.isInteger(hz) && hz > 0 && hz <= 120) ? frequencies : [];
  });
}

function renderFrequencyLevelSummaries(cap) {
  if (cap === null) return "";
  const duo = getInstallationTopology() === "duo";
  const hp1 = getAutomaticFrequencyTables("hp1");
  const hp2 = duo ? getAutomaticFrequencyTables("hp2") : [];
  return hp1.map((frequencies, index) => {
    const mode = index === 0 ? t("frequencyExclusion.heating") : t("frequencyExclusion.cooling");
    const other = hp2[index];
    if (!duo || frequencies.join() === other.join()) {
      return renderFrequencyLevelSummary(mode, frequencies, cap);
    }
    return renderFrequencyLevelSummary(`${mode} · HP1`, frequencies, cap)
      + renderFrequencyLevelSummary(`${mode} · HP2`, other, cap);
  }).join("");
}

function renderFrequencyLevelSummary(label, frequencies, cap) {
  if (!frequencies.length) return "";
  // The firmware publishes the frequencies in physical level order, starting at F1.
  let level = frequencies.length;
  while (level > 0 && frequencies[level - 1] > cap) level--;
  const summary = level > 0 ? `F${level} · ${frequencies[level - 1]} Hz` : t("frequencyLimits.noLevel");
  return `<div class="oq-settings-frequency-caption"><span>${escapeHtml(label)}</span><strong>${escapeHtml(summary)}</strong></div>`;
}

export function renderFrequencyLimitLevels(key) {
  return `<div data-oq-frequency-limit-levels>${renderFrequencyLevelSummaries(readFrequency(key, true))}</div>`;
}

export function getFrequencyLimitModel(key, { useDrafts = false, mode = "" } = {}) {
  const cap = readFrequency(key, useDrafts);
  const units = getInstallationTopology() === "duo" ? ["hp1", "hp2"] : ["hp1"];
  const modes = mode ? [mode] : ["heating", "cooling"];
  const limits = units.flatMap((unit) => modes.map((operation) => {
    const minimum = readFrequency(`${unit}Minimum${operation === "cooling" ? "Cooling" : "Heating"}Hz`);
    return { unit, mode: operation, minimum, blocked: cap !== null && minimum > 0 && cap < minimum };
  }));
  return { cap, limits, blocked: limits.filter((limit) => limit.blocked), unknown: limits.some((limit) => !(limit.minimum > 0)) };
}

export function describeFrequencyLimit(cap, { unit, mode, minimum }) {
  return t("frequencyLimits.describe", {
    cap,
    unit: unit.toUpperCase(),
    mode: mode === "cooling" ? t("frequencyLimits.modeCooling") : t("frequencyLimits.modeHeating"),
    minimum,
  });
}

export function getFrequencyLimitWarning(key) {
  const model = getFrequencyLimitModel(key, { useDrafts: true });
  const groups = new Map();
  for (const limit of model.blocked) {
    const groupKey = `${limit.mode}:${limit.minimum}`;
    if (!groups.has(groupKey)) groups.set(groupKey, { ...limit, units: [] });
    groups.get(groupKey).units.push(limit.unit.toUpperCase());
  }
  const minima = [...groups.values()].map(({ units, mode, minimum }) =>
    t("frequencyLimits.groupUnits", {
      units: units.join(t("frequencyLimits.unitsAnd")),
      mode: mode === "cooling" ? t("frequencyLimits.modeCooling") : t("frequencyLimits.modeHeating"),
      minimum,
    }));
  const messages = minima.length
    ? [t("frequencyLimits.tooLow", { cap: model.cap, minima: minima.join("; ") })]
    : [];
  if (model.unknown) messages.push(t("frequencyLimits.unknownMin"));
  return { text: messages.join(" "), warning: model.blocked.length > 0 };
}

export function patchFrequencyLimitWarnings(root = state.root) {
  root?.querySelectorAll("[data-oq-frequency-limit-warning]").forEach((node) => {
    const key = node.dataset.oqFrequencyLimitWarning;
    const warning = getFrequencyLimitWarning(key);
    // Keep the visible slider and its warning on the same draft/live value,
    // including remote changes while the overview modal is open.
    const cap = readFrequency(key, true);
    const card = node.closest("[data-oq-settings-field]");
    const input = card?.querySelector('input[type="range"]');
    const label = card?.querySelector(".oq-helper-slider-meta strong");
    const levels = card?.querySelector("[data-oq-frequency-limit-levels]");
    if (cap !== null && input && input.value !== String(cap)) input.value = String(cap);
    if (cap !== null && label) label.textContent = formatValue(key, cap);
    if (levels) {
      const markup = renderFrequencyLevelSummaries(cap);
      if (levels.innerHTML !== markup) levels.innerHTML = markup;
    }
    if (node.textContent !== warning.text) node.textContent = warning.text;
    node.hidden = !warning.text;
    node.classList.toggle("oq-settings-action-note--warning", warning.warning);
  });
}
