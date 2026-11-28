import { getEntityValue, parseLooseNumber } from "./entity-store.js";
import { state } from "./state.js";
import { t } from "../i18n/index.js";

export function readAutomaticFrequencyParts(hp, read) {
  return String(read(`${hp}AutomaticFrequencies`) || "").split("|");
}

export function parseAutomaticFrequencyTables(tables) {
  return [0, 1].map((index) => {
    const frequencies = tables[index]?.split(",").map(Number) || [];
    return frequencies.every((hz) => Number.isInteger(hz) && hz > 0 && hz <= 120) ? frequencies : [];
  });
}

export function getAutomaticFrequencyTables(hp, read = getEntityValue) {
  return parseAutomaticFrequencyTables(readAutomaticFrequencyParts(hp, read));
}

const modes = ["Heating", "Cooling"];
const periods = [
  { key: "dayMaxHz", label: "day" },
  { key: "silentMaxHz", label: "quiet" },
];

export function getSavedFrequencyValue(key) {
  return state.entities[key]?.value ?? state.entities[key]?.state;
}

export function getFrequencyExclusionModel(hp, edits = {}, read = getEntityValue) {
  const value = (suffix) => edits[suffix] ?? read(`${hp}Exclude${suffix}`);
  const scope = value("Scope") || "Always";
  const min = parseLooseNumber(value("MinHz"));
  const max = parseLooseNumber(value("MaxHz"));
  const hasRange = min > 0 && max >= min && max <= 120;
  const tables = getAutomaticFrequencyTables(hp, read);
  const summaries = modes.map((mode, index) => {
    const frequencies = tables[index];
    const known = frequencies.length > 0;
    const available = periods.filter(({ label }) => scope !== "Quiet hours" || label === "quiet").map(({ key, label }) => {
      const cap = parseLooseNumber(read(key));
      const underCap = known && cap >= 0 && cap <= 120 ? frequencies.filter((hz) => hz <= cap) : null;
      const remaining = underCap?.filter((hz) => !hasRange || hz < min || hz > max).length ?? null;
      return { label, remaining, blocked: remaining === 0 && underCap?.length > 0 };
    });
    const excluded = known && hasRange
      ? [...new Set(frequencies.filter((hz) => hz >= min && hz <= max))]
      : [];
    return { mode, known, available, excluded };
  });
  return { scope, min, max, hasRange, invalidRange: max > 0 && min > max, summaries };
}

export function getFrequencyExclusionSaveWarning(hp, edits, read = getSavedFrequencyValue) {
  const current = getFrequencyExclusionModel(hp, {}, read);
  const next = getFrequencyExclusionModel(hp, edits, read);
  if (next.scope === "Off") return "";
  const blocked = next.summaries.flatMap(({ mode, available }, index) => available
    .filter(({ label, blocked }) => blocked && (current.scope === "Off"
      || !current.summaries[index].available.some((period) => period.label === label && period.blocked)))
    .map(({ label }) => `${modeLabel(mode)} (${periodLabel(label)})`));
  return blocked.length ? t("frequencyExclusion.confirm", { blocked: blocked.join(", ") }) : "";
}

function modeLabel(mode) {
  return mode === "Heating" ? t("frequencyLimits.modeHeating") : t("frequencyLimits.modeCooling");
}

function periodLabel(label) {
  return label === "day" ? t("frequencyExclusion.day") : t("frequencyExclusion.quiet");
}
