import { getEntityStateText, hasEntity } from "../core/app-shared.js";
import { getInputDraftValue } from "../core/control-drafts.js";
import { renderOqIcon } from "../core/config.js";
import { getEntityValue, getNumberMeta, parseLooseNumber } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { createOduGenerationDetectionModel } from "../core/odu-generation.js";
import { state } from "../core/state.js";
import { formatNumber, t } from "../i18n/index.js";
import { renderSettingsFieldCard, renderSettingsSection } from "./controls.js";

export const ELECTRICAL_LIMIT_KNOWN_GENERATIONS = ["V1", "V1.5", "V2"];
export const ELECTRICAL_LIMIT_MIN_A = 6;
// Absolute OpenQuatt ceiling for Duo V2 (2 x 13 A published per-ODU max);
// spiegelt oq_duo_current_limit_v2_max_a. De officiële Quatt Duo-specificatie
// (20 A) blijft de standaard- en waarschuwingsgrens.
export const ELECTRICAL_LIMIT_V2_MAX_A = 26;

export function detectionConfirmsFamily(topology, configuredGeneration, isV2) {
  const model = createOduGenerationDetectionModel({
    topology,
    configuredGeneration,
    hp1Available: hasEntity("hp1Generation"),
    hp1Generation: getEntityValue("hp1Generation"),
    hp1DetectAvailable: hasEntity("hp1GenerationDetect"),
    hp2Available: hasEntity("hp2Generation"),
    hp2Generation: getEntityValue("hp2Generation"),
    hp2DetectAvailable: hasEntity("hp2GenerationDetect"),
  });
  const duo = String(topology || "").trim().toLowerCase() === "duo";
  if (model.heatPumps.length !== (duo ? 2 : 1)) {
    return false;
  }
  return model.heatPumps.every((heatPump) => heatPump.known
    && (isV2 ? heatPump.generation === "V2" : (heatPump.generation === "V1" || heatPump.generation === "V1.5")));
}

export function getElectricalLimitTopologyInfo() {
  const topology = String(getEntityStateText("installationTopology") || "").trim().toLowerCase();
  const generation = String(getEntityValue("hpGeneration") || "").trim();
  const isDuo = topology === "duo";
  const isV2 = generation === "V2";
  const generationKnown = ELECTRICAL_LIMIT_KNOWN_GENERATIONS.includes(generation);
  const standardA = isDuo && isV2 ? 20 : 16;
  // Een verhoogde grens wordt alleen vrijgegeven wanneer de geconfigureerde
  // familie overeenkomt met de betrouwbaar gedetecteerde ODU-familie. Alleen
  // een (altijd aanwezige) hp_generation-selectie is nooit voldoende.
  const elevationConfirmed = isDuo && generationKnown && detectionConfirmsFamily(topology, generation, isV2);
  // Absolute OpenQuatt ceilings, derived from the published per-ODU maxima
  // (2 x 10 A V1/V1.5, 2 x 13 A V2). De officiële Duo-specificatie (16/20 A)
  // blijft de standaard. Zonder bevestigde detectie blijft de
  // installatiestandaard het plafond.
  const absoluteMaxA = elevationConfirmed ? (isV2 ? ELECTRICAL_LIMIT_V2_MAX_A : 20) : standardA;
  let standardLabel = t("settingsElectrical.topoSingle");
  if (isDuo && isV2) {
    standardLabel = t("settingsElectrical.topoDuoV2");
  } else if (isDuo && generationKnown) {
    standardLabel = t("settingsElectrical.topoDuoV1");
  } else if (isDuo) {
    standardLabel = t("settingsElectrical.topoDuoUnknown");
  }
  return { topology, generation, isDuo, isV2, generationKnown, standardA, absoluteMaxA, standardLabel };
}

export function formatDutchAmps(value) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) {
    return "—";
  }
  const text = numeric.toFixed(1).replace(".", ",");
  return `${text.endsWith(",0") ? text.slice(0, -2) : text} A`;
}

export function formatIndicativeKw(currentA) {
  const numeric = Number(currentA);
  if (!Number.isFinite(numeric)) {
    return "—";
  }
  return `${formatNumber((numeric * 230) / 1000, { minimumFractionDigits: 1, maximumFractionDigits: 1 })} kW`;
}

export function isElectricalLimitAboveStandard(value, info = null) {
  const resolved = info || getElectricalLimitTopologyInfo();
  const numeric = Number(value);
  return Number.isFinite(numeric) && numeric > resolved.standardA + 1e-9;
}

export function getElectricalLimitChangePlan(rawValue, committedRaw, minimumA = ELECTRICAL_LIMIT_MIN_A, info = null) {
  const resolved = info || getElectricalLimitTopologyInfo();
  const minimum = Number.isFinite(Number(minimumA)) ? Number(minimumA) : ELECTRICAL_LIMIT_MIN_A;
  const numeric = parseLooseNumber(rawValue);
  if (!Number.isFinite(numeric)) {
    return { valid: false, clamped: Number.NaN, fromA: Number.NaN, requiresConfirmation: false, info: resolved };
  }
  const clamped = Math.min(resolved.absoluteMaxA, Math.max(minimum, numeric));
  const committed = parseLooseNumber(committedRaw);
  const fromA = Number.isFinite(committed)
    ? Math.min(resolved.absoluteMaxA, Math.max(minimum, committed))
    : resolved.standardA;
  return {
    valid: true,
    clamped,
    toA: clamped,
    fromA,
    requiresConfirmation: clamped > resolved.standardA + 1e-9,
    info: resolved,
  };
}

function resolveCommittedCurrentA(info) {
  // NB: bewust niet via getEntityValue(): die geeft een open invoer-draft
  // terug, terwijl hier de laatst bevestigde waarde nodig is.
  const committed = parseLooseNumber(getCommittedElectricalLimitRaw());
  if (Number.isFinite(committed)) {
    return Math.min(info.absoluteMaxA, Math.max(ELECTRICAL_LIMIT_MIN_A, committed));
  }
  return info.standardA;
}

export function getCommittedElectricalLimitRaw() {
  const entity = state.entities.electricalCurrentLimit || {};
  return entity.value ?? entity.state ?? "";
}

export function getElectricalLimitBackupRestoreWarning(settings) {
  const info = getElectricalLimitTopologyInfo();
  const sections = settings && typeof settings === "object" ? Object.values(settings) : [];
  let backupRaw;
  for (const section of sections) {
    if (section && typeof section === "object"
      && Object.prototype.hasOwnProperty.call(section, "electricalCurrentLimit")) {
      backupRaw = section.electricalCurrentLimit;
      break;
    }
  }
  const backupA = parseLooseNumber(backupRaw);
  if (!Number.isFinite(backupA) || backupA <= info.standardA + 1e-9) {
    return "";
  }
  return t("settingsElectrical.backupWarning", { limit: formatDutchAmps(backupA), standard: formatDutchAmps(info.standardA), label: info.standardLabel });
}

export function resolveElectricalLimitView() {
  const info = getElectricalLimitTopologyInfo();
  const entityMeta = getNumberMeta("electricalCurrentLimit");
  const minA = Number.isFinite(entityMeta.min) ? entityMeta.min : ELECTRICAL_LIMIT_MIN_A;
  const draftRaw = getInputDraftValue("electricalCurrentLimit");
  const draftParsed = parseLooseNumber(draftRaw);
  const committedA = resolveCommittedCurrentA(info);
  const pendingTo = Number(state.pendingElectricalLimit?.toA);
  const effectiveRaw = Number.isFinite(draftParsed)
    ? draftParsed
    : Number.isFinite(pendingTo) ? pendingTo : committedA;
  const currentA = Math.min(info.absoluteMaxA, Math.max(minA, effectiveRaw));
  return {
    info,
    minA,
    committedA,
    currentA,
    meta: { ...entityMeta, min: minA, max: info.absoluteMaxA },
    aboveStandard: currentA > info.standardA + 1e-9,
    belowStandard: currentA < info.standardA - 1e-9,
    showRestore: Math.abs(committedA - info.standardA) > 1e-9 || Math.abs(currentA - info.standardA) > 1e-9,
  };
}

export function renderElectricalLimitRestore(view) {
  const busy = state.busyAction === "save-electricalCurrentLimit";
  const button = view.showRestore
    ? `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="reset-electrical-limit-to-default" ${busy || state.loadingEntities ? "disabled" : ""}>${escapeHtml(t("settingsElectrical.restoreButton", { limit: formatDutchAmps(view.info.standardA) }))}</button>`
    : "";
  return `<div class="oq-settings-electrical-restore">${button}</div>`;
}

export function renderElectricalLimitEntry(view = resolveElectricalLimitView()) {
  const { info, currentA } = view;
  const span = Math.max(1e-9, info.absoluteMaxA - view.minA);
  const standardPct = ((info.standardA - view.minA) / span) * 100;
  return `<div class="oq-settings-electrical-entry"><span class="oq-settings-electrical-entry-label">${renderOqIcon("pencil", "oq-settings-electrical-label-icon")}${escapeHtml(t("settingsElectrical.entryLabel"))}</span><div class="oq-settings-electrical-slider-block"><div class="oq-helper-slider-meta"><span>${formatDutchAmps(view.minA)}</span><strong>${formatDutchAmps(currentA)}</strong><span>${formatDutchAmps(info.absoluteMaxA)}</span></div><div class="oq-settings-electrical-slider-track" style="--oq-electrical-standard-pct:${standardPct.toFixed(1)}%"><input class="oq-helper-range oq-settings-electrical-slider" type="range" data-oq-field="electricalCurrentLimit" min="${view.meta.min}" max="${view.meta.max}" step="${view.meta.step}" value="${escapeHtml(currentA)}" aria-label="${escapeHtml(t("settingsElectrical.entryLabel"))}" ${state.loadingEntities ? "disabled" : ""}></div></div><span class="oq-settings-electrical-entry-caption">${escapeHtml(t("settingsElectrical.entryCaption", { limit: formatDutchAmps(info.standardA), label: info.standardLabel }))}</span></div>`;
}

export function renderElectricalLimitEstimate(view = resolveElectricalLimitView()) {
  return `<div class="oq-settings-electrical-estimate"><span>${renderOqIcon("calculator", "oq-settings-electrical-label-icon")}${escapeHtml(t("settingsElectrical.estimateLabel"))}</span><strong>${escapeHtml(t("settingsElectrical.estimateValue", { value: formatIndicativeKw(view.currentA) }))}</strong><em>${escapeHtml(t("settingsElectrical.estimateNote"))}</em></div>`;
}

export function renderElectricalLimitFooter(view = resolveElectricalLimitView()) {
  const { info } = view;
  const warningMarkup = view.aboveStandard
    ? `<div class="oq-settings-electrical-warning" role="alert"><span class="oq-settings-cooling-limit-warning-icon" aria-hidden="true">!</span><div class="oq-settings-electrical-warning-copy"><strong>${escapeHtml(t("settingsElectrical.warningTitle"))}</strong><span>${escapeHtml(t("settingsElectrical.warningCopy", { limit: formatDutchAmps(info.standardA), label: info.standardLabel }))}</span></div></div>`
    : "";
  const belowMarkup = !view.aboveStandard && view.belowStandard
    ? `<p class="oq-settings-electrical-note">${escapeHtml(t("settingsElectrical.belowNote"))}</p>`
    : "";
  return `<div class="oq-settings-electrical-body">${warningMarkup}${belowMarkup}<p class="oq-settings-electrical-safety">${renderOqIcon("triangle-alert", "oq-settings-electrical-safety-icon")}<strong>${escapeHtml(t("settingsElectrical.safetyPrefix"))}</strong> ${escapeHtml(t("settingsElectrical.safetyCopy"))}</p></div>`;
}

export function renderSettingsElectricalCurrentLimitSection() {
  if (!hasEntity("electricalCurrentLimit")) {
    return "";
  }

  const view = resolveElectricalLimitView();

  return renderSettingsSection(
    t("settingsElectrical.sectionGroup"),
    t("settingsElectrical.sectionTitle"),
    t("settingsElectrical.sectionCopy"),
    renderSettingsFieldCard(
      "electricalCurrentLimit",
      t("settingsElectrical.cardTitle"),
      t("settingsElectrical.cardCopy"),
      `<div class="oq-settings-electrical-control-row">${renderElectricalLimitEntry(view)}${renderElectricalLimitEstimate(view)}</div>${renderElectricalLimitRestore(view)}`,
      "",
      renderElectricalLimitFooter(view),
    ),
  );
}
