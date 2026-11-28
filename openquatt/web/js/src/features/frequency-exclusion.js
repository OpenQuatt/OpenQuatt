import { isEntityActive } from "../core/app-shared.js";
import { getEntityValue, toTimeInputValue } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";
import { getFrequencyExclusionModel, getSavedFrequencyValue } from "../core/frequency-exclusion-model.js";

export { getFrequencyExclusionModel, getFrequencyExclusionSaveWarning } from "../core/frequency-exclusion-model.js";

export function formatFrequencyRangeLabel(min, max, unit = "Hz") {
  if (min === 0 && max === 0) return t("settingsControls.noExclusion");
  if (min === 0) return t("settingsControls.openLowerRange", { value: `${max} ${unit}` });
  if (max === 0) return t("settingsControls.openUpperRange", { value: `${min} ${unit}` });
  if (min > max) return t("settingsControls.invalidRange");
  return `${min}–${max} ${unit}`;
}

function getDetails(hp, edits) {
  const saving = state.savingFrequencyRange.startsWith(`${hp}Exclude`);
  const { scope, min, max, hasRange, summaries } = getFrequencyExclusionModel(hp, edits, saving ? getSavedFrequencyValue : getEntityValue);
  let status = t("frequencyExclusion.noRange");
  if (scope === "Off") status = t("frequencyExclusion.off");
  else if (min === 0 && max > 0) status = t("frequencyExclusion.chooseLower");
  else if (max === 0 && min > 0) status = t("frequencyExclusion.chooseUpper");
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
