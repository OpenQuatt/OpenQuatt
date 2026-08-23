import { getEntityValue, hasEntity } from "../core/entity-store.js";
import { createOduGenerationDetectionModel } from "../core/odu-generation.js";
import { state } from "../core/state.js";
import { escapeHtml } from "../core/html.js";
import { renderNamedActionButton, renderSettingsFieldCard, renderSettingsSystemRow } from "../settings/controls.js";
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
      copy: "Niet alle aangesloten warmtepompen zijn betrouwbaar herkend. Daarom wordt geen versie geadviseerd; kies alleen handmatig als je het type zeker weet.",
    };
  }
  if (model.status === "mixed") {
    return {
      warning: true,
      copy: "De warmtepompen hebben verschillende generaties. Voor deze combinatie wordt geen versie geadviseerd; controleer de handmatige keuze.",
    };
  }
  if (model.status === "mismatch") {
    return {
      warning: true,
      copy: `Aanbevolen: ${model.recommendation}. Nu geselecteerd: ${model.configuredGeneration}. De selectie wordt niet automatisch gewijzigd.`,
    };
  }
  if (model.status === "match") {
    return {
      warning: false,
      copy: `${model.recommendation} is geselecteerd en komt overeen met de automatische detectie.`,
    };
  }
  if (model.status === "detected") {
    return {
      warning: false,
      copy: `Aanbevolen: ${model.recommendation}. Kies deze versie hieronder om de instelling expliciet op te slaan.`,
    };
  }
  return {
    warning: true,
    copy: "Nog geen betrouwbare detectiestatus ontvangen. Er wordt geen versie geadviseerd.",
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
  const mixedLabel = model.mixed
    ? `Gemengde Duo: ${model.heatPumps.map((heatPump) => `HP${heatPump.index} ${heatPump.generation}`).join(" · ")}.`
    : "";
  const rows = model.heatPumps.map((heatPump) => {
    const value = heatPump.known ? `Quatt ODU ${heatPump.generation}` : "Unknown";
    const note = heatPump.known
      ? "Automatisch door de firmware gedetecteerd."
      : heatPump.available
        ? "Het type kon niet betrouwbaar worden vastgesteld."
        : "Nog geen detectiestatus beschikbaar.";
    const busy = state.loadingEntities || Boolean(state.busyAction);
    const action = heatPump.detectAvailable
      ? renderNamedActionButton(
          heatPump.detectKey,
          state.busyAction === heatPump.detectKey ? "Detecteren..." : "Opnieuw detecteren",
          "oq-helper-button oq-helper-button--ghost",
          busy,
        )
      : "";
    return renderSettingsSystemRow({
      label: `HP${heatPump.index}`,
      value,
      note,
      action,
      dataAttribute: "data-oq-odu-generation",
      dataValue: `hp${heatPump.index}`,
    });
  }).join("");

  return renderSettingsFieldCard(
    "oduGenerationDetection",
    "Automatisch gevonden",
    "OpenQuatt leest de ODU-generatie uit de buitenunit. De handmatige selectie blijft apart en wordt nooit stilzwijgend aangepast.",
    `
      <div class="oq-settings-system-summary">${rows}</div>
      ${mixedLabel ? `<p class="oq-settings-action-note">${escapeHtml(mixedLabel)}</p>` : ""}
      <p class="${advice.warning ? "oq-settings-source-warning" : "oq-settings-action-note"}">${escapeHtml(advice.copy)}</p>
    `,
    "oq-settings-field--span-2",
  );
}
