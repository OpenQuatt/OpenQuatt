import { hasEntity } from "../core/app-shared.js";
import { getEntityValue } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { renderStatCard } from "../views/stat-card.js";
import { renderNamedActionButton, renderSettingsSwitchField } from "./controls.js";

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
const estimateNote = (value, ready) => value == null ? "Nog geen schatting" : ready ? "Gereed" : "Voorlopige schatting";
const statusIsStale = (status) => !Number.isFinite(status.tickEpoch) || status.tickEpoch <= 0
  || Math.floor(Date.now() / 1000) - status.tickEpoch > 30;

function quality(status) {
  if (!status.storageReady) return "Opslag niet gereed";
  if (!status.enabled) return "Nog niet beoordeeld";
  if (statusIsStale(status)) return "Status verouderd";
  if (status.invalidReasons.length || status.blockedReasons.length) return "Geblokkeerd";
  if (status.adviceReady) return "Voldoende data";
  return status.records ? "Aan het leren" : "Nog geen metingen";
}

function qualityNote(status) {
  if (status.enabled && statusIsStale(status)) return "Geen actuele, gevalideerde leerkwaliteit";
  const modelReasons = status.blockedReasons.filter((reason) => MODEL_REASONS.includes(reason));
  if (modelReasons.length) return reasons(modelReasons, "Modelvalidatie geblokkeerd");
  if (status.rlsReady && status.batchAdviceReady && !status.adviceReady) {
    return "Nog niet gezamenlijk gevalideerd";
  }
  return reasons(status.rlsReadinessReasons, status.enabled ? "Nog geen kwaliteitsreden" : "Start passief leren om te beoordelen");
}

function sourceNote(status) {
  const labels = { room: "kamer", setpoint: "setpoint", outside: "buiten", flow: "flow" };
  const routes = Object.entries(labels).map(([key, label]) => {
    const source = status.sources[key];
    return `${label}: ${source.route}${source.valid ? "" : " (ongeldig)"}`;
  });
  const sourceReasons = [...status.invalidReasons, ...status.blockedReasons.filter((reason) => !MODEL_REASONS.includes(reason))];
  return `${reasons(sourceReasons, "Bronnen bruikbaar")} · ${routes.join(" · ")}`;
}

function installationNote() {
  const topology = String(getEntityValue("installationTopology") || "").toLowerCase();
  if (topology === "duo") return "Water · Duo: HP1 → HP2 in serie.";
  if (topology === "single") return "Water · Single: één warmtepomp.";
  return "Water · Single/Duo volgt uit de geïnstalleerde firmware.";
}


export function renderHouseLearningStatusMarkup(status = state.houseLearningStatus) {
  if (state.houseLearningReset === "pending") {
    return '<p class="oq-settings-action-note" role="status">Leerdata wordt gewist en gecontroleerd…</p>';
  }
  if (state.houseLearningReset === "error" || state.houseLearningReset === "uncertain") {
    return `<p class="oq-settings-action-note oq-settings-action-note--error" role="alert">${escapeHtml(state.houseLearningResetError)}</p>`;
  }
  if (!status) return state.houseLearningStatusError
    ? `<p class="oq-settings-action-note oq-settings-action-note--error">${escapeHtml(state.houseLearningStatusError)}</p>`
    : "";
  const blocked = status.invalidReasons.length > 0 || status.blockedReasons.length > 0;
  const cards = [
    ["Status", status.enabled ? "Actief" : "Gepauzeerd", "Alleen diagnose; geen automatische toepassing", true, status.enabled ? "green" : ""],
    ["Bron", blocked ? "Geblokkeerd" : status.sourceStatus === "ok" ? "Bruikbaar" : "Niet bruikbaar", sourceNote(status), true, blocked ? "orange" : "green"],
    ["Leerkwaliteit", quality(status), qualityNote(status), true, status.adviceReady && !statusIsStale(status) ? "green" : "sky"],
    ["Metingen", String(status.records), `${status.rlsSamples} RLS-samples`],
    ["Batch H", metric(status.hBatch, "W/K"), "Batchschatting"],
    ["Batch T₀", metric(status.t0Batch, "°C"), "Batchschatting"],
    ["RLS U", metric(status.uRls, "W/K"), estimateNote(status.uRls, status.rlsReady)],
    ["RLS C", metric(status.cRlsWhPerK, "Wh/K"), estimateNote(status.cRlsWhPerK, status.rlsReady)],
  ];
  return `<div class="oq-settings-grid">${cards.map(([label, value, note, cardStatus, tone]) => renderStatCard({ label, value, note, status: cardStatus, tone })).join("")}</div>${state.houseLearningStatusError ? `<p class="oq-settings-action-note oq-settings-action-note--error">${escapeHtml(state.houseLearningStatusError)}</p>` : ""}`;
}

export function renderHouseLearningSettings() {
  if (!hasEntity("houseLearningEnabled") || !state.houseLearningEndpointAvailable) return "";
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
        <p>Diagnostische schatting van H, T₀, U en C. Geen automatische wijzigingen.</p>
        <p>${installationNote()}</p>
      </div>
      <div class="oq-settings-grid">
        ${renderSettingsSwitchField("houseLearningEnabled", "Passief leren", "Pauzeer of hervat. Na herstart staat dit uit.", "Verzamelt metingen.", "Geen nieuwe metingen.")}
      </div>
      <div data-oq-house-learning-status>${renderHouseLearningStatusMarkup()}</div>
      <div class="oq-helper-actions">
        <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="download-house-learning" ${busy ? "disabled" : ""}>${exporting ? "Leerdata downloaden…" : "Diagnostische leerdata downloaden"}</button>${reset}
      </div>
    </div>`;
}

export function patchHouseLearningSettingsStatus() {
  const panel = state.root?.querySelector("[data-oq-house-learning]");
  if (panel && (!hasEntity("houseLearningEnabled") || !state.houseLearningEndpointAvailable)) {
    panel.remove?.();
    return true;
  }
  const node = panel?.querySelector("[data-oq-house-learning-status]");
  if (!node) return false;
  const markup = renderHouseLearningStatusMarkup();
  if (node.innerHTML !== markup) node.innerHTML = markup;
  return true;
}
