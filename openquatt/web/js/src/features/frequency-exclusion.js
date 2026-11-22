import { isEntityActive } from "../core/app-shared.js";
import { getEntityValue, parseLooseNumber, toTimeInputValue } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";

const modes = ["Heating", "Cooling"];
const periods = [
  { key: "dayMaxHz", label: "day" },
  { key: "silentMaxHz", label: "quiet" },
];

function savedValue(key) {
  return state.entities[key]?.value ?? state.entities[key]?.state;
}

export function getFrequencyExclusionModel(hp, edits = {}, read = getEntityValue) {
  const value = (suffix) => edits[suffix] ?? read(`${hp}Exclude${suffix}`);
  const scope = value("Scope") || "Always";
  const min = parseLooseNumber(value("MinHz"));
  const max = parseLooseNumber(value("MaxHz"));
  const hasRange = min > 0 && max >= min && max <= 120;
  const tables = String(read(`${hp}AutomaticFrequencies`) || "").split("|");
  const summaries = modes.map((mode, index) => {
    const frequencies = tables[index]?.split(",").map(Number) || [];
    const known = frequencies.length > 0 && frequencies.every((hz) => Number.isInteger(hz) && hz > 0 && hz <= 120);
    const available = periods.filter(({ label }) => scope !== "Quiet hours" || label === "quiet").map(({ key, label }) => {
      const cap = parseLooseNumber(read(key));
      const underCap = known && cap >= 0 && cap <= 120 ? frequencies.filter((hz) => hz <= cap) : null;
      const remaining = underCap?.filter((hz) => !hasRange || hz < min || hz > max).length ?? null;
      return { label, remaining, blocked: remaining === 0 && underCap?.length > 0 };
    });
    const excluded = known && hasRange
      ? frequencies.flatMap((hz, level) => hz >= min && hz <= max ? [`F${level + 1} (${hz} Hz)`] : [])
      : [];
    return { mode, known, available, excluded };
  });
  return { scope, hasRange, invalidRange: max > 0 && min > max, summaries };
}

export function getFrequencyExclusionSaveWarning(hp, edits) {
  const current = getFrequencyExclusionModel(hp, {}, savedValue);
  const next = getFrequencyExclusionModel(hp, edits, savedValue);
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

function getDetails(hp, edits) {
  const { scope, hasRange, summaries } = getFrequencyExclusionModel(hp, edits);
  let status = t("frequencyExclusion.noRange");
  if (scope === "Off") status = t("frequencyExclusion.off");
  else if (hasRange && scope === "Always") status = t("frequencyExclusion.alwaysActive");
  else if (hasRange) {
    const silentStatus = getEntityValue("silentStatus");
    const endTime = toTimeInputValue(getEntityValue("silentEndTime"));
    if (silentStatus === "forced_on") status = t("frequencyExclusion.manualOn");
    else if (silentStatus === "forced_off") status = t("frequencyExclusion.manualOff");
    else if (!silentStatus || silentStatus === "time_invalid") status = t("frequencyExclusion.waiting");
    else if (isEntityActive("silentActive")) {
      status = endTime ? t("frequencyExclusion.until", { time: endTime }) : t("frequencyExclusion.alwaysActive");
    } else status = t("frequencyExclusion.outside");
  }
  return [
    { text: status, warning: false },
    ...summaries.map(({ mode, known, available, excluded }) => {
      if (!hasRange || scope === "Off") return { text: "", warning: false };
      const label = modeLabel(mode);
      if (!known) return { text: t("frequencyExclusion.unknown", { mode: label }), warning: false };
      const counts = available.filter(({ remaining }) => remaining !== null)
        .map(({ label, remaining }) => t("frequencyExclusion.remaining", { period: periodLabel(label), count: remaining }));
      const affected = excluded.length
        ? t("frequencyExclusion.affected", { levels: excluded.join(", ") })
        : t("frequencyExclusion.unaffected");
      return { text: [label, affected, ...counts].join(" · "), warning: available.some(({ remaining }) => remaining === 0) };
    }),
  ];
}

export function renderFrequencyExclusionDetails(hp) {
  return `<div data-oq-frequency-exclusion="${hp}">${getDetails(hp).map(({ text, warning }) => `<p class="oq-settings-action-note${warning ? " oq-settings-action-note--warning" : ""}"${text ? "" : " hidden"}>${escapeHtml(text)}</p>`).join("")}</div>`;
}

export function patchFrequencyExclusionDetails(root = state.root, edits) {
  root?.querySelectorAll("[data-oq-frequency-exclusion]").forEach((node) => {
    getDetails(node.dataset.oqFrequencyExclusion, edits).forEach(({ text, warning }, index) => {
      const line = node.children[index];
      if (line.textContent !== text) line.textContent = text;
      line.hidden = !text;
      line.classList.toggle("oq-settings-action-note--warning", warning);
    });
  });
}
