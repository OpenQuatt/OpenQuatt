import { formatValue, getEntityValue, parseLooseNumber } from "../core/entity-store.js";
import { state } from "../core/state.js";
import { getInstallationTopology } from "./device-context.js";

function readFrequency(key, useDrafts = false) {
  const entity = state.entities[key];
  const value = parseLooseNumber(useDrafts ? getEntityValue(key) : entity?.value ?? entity?.state);
  return Number.isFinite(value) && value >= 0 && value <= 120 ? Math.round(value) : null;
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
  return `${cap} Hz is lager dan het minimum van ${unit.toUpperCase()} bij ${mode === "cooling" ? "koelen" : "verwarmen"}: ${minimum} Hz. ${unit.toUpperCase()} kan hierdoor niet starten.`;
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
    `${units.join(" en ")} bij ${mode === "cooling" ? "koelen" : "verwarmen"}: ${minimum} Hz`);
  const messages = minima.length
    ? [`${model.cap} Hz is te laag. Minimum: ${minima.join("; ")}. Deze buitenunits kunnen in die bedrijfsmodi niet starten.`]
    : [];
  if (model.unknown) messages.push("Minimumfrequentie nog niet beschikbaar voor alle bedrijfsmodi en buitenunits. Een te lage limiet kan starten verhinderen.");
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
    if (cap !== null && input && input.value !== String(cap)) input.value = String(cap);
    if (cap !== null && label) label.textContent = formatValue(key, cap);
    if (node.textContent !== warning.text) node.textContent = warning.text;
    node.hidden = !warning.text;
    node.classList.toggle("oq-settings-action-note--warning", warning.warning);
  });
}
