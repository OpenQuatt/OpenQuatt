import { getEntityValue, hasEntity } from "../core/entity-store.js";
import { createOduGenerationDetectionModel } from "../core/odu-generation.js";
import { state } from "../core/state.js";
import { escapeHtml } from "../core/html.js";
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

export function renderOduGenerationDetectionStatus({ embedded = false } = {}) {
  const model = getOduGenerationDetectionModel();
  const canDetect = model.heatPumps.some((heatPump) => heatPump.detectAvailable);
  if (!model.available && !canDetect) {
    return "";
  }

  const advice = getOduGenerationDetectionAdvice(model);
  const match = model.status === "match";
  const title = !model.complete ? "Detectie onvolledig" : model.mixed ? "Gemengde Duo" : "Automatisch gevonden";
  const badge = match ? "Komt overeen" : model.recommendation ? `Advies ${model.recommendation}` : "Geen advies";
  const badgeTone = match ? " is-success" : model.recommendation ? "" : " is-neutral";
  const busy = state.loadingEntities || Boolean(state.busyAction);
  const detectKeys = model.heatPumps.filter((heatPump) => heatPump.detectAvailable).map((heatPump) => heatPump.detectKey);
  const isDetecting = detectKeys.some((key) => state.busyAction === key) || state.busyAction === "odu-generation-detect-all";
  const detectButton = canDetect
    ? `<button class="oq-gen-reset" type="button" data-oq-action="press-odu-generation-detect-all" aria-label="ODU-generatie opnieuw detecteren" ${busy ? "disabled" : ""} aria-busy="${isDetecting ? "true" : "false"}">${escapeHtml(isDetecting ? "Detecteren…" : "Opnieuw detecteren")}</button>`
    : "";
  const rows = model.heatPumps.map((heatPump) => {
    const value = heatPump.known ? `Quatt ODU ${heatPump.generation}` : "Unknown";
    return `<div class="oq-settings-source-row oq-gen-unit${heatPump.known ? "" : " is-warning"}" data-oq-odu-generation="hp${heatPump.index}"><span class="oq-settings-source-row-label">HP${heatPump.index}</span><strong>${escapeHtml(value)}</strong></div>`;
  }).join("");

  return `<section class="oq-gen-detection oq-settings-field--span-2${embedded ? " is-embedded" : " oq-helper-surface oq-settings-field"}" data-oq-settings-field="oduGenerationDetection" aria-label="ODU-detectie"><div class="oq-gen-hd"><strong>${escapeHtml(title)}</strong><div class="oq-gen-hd-actions"><span class="oq-settings-section-badge oq-gen-badge${badgeTone}">${escapeHtml(badge)}</span>${detectButton}</div></div><div class="oq-gen-units${model.heatPumps.length > 1 ? " is-duo" : ""}" role="group" aria-label="Gedetecteerde buitenunits">${rows}</div>${match ? "" : `<p class="oq-settings-action-note${advice.warning ? " oq-settings-action-note--warning" : ""}" aria-live="polite">${escapeHtml(advice.copy)}</p>`}</section>`;
}
