import { getEntityStateText, hasEntity } from "../core/app-shared.js";
import { getInputDraftValue } from "../core/control-drafts.js";
import { getEntityValue, getNumberMeta, parseLooseNumber } from "../core/entity-store.js";
import { renderNumberInputControl } from "../core/number-controls.js";
import { renderSettingsFieldCard, renderSettingsSection } from "./controls.js";

export function renderSettingsElectricalCurrentLimitSection() {
  if (!hasEntity("electricalCurrentLimit")) {
    return "";
  }

  const topology = String(getEntityStateText("installationTopology") || "").trim().toLowerCase();
  const generation = String(getEntityValue("hpGeneration") || "").trim();
  const maxCurrentA = topology === "duo" && generation === "V2" ? 20 : 16;
  const entityMeta = getNumberMeta("electricalCurrentLimit");
  const currentDraft = parseLooseNumber(getInputDraftValue("electricalCurrentLimit"));
  const currentA = Number.isFinite(currentDraft)
    ? Math.min(maxCurrentA, Math.max(entityMeta.min, currentDraft))
    : maxCurrentA;
  const peakLimitW = Math.round(currentA * (3650 / 16));
  const meta = {
    ...entityMeta,
    max: maxCurrentA,
  };
  const control = renderNumberInputControl({
    key: "electricalCurrentLimit",
    value: currentA,
    meta,
    controlClass: "oq-helper-control oq-helper-control--suffix",
    unitMarkup: '<span class="oq-helper-unit-chip">A</span>',
  });

  return renderSettingsSection(
    "Elektrische installatie",
    "Elektrische ingangsgrens",
    "Begrenst het gezamenlijke elektrische ingangsvermogen van de warmtepompinstallatie.",
    renderSettingsFieldCard(
      "electricalCurrentLimit",
      "Maximale stroom",
      "Een lagere waarde beperkt de warmtepomp(en) eerder. De maximale waarde volgt de gekozen installatie en Quatt Hybrid-versie.",
      control,
      "",
      `<div class="oq-settings-field-note"><p>Actieve piekgrens: circa ${peakLimitW} W bij 230 V.</p><p>Power House gebruikt deze grens vooraf en via gemeten feedback. Stooklijn en koelen gebruiken alleen de gemeten feedback. Dit is een regelgrens, geen elektrische beveiliging.</p></div>`,
    ),
  );
}
