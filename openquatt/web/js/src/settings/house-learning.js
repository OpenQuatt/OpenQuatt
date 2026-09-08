import { getEntityNumericValue, getEntityStateText, hasEntity } from "../core/app-shared.js";
import { formatNumericState } from "../core/formatting.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { renderStatCard } from "../views/stat-card.js";
import { renderNamedActionButton, renderSettingsAdvancedDisclosure, renderSettingsSwitchField, renderSettingsSystemRow } from "./controls.js";
import { renderHouseLearningChart } from "./house-learning-chart.js";

const metric = (value, unit = "") => value != null && Number.isFinite(Number(value))
  ? `${Number(value).toFixed(1).replace(/\.0$/, "")} ${unit}`.trim()
  : "—";
const REASON_LABELS = {
  essential_source: "bron ontbreekt",
  source_stale: "bron verouderd",
  not_heating: "geen verwarming",
  boiler_heat: "ketelwarmte",
  defrost_or_oil_return: "ontdooien/olieretour",
  control_mode: "regelmodus",
  active_limit: "begrenzing",
  service_or_ota: "service/update",
  cooling: "koeling",
  source_uncertain: "bron onzeker",
  setpoint_recovery: "setpointwijziging",
  external_heat_not_excluded: "ketelbijdrage nog niet uitgesloten",
  persistence_unavailable: "opslag ontbreekt",
  not_enough_samples: "meer data nodig",
  outside_span: "te weinig buitenspreiding",
  heat_span: "te weinig vermogensspreiding",
  unobservable: "model niet bepaalbaar",
  parameter_bounds: "buiten modelgrenzen",
  residual_rms: "te veel modelruis",
  residual_bias: "te veel modelbias",
  invalid_state: "nog geen bruikbaar model",
  recent_data_invalid: "metingen ongeldig",
  stale_model: "model verouderd",
  model_context_mismatch: "modelcontext verschilt",
  insufficient_shared_temperature_range: "te weinig gedeeld temperatuurbereik",
  model_disagreement: "batch- en 1R1C-model verschillen",
  thermal_storage_not_stationary: "warmteopslag nog niet stabiel",
};
const MODEL_REASONS = ["model_context_mismatch", "insufficient_shared_temperature_range", "model_disagreement", "thermal_storage_not_stationary"];
const reasons = (values, fallback) => {
  const unique = [...new Set((values || []).map((value) => REASON_LABELS[value] || "onbekende blokkade"))];
  return unique.length ? unique.join(" · ") : fallback;
};
const estimateNote = (value, ready) => value == null ? "Nog geen schatting" : ready ? "Voldoende modeldata" : "Voorlopige schatting";
const statusIsStale = (status) => !Number.isFinite(status.tickEpoch) || status.tickEpoch <= 0
  || Math.floor(Date.now() / 1000) - status.tickEpoch > 30;
const SOURCE_LABELS = { room: "Kamertemperatuur", setpoint: "Kamer setpoint", outside: "Buitentemperatuur", flow: "Flow" };
const ROUTE_LABELS = {
  selected_room_temperature: "geselecteerde kamerbron",
  selected_room_setpoint: "geselecteerde setpointbron",
  selected_outside_temperature: "geselecteerde buitenbron",
  selected_flow: "geselecteerde flowbron",
  "HP1/HP2 composition": "HP1 en HP2",
  "Synthesized zero": "0 bij stilstand",
  unknown: "onbekende route",
};

function duration(seconds) {
  if (!Number.isFinite(seconds) || seconds < 0) return null;
  const rounded = Math.floor(seconds);
  const minutes = Math.floor(rounded / 60);
  const remainder = String(rounded % 60).padStart(2, "0");
  return minutes >= 60 ? `${Math.floor(minutes / 60)}:${String(minutes % 60).padStart(2, "0")}:${remainder}` : `${minutes}:${remainder}`;
}

function collectionCard(label, phase, status, waitingNote = "") {
  if (!status.enabled) return [label, "Gepauzeerd", "Passief leren staat uit.", true, ""];
  if (statusIsStale(status)) return [label, "Status verouderd", "Wacht op een actuele status van de regelaar.", true, "orange"];
  if (!phase) return [label, "Onbekend", "Deze firmware geeft geen voortgang door.", true, "sky"];
  const elapsed = duration(phase.elapsedSeconds);
  const target = duration(phase.targetSeconds);
  const progress = elapsed && target ? `${elapsed} / ${target}` : elapsed || target || "Tijd onbekend";
  const intervals = Number.isFinite(phase.intervals) ? ` · ${phase.intervals} meetperioden afgerond` : "";
  if (phase.active) return [label, "Verzamelt", `${progress}${intervals}`, true, "green"];
  return [label, "Wacht", waitingNote || (elapsed && target ? `${progress}${intervals} · wacht op geldige meting` : "Wacht op een geldige meting."), true, "orange"];
}

function sourceRow(key, source, status) {
  const route = ROUTE_LABELS[source.route] || source.route;
  if (!status.enabled) return renderSettingsSystemRow({ label: SOURCE_LABELS[key], value: "Niet beoordeeld", note: "Passief leren staat uit." });
  if (statusIsStale(status)) return renderSettingsSystemRow({ label: SOURCE_LABELS[key], value: "Status verouderd", note: "Wacht op een actuele status van de regelaar." });
  return renderSettingsSystemRow({ label: SOURCE_LABELS[key], value: source.valid ? "Geldig" : "Ongeldig", note: `Via ${route}` });
}

function renderWaterTemperatureCards() {
  const temperatures = [
    ["HP1 water in", "hp1WaterIn"],
    ["HP1 water uit", "hp1WaterOut"],
    ["HP2 water in", "hp2WaterIn"],
    ["HP2 water uit", "hp2WaterOut"],
  ].filter(([, key]) => hasEntity(key));
  if (!temperatures.length) return "";
  return `<div class="oq-settings-grid oq-house-learning-water">${temperatures.map(([label, key]) => {
    const value = getEntityNumericValue(key);
    return renderStatCard({ label, value: Number.isNaN(value) ? getEntityStateText(key) : formatNumericState(value, 1, "°C") });
  }).join("")}</div>`;
}

function activity(status) {
  if (status.blockedReasons.includes("runtime_blocked") && !statusIsStale(status)) return ["Herstart nodig", "Herstart de regelaar en schakel passief leren opnieuw in. Opgeslagen leerdata blijft behouden.", "orange"];
  if (!status.enabled) return ["Gepauzeerd", "Schakel passief leren in om metingen te verzamelen.", ""];
  if (statusIsStale(status)) return ["Status verouderd", "Wacht op een actuele status van de regelaar.", "orange"];
  if (status.sourceStatus === "series_junction_mismatch") return ["Watertemperaturen sluiten nog niet op elkaar aan.", "Controleer HP1 water uit en HP2 water in.", "orange"];
  if (status.status === "collecting") {
    if (status.controlMode === 0 || status.controlMode === 1) return ["Verzamelt tijdens verwarmingspauze", "Deze pauze telt mee in de meting van warmteverlies en afkoeling.", "green"];
    return ["Verzamelt nu", "Een geldige meting wordt verwerkt.", "green"];
  }
  const missing = Object.entries(status.sources).filter(([, source]) => !source.valid).map(([key]) => SOURCE_LABELS[key].toLowerCase());
  if (missing.length) return ["Wacht op bron", `Nog geen geldige waarde voor ${missing.join(" en ")}.`, "orange"];
  const waitingNotes = {
    defrost_or_oil_return: "Verzamelen hervat na ontdooien of olieretour.",
    service_or_ota: "Verzamelen hervat na service of de firmware-update.",
    cooling: "Tijdens koelen worden geen verwarmingsmetingen verzameld.",
    boiler_heat: "Verzamelen wacht tot alleen de warmtepomp verwarmt.",
    external_heat_not_excluded: "De warmtemeting is nog niet geschikt om mee te leren.",
    active_limit: "Verzamelen wacht tot de actieve begrenzing voorbij is.",
    persistence_unavailable: "De opslag voor leergegevens is niet beschikbaar.",
  };
  const reason = [...status.invalidReasons, ...status.blockedReasons].find((key) => waitingNotes[key]);
  return ["Wacht op meting", waitingNotes[reason] || "Een geldige verwarmingsmeting is nog niet beschikbaar.", "orange"];
}

function modelStatus(status) {
  if (!status.storageReady) return "Opslag niet gereed";
  if (!status.enabled) return "Nog niet beoordeeld";
  if (statusIsStale(status)) return "Status verouderd";
  if (status.adviceReady) return "Warmteverlies komt overeen";
  return status.rlsSamples ? "Voorlopige schatting" : "Nog geen 1R1C-data";
}

function modelStatusNote(status) {
  if (status.enabled && statusIsStale(status)) return "Geen actuele, gevalideerde leerkwaliteit";
  const modelReasons = status.blockedReasons.filter((reason) => MODEL_REASONS.includes(reason));
  if (modelReasons.length) return reasons(modelReasons, "Modelvalidatie geblokkeerd");
  if (status.adviceReady) return "Beide modellen schatten een vergelijkbaar warmteverlies. Warmteopslag (C) is nog niet gevalideerd; de regeling verandert niet.";
  if (status.rlsReady && status.batchAdviceReady) return "Nog niet gezamenlijk gevalideerd";
  if (status.rlsSamples) {
    return `${status.rlsSamples} geaccepteerde periode${status.rlsSamples === 1 ? "" : "n"} van 30 minuten. Meer data en variatie nodig voor validatie.`;
  }
  return status.enabled ? "Wacht op de eerste geldige periode van 30 minuten." : "Start passief leren om te beoordelen";
}

export function renderHouseLearningStatusMarkup(status = state.houseLearningStatus) {
  if (state.houseLearningReset === "pending") {
    return '<p class="oq-settings-action-note" role="status">Leerdata wordt gewist en gecontroleerd…</p>';
  }
  if (state.houseLearningReset === "error" || state.houseLearningReset === "uncertain") {
    return `<p class="oq-settings-action-note oq-settings-action-note--error" role="alert">${escapeHtml(state.houseLearningResetError)}</p>`;
  }
  if (!status) return state.houseLearningEndpointChecking && !state.houseLearningEndpointChecked
    ? '<p class="oq-settings-action-note" role="status">Beschikbaarheid van passief leren wordt gecontroleerd…</p>'
    : state.houseLearningStatusError
    ? `<p class="oq-settings-action-note oq-settings-action-note--error">${escapeHtml(state.houseLearningStatusError)}</p>`
    : "";
  const [activityValue, activityNote, activityTone] = activity(status);
  const invalidSources = Object.entries(status.sources).filter(([, source]) => !source.valid).map(([key]) => SOURCE_LABELS[key].toLowerCase());
  const batchWaiting = invalidSources.length ? `Wacht op ${invalidSources.join(" en ")}.`
    : status.invalidReasons.includes("setpoint_recovery") ? "Wacht tot de kamer stabiel is na de setpointwijziging."
    : "Wacht op een stabiele, geldige verwarmingsmeting.";
  const summaryCards = [
    ["Leren", activityValue, activityNote, true, activityTone],
    collectionCard("Woninglijn (stabiele verwarming)", status.collection?.batch, status, batchWaiting),
    collectionCard("Opwarmen en afkoelen (1R1C)", status.collection?.thermal, status, activityNote),
  ];
  const modelCards = [
    ["Modelstatus", modelStatus(status), modelStatusNote(status), true, status.adviceReady && !statusIsStale(status) ? "green" : "sky"],
    ["1R1C-perioden", String(status.rlsSamples), "Geaccepteerde perioden van 30 minuten"],
    ["Warmteverlies (U)", metric(status.uRls, "W/K"), estimateNote(status.uRls, status.rlsReady)],
    ["Warmteopslag (C)", metric(status.cRlsWhPerK, "Wh/K"), status.cRlsWhPerK == null ? "Nog geen schatting" : "Voorlopig; nog niet gevalideerd"],
  ];
  const batchDiagnosticCards = [
    ["Stabiele batch-perioden", String(status.records), "Langdurige perioden voor vergelijking van het warmteverlies"],
    ["Batch warmteverlies (H)", metric(status.hBatch, "W/K"), "Schatting uit stabiele perioden"],
    ["Batch starttemperatuur (T₀)", metric(status.t0Batch, "°C"), "Geschatte buitentemperatuur waarbij verwarming begint"],
  ];
  return `<div class="oq-settings-grid oq-house-learning-summary">${summaryCards.map(([label, value, note, cardStatus, tone]) => renderStatCard({ label, value, note, status: cardStatus, tone })).join("")}</div><div class="oq-settings-system-summary oq-house-learning-sources">${Object.entries(status.sources).map(([key, source]) => sourceRow(key, source, status)).join("")}</div>${renderWaterTemperatureCards()}<div class="oq-settings-grid oq-house-learning-model">${modelCards.map(([label, value, note, cardStatus, tone]) => renderStatCard({ label, value, note, status: cardStatus, tone })).join("")}</div>${renderSettingsAdvancedDisclosure("house-learning-model", "Modeldiagnostiek", "De voorlopige 1R1C-schatting staat hierboven. Deze extra batchcontrole gebruikt alleen langdurige stabiele perioden en verandert de regeling niet.", `<div class="oq-settings-grid">${batchDiagnosticCards.map(([label, value, note, cardStatus, tone]) => renderStatCard({ label, value, note, status: cardStatus, tone })).join("")}</div>`)}${state.houseLearningStatusError ? `<p class="oq-settings-action-note oq-settings-action-note--error">${escapeHtml(state.houseLearningStatusError)}</p>` : ""}`;
}

function houseLearningChartSignature(busy = Boolean(state.busyAction) || state.houseLearningReset === "pending") {
  return JSON.stringify([busy, state.houseLearningChartFetchedAt, state.houseLearningChart?.length,
    state.houseLearningChartLoading, state.houseLearningChartError,
    state.houseLearningStatus?.hBatch, state.houseLearningStatus?.t0Batch, state.houseLearningStatus?.batchAdviceReady,
    ...["houseColdTemp", "houseOutdoorMax", "housePower"].map(getEntityNumericValue)]);
}

function renderHouseLearningChartPanel(busy = Boolean(state.busyAction) || state.houseLearningReset === "pending") {
  const chart = state.houseLearningChart === null ? "" : renderHouseLearningChart(
    state.houseLearningChart,
    { coldC: getEntityNumericValue("houseColdTemp"), zeroC: getEntityNumericValue("houseOutdoorMax"), ratedW: getEntityNumericValue("housePower") },
    { h: state.houseLearningStatus?.hBatch, t0: state.houseLearningStatus?.t0Batch, ready: state.houseLearningStatus?.batchAdviceReady },
  );
  return `
        <div class="oq-house-learning-chart-head"><div><h5>Woninglijn en meetresultaten</h5><p>Vergelijk de ingestelde woninglijn met de metingen. Tik op een meetpunt voor datum, meetduur en waarden.</p></div><button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="load-house-learning-chart" ${state.houseLearningChartLoading || busy ? "disabled" : ""}>${state.houseLearningChartLoading ? "Meetgegevens laden…" : "Meetgegevens tonen"}</button></div>
        ${state.houseLearningChartError ? `<p class="oq-settings-action-note oq-settings-action-note--error">${escapeHtml(state.houseLearningChartError)}</p>` : ""}
        ${chart}${state.houseLearningChartFetchedAt ? `<p class="oq-house-learning-chart-freshness">Geladen ${escapeHtml(new Date(state.houseLearningChartFetchedAt).toLocaleString("nl-NL", { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" }))}</p>` : ""}
`;
}

export function renderHouseLearningSettings() {
  const initialCapabilityCheck = state.houseLearningEndpointChecking && !state.houseLearningEndpointChecked;
  if (!hasEntity("houseLearningEnabled") || (!state.houseLearningEndpointAvailable && !initialCapabilityCheck)) return "";
  const resetting = state.busyAction === "houseLearningReset" || state.houseLearningReset === "pending";
  const busy = Boolean(state.busyAction) || resetting;
  const exporting = state.busyAction === "houseLearningExport";
  const reset = hasEntity("houseLearningReset")
    ? renderNamedActionButton("houseLearningReset", resetting ? "Leerdata wissen…" : "Leerdata wissen", "oq-helper-button oq-helper-button--ghost", busy)
    : "";
  return `
    <div class="oq-settings-subpanel oq-settings-subpanel--nested" data-oq-house-learning>
      <div class="oq-settings-subpanel-head">
        <p class="oq-helper-label">Passief leren</p><h4>Huismodel volgen</h4>
        <p>Volgt hoeveel warmte je woning nodig heeft en hoe snel deze opwarmt en afkoelt. Geen automatische wijzigingen.</p>
      </div>
      <div class="oq-settings-grid">
        ${renderSettingsSwitchField("houseLearningEnabled", "Passief leren", "Pauzeer of hervat. Na herstart staat dit uit.", "Leren ingeschakeld.", "Geen nieuwe metingen.")}
      </div>
      <div data-oq-house-learning-status>${renderHouseLearningStatusMarkup()}</div>
      <div class="oq-house-learning-chart-panel" data-oq-house-learning-chart-panel data-oq-chart-signature="${escapeHtml(houseLearningChartSignature(busy))}">${renderHouseLearningChartPanel(busy)}</div>
      <div class="oq-helper-actions">
        <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="download-house-learning" ${busy ? "disabled" : ""}>${exporting ? "Leerdata downloaden…" : "Diagnostische leerdata downloaden"}</button>${reset}
      </div>
    </div>`;
}

export function patchHouseLearningSettingsStatus() {
  const panel = state.root?.querySelector("[data-oq-house-learning]");
  const initialCapabilityCheck = state.houseLearningEndpointChecking && !state.houseLearningEndpointChecked;
  if (panel && (!hasEntity("houseLearningEnabled") || (!state.houseLearningEndpointAvailable && !initialCapabilityCheck))) {
    panel.remove?.();
    return true;
  }
  const node = panel?.querySelector("[data-oq-house-learning-status]");
  if (!node) return false;
  const chartNode = panel?.querySelector("[data-oq-house-learning-chart-panel]");
  if (chartNode) {
    const signature = houseLearningChartSignature();
    if (chartNode.getAttribute("data-oq-chart-signature") !== signature) {
      chartNode.innerHTML = renderHouseLearningChartPanel();
      chartNode.setAttribute("data-oq-chart-signature", signature);
    }
  }
  const markup = renderHouseLearningStatusMarkup();
  if (node.innerHTML !== markup) node.innerHTML = markup;
  return true;
}
