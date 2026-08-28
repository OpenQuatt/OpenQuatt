import { renderSettingsSection, renderSettingsSliderField, renderSettingsTimeField } from "./controls.js";
import { hasEntity } from "../core/app-shared.js";
import { escapeHtml } from "../core/html.js";

  export function renderSilentSettingsGrid(className = "oq-settings-grid") {
    const frequencyCaps = hasEntity("silentHeatingMaxHz")
      ? `
        ${renderSettingsSliderField("silentHeatingMaxHz", "Maximale compressorfrequentie verwarmen tijdens stille uren", "Bepaalt hoe snel de compressor bij verwarmen maximaal mag draaien tijdens stille uren.")}
        ${renderSettingsSliderField("silentCoolingMaxHz", "Maximale compressorfrequentie koelen tijdens stille uren", "Bepaalt hoe snel de compressor bij koelen maximaal mag draaien tijdens stille uren.")}
        ${renderSettingsSliderField("dayHeatingMaxHz", "Maximale compressorfrequentie verwarmen overdag", "Bepaalt hoe snel de compressor bij verwarmen overdag maximaal mag draaien.")}
        ${renderSettingsSliderField("dayCoolingMaxHz", "Maximale compressorfrequentie koelen overdag", "Bepaalt hoe snel de compressor bij koelen overdag maximaal mag draaien.")}
      `
      : `
        ${renderSettingsSliderField("silentMax", "Maximaal niveau tijdens stille uren", "Zo ver mag het systeem nog opschalen tijdens stille uren.")}
        ${renderSettingsSliderField("dayMax", "Maximaal niveau overdag", "Zo ver mag het systeem overdag opschalen.")}
      `;
    return `
      <div class="${escapeHtml(className)}">
        ${renderSettingsTimeField("silentStartTime", "Start stille uren", "Vanaf dit tijdstip werkt het systeem in stille modus.")}
        ${renderSettingsTimeField("silentEndTime", "Einde stille uren", "Vanaf dit tijdstip stopt de stille modus weer.")}
        ${frequencyCaps}
      </div>
    `;
  }

  export function renderSettingsSilentSection() {
    return renderSettingsSection(
      "Comfort",
      "Stille uren",
      "Kies wanneer het systeem stiller moet werken en begrens verwarmen en koelen onafhankelijk op compressorfrequentie.",
      renderSilentSettingsGrid(),
    );
  }

  export function renderSilentSettingsFields() {
    return renderSilentSettingsGrid("oq-settings-grid oq-settings-grid--modal");
  }
