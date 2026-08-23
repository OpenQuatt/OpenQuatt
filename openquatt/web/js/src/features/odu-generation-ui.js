import { getEntityValue, hasEntity } from "../core/entity-store.js";
import { createOduGenerationDetectionModel } from "../core/odu-generation.js";
import { state } from "../core/state.js";
import { escapeHtml } from "../core/html.js";
import { renderSettingsFieldCard } from "../settings/controls.js";
import { getInstallationTopology } from "./device-context.js";

export function getOduGenerationDetectionModel() {
  return createOduGenerationDetectionModel({
    topology: getInstallationTopology(),
    configuredGeneration: getEntityValue("hpGeneration"),
    hp1Available: hasEntity("hp1Generation"),
    hp1Generation: getEntityValue("hp1Generation"),
    hp1DetectAvailable: hasEntity("hp1GenerationDetect"),
    hp2Available: hasEntity("hp2Generation"),
    hp2Generation: getEntityValue("hp2Generation"),
    hp2DetectAvailable: hasEntity("hp2GenerationDetect"),
  });
}

export function getOduGenerationDetectionAdvice(model = getOduGenerationDetectionModel()) {
  if (model.status === "unknown") {
    return {
      warning: true,
      copy: "Niet alle buitenunits zijn herkend. Daarom wordt geen versie geadviseerd.",
    };
  }
  if (model.status === "mixed") {
    return {
      warning: true,
      copy: "Deze combinatie geeft geen veilig advies. Controleer de gekozen versie.",
    };
  }
  if (model.status === "mismatch") {
    return {
      warning: true,
      copy: `${model.recommendation} wordt aanbevolen; ${model.configuredGeneration} is geselecteerd. Er wordt niets automatisch gewijzigd.`,
    };
  }
  if (model.status === "match") {
    return {
      warning: false,
      copy: `${model.recommendation} komt overeen met de detectie.`,
    };
  }
  if (model.status === "detected") {
    return {
      warning: false,
      copy: `${model.recommendation} wordt aanbevolen. Selecteer deze versie om de keuze op te slaan.`,
    };
  }
  return {
    warning: true,
    copy: "Nog geen betrouwbare detectie; er wordt geen versie geadviseerd.",
  };
}

export function getOduGenerationChoiceMeta(option, selectedGeneration, recommendedGeneration) {
  const selected = option === selectedGeneration;
  const recommended = option === recommendedGeneration;
  if (selected && recommended) return "Geselecteerd · aanbevolen";
  if (recommended) return "Aanbevolen";
  if (selected) return "Geselecteerd";
  return "";
}

export function renderOduGenerationDetectionStatus() {
  const model = getOduGenerationDetectionModel();
  const canDetect = model.heatPumps.some((heatPump) => heatPump.detectAvailable);
  if (!model.available && !canDetect) {
    return "";
  }

  const advice = getOduGenerationDetectionAdvice(model);
  const title = !model.complete
    ? "Detectie onvolledig"
    : model.mixed ? "Gemengde Duo gedetecteerd" : "Automatisch gevonden";
  const busy = state.loadingEntities || Boolean(state.busyAction);
  const rows = model.heatPumps.map((heatPump) => {
    const value = heatPump.known ? `Quatt ODU ${heatPump.generation}` : "Unknown";
    const detecting = state.busyAction === heatPump.detectKey;
    const action = heatPump.detectAvailable
      ? `<button class="oq-settings-info-button oq-settings-generation-redetect" type="button" data-oq-action="press-named-button" data-oq-button-key="${escapeHtml(heatPump.detectKey)}" aria-label="${escapeHtml(detecting ? `HP${heatPump.index} wordt opnieuw gedetecteerd` : `Detecteer HP${heatPump.index} opnieuw`)}" ${detecting ? 'aria-busy="true"' : ""} ${busy ? "disabled" : ""}>${detecting ? "…" : "↻"}</button>`
      : "";
    return `
      <div class="oq-settings-source-row${heatPump.known ? "" : " is-warning"}" data-oq-odu-generation="hp${heatPump.index}">
        <span class="oq-settings-source-row-label">HP${heatPump.index}${action}</span>
        <strong>${escapeHtml(value)}</strong>
      </div>
    `;
  }).join("");

  return renderSettingsFieldCard(
    "oduGenerationDetection",
    title,
    "",
    `
      <div class="oq-settings-quickstart-status">
        <div class="oq-settings-source-rows" role="group" aria-label="Gedetecteerde buitenunits en advies">
          ${rows}
          <div class="oq-settings-source-row${model.recommendation ? " is-warning" : ""}">
            <span class="oq-settings-source-row-label">${model.mixed && model.recommendation ? "Gemengde Duo · advies" : "Advies"}</span>
            <strong>${escapeHtml(model.recommendation || "Geen advies")}</strong>
          </div>
        </div>
        <p class="oq-settings-action-note${advice.warning ? " oq-settings-action-note--warning" : ""}" aria-live="polite">${escapeHtml(advice.copy)}</p>
      </div>
    `,
    "oq-settings-field--span-2 oq-settings-field--compact",
  );
}
