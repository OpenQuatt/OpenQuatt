import { formatValue, getEntityValue, parseLooseNumber } from "../core/entity-store.js";
import { parseAutomaticFrequencyTables, readAutomaticFrequencyParts } from "../core/frequency-exclusion-model.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";
import { getInstallationTopology } from "./device-context.js";

export { getAutomaticFrequencyTables } from "../core/frequency-exclusion-model.js";

function readFrequency(key, useDrafts = false) {
  const entity = state.entities[key];
  const value = parseLooseNumber(useDrafts ? getEntityValue(key) : entity?.value ?? entity?.state);
  return Number.isFinite(value) && value >= 0 && value <= 120 ? Math.round(value) : null;
}

function getFrequencyLevelOptions(hp, quiet) {
  const parts = readAutomaticFrequencyParts(hp, getEntityValue);
  const [level, ...limits] = quiet && parts[2] ? parts[2].split(",").map(Number) : [];
  return parseAutomaticFrequencyTables(parts).map((frequencies, index) => {
    const hz = limits[index];
    const known = Number.isInteger(level) && level > 0 && level <= 20 && limits.length === 2
      && Number.isInteger(hz) && hz > 0 && hz <= 120
      && (level > frequencies.length || frequencies[level - 1] === hz);
    return { frequencies, nativeLevel: known ? level : 0, nativeHz: known ? hz : 0 };
  });
}

function renderFrequencyLevelSummaries(key, cap) {
  if (cap === null) return "";
  const quiet = key === "silentMaxHz";
  const duo = getInstallationTopology() === "duo";
  const hp1 = getFrequencyLevelOptions("hp1", quiet);
  const hp2 = duo ? getFrequencyLevelOptions("hp2", quiet) : [];
  const markup = hp1.map((options, index) => {
    const mode = index === 0 ? t("frequencyExclusion.heating") : t("frequencyExclusion.cooling");
    const other = hp2[index];
    if (!other || (options.frequencies.join() === other.frequencies.join()
      && options.nativeLevel === other.nativeLevel && options.nativeHz === other.nativeHz)) {
      return renderFrequencyLevelSummary(mode, options, cap);
    }
    return renderFrequencyLevelSummary(`${mode} · HP1`, options, cap)
      + renderFrequencyLevelSummary(`${mode} · HP2`, other, cap);
  }).join("");
  if (quiet && [...hp1, ...hp2].some(({ frequencies, nativeLevel }) => frequencies.length && !nativeLevel)) {
    return markup + `<p class="oq-settings-action-note">${escapeHtml(t("frequencyLimits.nativeUnknown"))}</p>`;
  }
  return markup;
}

function renderFrequencyLevelSummary(label, { frequencies, nativeLevel, nativeHz }, cap) {
  if (!frequencies.length) return "";
  // The firmware publishes the frequencies in physical level order, starting at F1.
  let level = nativeLevel ? Math.min(frequencies.length, nativeLevel) : frequencies.length;
  while (level > 0 && frequencies[level - 1] > cap) level--;
  const summary = level > 0 ? `F${level} · ${frequencies[level - 1]} Hz` : t("frequencyLimits.noLevel");
  const value = `<strong>${escapeHtml(summary)}</strong>`;
  const detail = nativeLevel && (level === nativeLevel
    ? t("frequencyLimits.nativeCeiling")
    : t("frequencyLimits.nativeCeilingHz", { level: nativeLevel, hz: nativeHz }));
  return `<div class="oq-settings-frequency-caption"><span>${escapeHtml(label)}</span>${detail
    ? `<span class="oq-settings-frequency-value">${value}<small>${escapeHtml(detail)}</small></span>`
    : value}</div>`;
}

export function renderFrequencyLimitLevels(key) {
  return `<div data-oq-frequency-limit-levels>${renderFrequencyLevelSummaries(key, readFrequency(key, true))}</div>`;
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
      const markup = renderFrequencyLevelSummaries(key, cap);
      if (levels.innerHTML !== markup) levels.innerHTML = markup;
    }
    if (node.textContent !== warning.text) node.textContent = warning.text;
    node.hidden = !warning.text;
    node.classList.toggle("oq-settings-action-note--warning", warning.warning);
  });
}
