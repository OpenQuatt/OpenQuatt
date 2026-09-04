import { getEntityStateText, hasEntity } from "../core/app-shared.js";
import { getInputDraftValue } from "../core/control-drafts.js";
import { getEntityValue, getNumberMeta, parseLooseNumber } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { createOduGenerationDetectionModel } from "../core/odu-generation.js";
import { renderNumberInputControl } from "../core/number-controls.js";
import { state } from "../core/state.js";
import { renderSettingsFieldCard, renderSettingsSection } from "./controls.js";

export const ELECTRICAL_LIMIT_KNOWN_GENERATIONS = ["V1", "V1.5", "V2"];
export const ELECTRICAL_LIMIT_MIN_A = 10;
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
  let standardLabel = "Single";
  if (isDuo && isV2) {
    standardLabel = "Duo V2";
  } else if (isDuo && generationKnown) {
    standardLabel = "Duo V1/V1.5";
  } else if (isDuo) {
    standardLabel = "Duo (onbekende versie)";
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
  return `${((numeric * 230) / 1000).toFixed(1).replace(".", ",")} kW`;
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
  return `Let op: deze backup zet de elektrische ingangsgrens op ${formatDutchAmps(backupA)}, boven de standaard ${formatDutchAmps(info.standardA)} voor deze installatie (${info.standardLabel}). Herstellen vereist dezelfde controle als handmatig verhogen: bevestig alleen wanneer de volledige elektrische aansluiting hiervoor geschikt is. Alleen een zwaardere installatieautomaat plaatsen is niet voldoende.`;
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
  const busy = state.busyAction === "save-electricalCurrentLimit" || state.busyAction === "electricalCurrentLimitReset";
  const button = view.showRestore
    ? `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="reset-electrical-limit-to-default" ${busy || state.loadingEntities ? "disabled" : ""}>Standaardwaarde herstellen (${formatDutchAmps(view.info.standardA)})</button>`
    : "";
  return `<div class="oq-settings-electrical-restore">${button}</div>`;
}

export function renderElectricalLimitFooter(view = resolveElectricalLimitView()) {
  const { info, currentA } = view;
  const warningMarkup = view.aboveStandard
    ? `<div class="oq-settings-electrical-warning" role="alert"><span class="oq-settings-cooling-limit-warning-icon" aria-hidden="true">!</span><div class="oq-settings-electrical-warning-copy"><strong>Hogere waarde dan de standaard elektrische aansluiting</strong><span>Je hebt een waarde gekozen boven de standaard ${formatDutchAmps(info.standardA)} voor een ${escapeHtml(info.standardLabel)}. Verhoog deze grens alleen wanneer de warmtepomp is aangesloten op een daarvoor ontworpen, zwaarder afgezekerde groep en ook de bekabeling, werkschakelaar en het overige aansluitmateriaal hiervoor geschikt zijn. Alleen de installatieautomaat vervangen door een zwaarder exemplaar is niet voldoende en kan gevaarlijk zijn.</span></div></div>`
    : "";
  const belowMarkup = !view.aboveStandard && view.belowStandard
    ? `<p class="oq-settings-electrical-note">Een lagere waarde kan het maximale verwarmings- en koelvermogen beperken.</p>`
    : "";
  return `<div class="oq-settings-electrical-body"><div class="oq-settings-electrical-facts"><div class="oq-settings-electrical-fact"><span>Standaard voor deze installatie</span><strong>${formatDutchAmps(info.standardA)} · ${escapeHtml(info.standardLabel)}</strong></div><div class="oq-settings-electrical-fact"><span>Indicatief vermogen bij 230 V</span><strong>circa ${formatIndicativeKw(currentA)}</strong></div></div><p class="oq-settings-electrical-caption">Benadering op basis van de ingestelde stroom (${formatDutchAmps(currentA)}); geen gegarandeerde harde begrenzing.</p>${warningMarkup}${belowMarkup}<p class="oq-settings-electrical-safety"><strong>Let op:</strong> dit is een softwarematige regelgrens en geen elektrische beveiliging. De groepzekering, bekabeling en elektrische aansluiting moeten altijd geschikt zijn voor de ingestelde stroom. Korte stroompieken boven de ingestelde waarde zijn niet volledig uit te sluiten.</p></div>`;
}

export function renderSettingsElectricalCurrentLimitSection() {
  if (!hasEntity("electricalCurrentLimit")) {
    return "";
  }

  const view = resolveElectricalLimitView();
  const control = renderNumberInputControl({
    key: "electricalCurrentLimit",
    value: view.currentA,
    meta: view.meta,
    controlClass: "oq-helper-control oq-helper-control--suffix",
    unitMarkup: '<span class="oq-helper-unit-chip">A</span>',
  });

  return renderSettingsSection(
    "Elektrische installatie",
    "Elektrische ingangsgrens",
    "Beperk de gezamenlijke elektrische belasting van de buitenunits. OpenQuatt verlaagt zo nodig het compressorvermogen om het stroomverbruik rond deze grens te houden.",
    renderSettingsFieldCard(
      "electricalCurrentLimit",
      "Maximale gezamenlijke netstroom",
      "Deze grens geldt voor alle buitenunits samen, niet per warmtepomp. Power House gebruikt de grens vooraf bij de vermogensverdeling en regelt daarna bij op basis van gemeten feedback. Stooklijnbedrijf en koelen gebruiken alleen de gemeten feedback. Een lagere waarde kan het beschikbare verwarmings- en koelvermogen beperken. Door meetvertraging en korte stroompieken kan de werkelijke stroom tijdelijk boven de ingestelde waarde komen. Dit is een softwarematige regelgrens en geen elektrische beveiliging.",
      `${control}${renderElectricalLimitRestore(view)}`,
      "",
      renderElectricalLimitFooter(view),
    ),
  );
}
