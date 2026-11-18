import { getEntityValue, hasEntity } from "../core/entity-store.js";
import { createOduGenerationDetectionModel } from "../core/odu-generation.js";
import { state } from "../core/state.js";
import { escapeHtml } from "../core/html.js";
import { t } from "../i18n/index.js";
import { getInstallationTopology } from "./device-context.js";

export function getOduGenerationDetectionModel() {
  return createOduGenerationDetectionModel({
    topology: getInstallationTopology(),
    configuredGeneration: getEntityValue("hpGeneration"),
    hp1Available: hasEntity("hp1Generation"),
    hp1Generation: getEntityValue("hp1Generation"),
    hp1DetectAvailable: hasEntity("hp1GenerationDetect"),
    hp1Firmware: getEntityValue("hp1OduFirmware"),
    hp2Available: hasEntity("hp2Generation"),
    hp2Generation: getEntityValue("hp2Generation"),
    hp2DetectAvailable: hasEntity("hp2GenerationDetect"),
    hp2Firmware: getEntityValue("hp2OduFirmware"),
  });
}

export function getOduGenerationDetectionAdvice(model = getOduGenerationDetectionModel()) {
  if (model.status === "unknown") {
    return {
      warning: true,
      copy: t("oduGeneration.adviceUnknown"),
    };
  }
  if (model.status === "mixed") {
    return {
      warning: true,
      copy: t("oduGeneration.adviceMixed"),
    };
  }
  if (model.status === "mismatch") {
    return {
      warning: true,
      copy: t("oduGeneration.adviceMismatch", { recommendation: model.recommendation, configured: model.configuredGeneration }),
    };
  }
  if (model.status === "match") {
    return {
      warning: false,
      copy: t("oduGeneration.adviceMatch", { recommendation: model.recommendation }),
    };
  }
  if (model.status === "detected") {
    return {
      warning: false,
      copy: t("oduGeneration.adviceDetected", { recommendation: model.recommendation }),
    };
  }
  return {
    warning: true,
    copy: t("oduGeneration.adviceNone"),
  };
}

export function getOduGenerationChoiceMeta(option, selectedGeneration, recommendedGeneration) {
  const selected = option === selectedGeneration;
  const recommended = option === recommendedGeneration;
  if (selected && recommended) return t("oduGeneration.metaSelectedRecommended");
  if (recommended) return t("oduGeneration.metaRecommended");
  if (selected) return t("oduGeneration.metaSelected");
  return "";
}

function renderOduUnits(model, detecting = false) {
  const rows = model.heatPumps.map(({ index, generation, firmwareVersion, known }) => {
    const label = detecting ? t("oduGeneration.rowDetecting") : known ? generation : t("common.unknown");
    const firmware = !detecting && firmwareVersion
      ? `<span class="oq-gen-unit-firmware">${escapeHtml(t("oduGeneration.firmware", { version: firmwareVersion }))}</span>`
      : "";
    return `
      <li class="oq-gen-unit" data-oq-odu-generation="hp${index}">
        <span class="oq-gen-unit-label">${escapeHtml(t("oduGeneration.unit", { index }))}</span>
        <div class="oq-gen-unit-value">
          <strong>${escapeHtml(label)}</strong>
          ${firmware}
        </div>
      </li>
    `;
  }).join("");
  return `<ul class="oq-gen-units${model.heatPumps.length > 1 ? " is-duo" : ""}" role="list" aria-label="${escapeHtml(t("oduGeneration.unitsAria"))}">${rows}</ul>`;
}

export function renderOduFirmwareSummary(model) {
  if (!model.heatPumps.some(({ known, firmwareVersion }) => known || firmwareVersion)) {
    return "";
  }
  return `
    <div class="oq-gen-summary">
      <p class="oq-settings-quickstart-status-label">${escapeHtml(t("oduGeneration.unitsAria"))}</p>
      ${renderOduUnits(model)}
    </div>
  `;
}

export function renderOduGenerationDetectionStatus({ embedded = false } = {}) {
  const model = getOduGenerationDetectionModel();
  const canDetect = model.heatPumps.some((heatPump) => heatPump.detectAvailable);
  if (!model.available && !canDetect && !model.heatPumps.some(({ firmwareVersion }) => firmwareVersion)) {
    return "";
  }

  const busy = state.loadingEntities || Boolean(state.busyAction);
  const detectKeys = model.heatPumps.filter((heatPump) => heatPump.detectAvailable).map((heatPump) => heatPump.detectKey);
  const isDetecting = detectKeys.some((key) => state.busyAction === key) || state.busyAction === "odu-generation-detect-all";
  const advice = isDetecting
    ? {
        warning: false,
        copy: model.heatPumps.length > 1
          ? t("oduGeneration.detectingDuo")
          : t("oduGeneration.detectingSingle"),
      }
    : getOduGenerationDetectionAdvice(model);
  const match = !isDetecting && model.status === "match";
  const title = isDetecting
    ? t("oduGeneration.titleDetecting")
    : !model.complete ? t("oduGeneration.titleIncomplete") : model.mixed ? t("oduGeneration.titleMixed") : t("oduGeneration.titleFound");
  const badge = isDetecting
    ? t("oduGeneration.badgeWait")
    : match ? t("oduGeneration.badgeMatch") : model.recommendation ? t("oduGeneration.badgeAdvice", { recommendation: model.recommendation }) : t("oduGeneration.badgeNone");
  const badgeTone = match ? " is-success" : model.recommendation || isDetecting ? "" : " is-neutral";
  const detectButton = canDetect
    ? `<button class="oq-gen-reset" type="button" data-oq-action="press-odu-generation-detect-all" aria-label="${escapeHtml(t("oduGeneration.detectAria"))}" ${busy ? "disabled" : ""} aria-busy="${isDetecting ? "true" : "false"}">${escapeHtml(isDetecting ? t("oduGeneration.detectingLabel") : t("oduGeneration.redetectLabel"))}</button>`
    : "";
  return `<section class="oq-gen-detection oq-settings-field--span-2${embedded ? " is-embedded" : " oq-helper-surface oq-settings-field"}" data-oq-settings-field="oduGenerationDetection" aria-label="${escapeHtml(t("oduGeneration.sectionAria"))}"><div class="oq-gen-hd"><strong>${escapeHtml(title)}</strong><div class="oq-gen-hd-actions"><span class="oq-settings-section-badge oq-gen-badge${badgeTone}">${escapeHtml(badge)}</span>${detectButton}</div></div>${renderOduUnits(model, isDetecting)}${match ? "" : `<p class="oq-settings-action-note${advice.warning ? " oq-settings-action-note--warning" : ""}" aria-live="polite">${escapeHtml(advice.copy)}</p>`}</section>`;
}
