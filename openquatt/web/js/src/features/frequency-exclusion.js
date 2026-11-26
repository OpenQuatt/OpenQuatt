import { isEntityActive } from "../core/app-shared.js";
import { getEntityValue, parseLooseNumber, toTimeInputValue } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";
import { getAutomaticFrequencyTables } from "./frequency-limits.js";

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
  const details = summaries.map(({ mode, known, available, excluded }) => {
    if (!hasRange || scope === "Off") return { title: "", affected: "", counts: "", warning: false };
    const title = mode === "Heating" ? t("frequencyExclusion.heating") : t("frequencyExclusion.cooling");
    if (!known) return { title, affected: t("frequencyExclusion.unknown"), counts: "", warning: false };
    const counts = available.filter(({ remaining }) => remaining !== null)
      .map(({ label, remaining }) => label === "day"
        ? t("frequencyExclusion.availableDay", { count: remaining })
        : t("frequencyExclusion.availableQuiet", { count: remaining }));
    const affected = excluded.length
      ? t("frequencyExclusion.affected", { frequencies: excluded.map((hz) => `${hz} Hz`).join(", ") })
      : t("frequencyExclusion.unaffected");
    return {
      title,
      affected,
      counts: counts.length ? t("frequencyExclusion.available", { periods: counts.join(" · ") }) : "",
      warning: available.some(({ remaining }) => remaining === 0),
    };
  });
  return { status, details };
}

export function renderFrequencyExclusionDetails(hp) {
  const { status, details } = getDetails(hp);
  return `<div class="oq-settings-field--span-2 oq-frequency-exclusion-details" data-oq-frequency-exclusion="${hp}">
    <p class="oq-settings-action-note">${escapeHtml(status)}</p>
    ${details.map(({ title, affected, counts, warning }) => `<p class="oq-settings-action-note${warning ? " oq-settings-action-note--warning" : ""}"${title ? "" : " hidden"}>
      <strong>${escapeHtml(title)}</strong>
      <span>${escapeHtml(affected)}</span>
      <span${counts ? "" : " hidden"}>${escapeHtml(counts)}</span>
    </p>`).join("")}
  </div>`;
}

export function patchFrequencyExclusionDetails(root = state.root, edits) {
  root?.querySelectorAll("[data-oq-frequency-exclusion]").forEach((node) => {
    const { status, details } = getDetails(node.dataset.oqFrequencyExclusion, edits);
    if (node.firstElementChild.textContent !== status) node.firstElementChild.textContent = status;
    details.forEach(({ title, affected, counts, warning }, index) => {
      const line = node.children[index + 1];
      const [heading, frequencies, availability] = line.children;
      if (heading.textContent !== title) heading.textContent = title;
      if (frequencies.textContent !== affected) frequencies.textContent = affected;
      if (availability.textContent !== counts) availability.textContent = counts;
      availability.hidden = !counts;
      line.hidden = !title;
      line.classList.toggle("oq-settings-action-note--warning", warning);
    });
  });
}
