import { renderSettingsSection, renderSettingsSliderField, renderSettingsTimeField } from "./controls.js";
import { escapeHtml } from "../core/html.js";

  export function renderSilentSettingsGrid(className = "oq-settings-grid") {
    return `
      <div class="${escapeHtml(className)}">
        ${renderSettingsTimeField("silentStartTime", "Start stille uren", "Vanaf dit tijdstip werkt het systeem in stille modus.")}
        ${renderSettingsTimeField("silentEndTime", "Einde stille uren", "Vanaf dit tijdstip stopt de stille modus weer.")}
        ${renderSettingsSliderField("silentMaxHz", "Maximale compressorfrequentie tijdens stille uren", "Bepaalt hoe snel de compressor maximaal mag draaien tijdens stille uren.")}
        ${renderSettingsSliderField("dayMaxHz", "Maximale compressorfrequentie overdag", "Bepaalt hoe snel de compressor overdag maximaal mag draaien.")}
      </div>
    `;
  }

  export function renderSettingsSilentSection() {
    return renderSettingsSection(
      "Comfort",
      "Stille uren",
      "Kies wanneer het systeem stiller moet werken en begrens de compressorfrequentie.",
      renderSilentSettingsGrid(),
    );
  }

  export function renderSilentSettingsFields() {
    return renderSilentSettingsGrid("oq-settings-grid oq-settings-grid--modal");
  }
