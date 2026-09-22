import { renderSettingsSection, renderSettingsSliderField, renderSettingsTimeField } from "./controls.js";
import { escapeHtml } from "../core/html.js";
import { getFrequencyLimitWarning } from "../features/frequency-limits.js";
import { t } from "../i18n/index.js";

  function renderFrequencyLimitWarning(key) {
    const warning = getFrequencyLimitWarning(key);
    return `<p class="oq-settings-action-note${warning.warning ? " oq-settings-action-note--warning" : ""}" data-oq-frequency-limit-warning="${escapeHtml(key)}" role="status" aria-live="polite" ${warning.text ? "" : "hidden"}>${escapeHtml(warning.text)}</p>`;
  }

  export function renderSilentSettingsGrid(className = "oq-settings-grid") {
    return `
      <div class="${escapeHtml(className)}">
        ${renderSettingsTimeField("silentStartTime", t("settingsSilent.startTitle"), t("settingsSilent.startCopy"))}
        ${renderSettingsTimeField("silentEndTime", t("settingsSilent.endTitle"), t("settingsSilent.endCopy"))}
        ${renderSettingsSliderField("silentMaxHz", t("settingsSilent.maxHzNightTitle"), t("settingsSilent.maxHzNightCopy"), "", { footerMarkup: renderFrequencyLimitWarning("silentMaxHz") })}
        ${renderSettingsSliderField("dayMaxHz", t("settingsSilent.maxHzDayTitle"), t("settingsSilent.maxHzDayCopy"), "", { footerMarkup: renderFrequencyLimitWarning("dayMaxHz") })}
      </div>
    `;
  }

  export function renderSettingsSilentSection() {
    return renderSettingsSection(
      t("settingsSilent.sectionGroup"),
      t("settingsSilent.sectionTitle"),
      t("settingsSilent.sectionCopy"),
      renderSilentSettingsGrid(),
    );
  }

  export function renderSilentSettingsFields() {
    return renderSilentSettingsGrid("oq-settings-grid oq-settings-grid--modal");
  }
