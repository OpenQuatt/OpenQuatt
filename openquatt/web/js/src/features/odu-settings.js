import { hasEntity } from "../core/app-shared.js";
import { invokeActionMap } from "../core/action-router.js";
import { fetchWithTimeout } from "../core/browser-utils.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { getBasePath } from "../core/url-path.js";
import { getInstallationTopology } from "./device-context.js";

const MODE_OPTIONS = [
  [1, "Volgt buitentemperatuur"],
  [2, "Tijdens ontdooien"],
  [3, "Onbekend (standaard V1.5 en V2)"],
];

function modeDescription(mode) {
  if (Number(mode) === 1) {
    return "De verwarming schakelt op basis van de ingestelde buitentemperatuurgrenzen.";
  }
  if (Number(mode) === 2) {
    return "De verwarming schakelt in zodra een ontdooicyclus start en twee minuten nadat deze is afgelopen weer uit.";
  }
  if (Number(mode) === 3) {
    return "De werking is niet officieel gedocumenteerd. In de praktijk lijkt de verwarming onder 0 °C aan te gaan, boven 0 °C uit te gaan en tijdens ontdooien actief te zijn. Dit is de standaardinstelling voor Quatt buitenunit V1.5 en V2.";
  }
  return "De werking van deze waarde is niet bekend.";
}

function settingsSummary(settings) {
  const mode = Number(settings?.mode);
  if (mode !== 1) return `modus ${Number.isInteger(mode) ? mode : "—"}`;
  return `modus 1, ${settings?.startTemperatureC ?? "—"} °C inschakelen en ${settings?.stopDeltaC ?? "—"} °C verschil`;
}

export function getOduSettingsEndpoint(hp, action) {
  return `${getBasePath()}/openquatt/odu-settings/hp${Number(hp) === 2 ? 2 : 1}/${action}`;
}

export function getOduSettingsHpIndexes() {
  return getInstallationTopology() === "duo" || hasEntity("hp2ExcludeMinHz") ? [1, 2] : [1];
}

function normalizeSettings(values = {}) {
  const mode = Number(values.mode);
  const startTemperatureC = Number(values.start_temperature_c);
  const stopDeltaC = Number(values.stop_delta_c);
  return {
    mode: Number.isInteger(mode) ? mode : null,
    startTemperatureC: Number.isInteger(startTemperatureC) ? startTemperatureC : null,
    stopDeltaC: Number.isInteger(stopDeltaC) ? stopDeltaC : null,
  };
}

export function normalizeOduSettingsStatus(payload = {}, hp = 1) {
  return {
    available: payload.available !== false,
    unsupported: payload.unsupported === true,
    hp: Number(payload.hp || hp) === 2 ? 2 : 1,
    busy: payload.busy === true,
    loaded: payload.loaded === true,
    profileAvailable: payload.profile_available === true,
    autoReapply: payload.auto_reapply === true,
    identityReady: payload.identity_ready === true,
    identityMatches: payload.identity_matches === true,
    writeUncertain: payload.write_uncertain === true,
    variant: Number(payload.variant) || 0,
    controlBoardItem: Number(payload.control_board_item) || 0,
    status: String(payload.status || "READY"),
    csrfToken: String(payload.csrf_token || ""),
    actual: normalizeSettings(payload.actual),
    desired: normalizeSettings(payload.desired),
    defaults: normalizeSettings(payload.defaults),
  };
}

export function getOduSettingsStatus(hp) {
  return state.oduSettingsStatuses?.[Number(hp) === 2 ? 2 : 1] || null;
}

function getDraft(hp) {
  const hpIndex = Number(hp) === 2 ? 2 : 1;
  if (state.oduSettingsDrafts?.[hpIndex]) return state.oduSettingsDrafts[hpIndex];
  const status = getOduSettingsStatus(hpIndex);
  const source = status?.profileAvailable && status.identityMatches
    ? status.desired
    : status?.loaded
      ? status.actual
      : status?.defaults;
  return {
    mode: String(source?.mode ?? ""),
    startTemperatureC: String(source?.startTemperatureC ?? ""),
    stopDeltaC: String(source?.stopDeltaC ?? ""),
    autoReapply: status?.autoReapply === true,
    dirty: false,
  };
}

function hydrateDraft(status, force = false) {
  const current = state.oduSettingsDrafts?.[status.hp];
  if (current?.dirty && !force) return;
  const source = status.profileAvailable && status.identityMatches
    ? status.desired
    : status.loaded
      ? status.actual
      : status.defaults;
  state.oduSettingsDrafts = {
    ...(state.oduSettingsDrafts || {}),
    [status.hp]: {
      mode: String(source.mode ?? ""),
      startTemperatureC: String(source.startTemperatureC ?? ""),
      stopDeltaC: String(source.stopDeltaC ?? ""),
      autoReapply: status.autoReapply,
      dirty: false,
    },
  };
}

async function fetchStatus(hp) {
  const response = await fetchWithTimeout(
    getOduSettingsEndpoint(hp, "status"),
    { cache: "no-store", headers: { "Cache-Control": "no-store" } },
    8000,
    `HP${hp} status reageert niet`,
  );
  if (response.status === 404) {
    return normalizeOduSettingsStatus({ available: false, unsupported: true, hp }, hp);
  }
  if (!response.ok) throw new Error(`HP${hp} status HTTP ${response.status}`);
  return normalizeOduSettingsStatus(await response.json(), hp);
}

function storeStatus(status, forceDraft = false) {
  state.oduSettingsStatuses = { ...(state.oduSettingsStatuses || {}), [status.hp]: status };
  hydrateDraft(status, forceDraft);
}

function statusFetchErrorMessage(error) {
  const detail = String(error?.message || error || "").trim();
  if (!detail || /failed to fetch|networkerror|load failed/i.test(detail)) {
    return "Status ophalen mislukt. Controleer de verbinding met OpenQuatt.";
  }
  return `Status ophalen mislukt. ${detail}`;
}

export function shouldRefreshOduSettingsSurface() {
  return state.systemModal === "odu-bottom-plate-settings";
}

export async function refreshOduSettingsStatuses(options = {}) {
  if (!shouldRefreshOduSettingsSurface() && options.force !== true) return false;
  if (state.oduSettingsFetchPromise) return state.oduSettingsFetchPromise;
  const now = Date.now();
  if (!options.force && now - Number(state.oduSettingsLastFetchAt || 0) < 5000) return false;
  const previous = JSON.stringify(state.oduSettingsStatuses || {});
  const hadError = Boolean(state.oduSettingsError);
  state.oduSettingsFetchPromise = (async () => {
    try {
      const statuses = await Promise.all(getOduSettingsHpIndexes().map(fetchStatus));
      statuses.forEach((status) => storeStatus(status));
      state.oduSettingsLastFetchAt = Date.now();
      state.oduSettingsError = "";
      const changed = JSON.stringify(state.oduSettingsStatuses || {}) !== previous;
      if ((changed || hadError) && shouldRefreshOduSettingsSurface()) render();
      return changed || hadError;
    } catch (error) {
      state.oduSettingsError = statusFetchErrorMessage(error);
      if (!options.silent) render();
      return false;
    } finally {
      state.oduSettingsFetchPromise = null;
    }
  })();
  return state.oduSettingsFetchPromise;
}

function errorMessage(error) {
  if (error === "busy") return "de Modbus-bus is nog bezig";
  if (error === "unavailable") return "de buitenunit is niet bereikbaar";
  if (error === "identity_required") return "het type buitenunit is nog niet vastgesteld";
  if (error === "invalid_settings") return "de gekozen waarden zijn ongeldig";
  if (error === "forbidden") return "de beveiligingscontrole is verlopen; laad de pagina opnieuw";
  return error || "actie geweigerd";
}

async function postAction(hp, action, values = {}) {
  let status = getOduSettingsStatus(hp);
  if (!status?.csrfToken) {
    status = await fetchStatus(hp);
    storeStatus(status);
  }
  const body = new URLSearchParams({ csrf_token: status.csrfToken });
  Object.entries(values).forEach(([key, value]) => body.set(key, String(value)));
  const response = await fetchWithTimeout(
    getOduSettingsEndpoint(hp, action),
    {
      method: "POST",
      cache: "no-store",
      headers: { "Cache-Control": "no-store", "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
      body: body.toString(),
    },
    8000,
    `HP${hp} actie reageert niet`,
  );
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(errorMessage(payload.error || `HTTP ${response.status}`));
  const next = normalizeOduSettingsStatus(payload, hp);
  storeStatus(next);
  return next;
}

async function waitForOperation(hp) {
  const deadline = Date.now() + 35000;
  while (Date.now() < deadline) {
    await new Promise((resolve) => window.setTimeout(resolve, 500));
    const status = await fetchStatus(hp);
    storeStatus(status);
    render();
    if (!status.busy) return status;
  }
  throw new Error("de buitenunit bleef langer dan 35 seconden bezig");
}

function parseDraftInteger(value) {
  const raw = String(value ?? "").trim();
  if (!raw) return null;
  const parsed = Number(raw);
  return Number.isInteger(parsed) ? parsed : null;
}

function validDraft(draft) {
  const mode = parseDraftInteger(draft.mode);
  const startTemperatureC = parseDraftInteger(draft.startTemperatureC);
  const stopDeltaC = parseDraftInteger(draft.stopDeltaC);
  return mode !== null && mode >= 1 && mode <= 3
    && startTemperatureC !== null && startTemperatureC >= -30 && startTemperatureC <= 30
    && stopDeltaC !== null && stopDeltaC >= 0 && stopDeltaC <= 30;
}

async function runOperation(hp, action) {
  const draft = getDraft(hp);
  if (action === "save" && !validDraft(draft)) {
    state.oduSettingsError = `HP${hp}: controleer modus, starttemperatuur (-30 tot 30 °C) en temperatuurverschil (0 tot 30 °C).`;
    render();
    return;
  }
  state.busyAction = `odu-settings-hp${hp}-${action}`;
  state.oduSettingsError = "";
  state.controlNotice = "";
  render();
  try {
    let status = await postAction(hp, action, action === "save" ? {
      mode: Number(draft.mode),
      start_temperature_c: Number(draft.startTemperatureC),
      stop_delta_c: Number(draft.stopDeltaC),
      auto_reapply: draft.autoReapply,
    } : {});
    if (status.busy) status = await waitForOperation(hp);
    storeStatus(status, true);
    if (/FAILED|MISMATCH/.test(status.status)) throw new Error(status.status);
    state.controlNotice = status.status === "PENDING_SAFE"
      ? `HP${hp}: opgeslagen; toepassen wacht tot de buitenunit stilstaat.`
      : action === "load"
        ? `HP${hp}: actuele waarden geladen.`
        : `HP${hp}: waarden opgeslagen en gecontroleerd.`;
  } catch (error) {
    state.oduSettingsError = `HP${hp}: actie mislukt. ${error.message || String(error)}`;
  } finally {
    state.busyAction = "";
    state.oduSettingsLastFetchAt = 0;
    render();
  }
}

export function updateOduSettingsDraft(input) {
  const hp = Number(input.dataset.oqOduSettingsHp) === 2 ? 2 : 1;
  const field = String(input.dataset.oqOduSettingsField || "");
  if (!["mode", "startTemperatureC", "stopDeltaC", "autoReapply"].includes(field)) return false;
  const draft = getDraft(hp);
  state.oduSettingsDrafts = {
    ...(state.oduSettingsDrafts || {}),
    [hp]: { ...draft, [field]: field === "autoReapply" ? Boolean(input.checked) : String(input.value), dirty: true },
  };
  const panel = input.closest(".oq-settings-odu-runtime-panel");
  const next = getDraft(hp);
  const start = parseDraftInteger(next.startTemperatureC);
  const delta = parseDraftInteger(next.stopDeltaC);
  const temperatureSettings = panel?.querySelector("[data-oq-odu-temperature-settings]");
  const modeOutput = panel?.querySelector("[data-oq-odu-mode-description]");
  const startOutput = panel?.querySelector("[data-oq-odu-start-temperature]");
  const stop = panel?.querySelector("[data-oq-odu-stop-temperature]");
  const saveButton = panel?.querySelector('[data-oq-action="odu-settings-save"]');
  if (temperatureSettings) temperatureSettings.hidden = Number(next.mode) !== 1;
  if (modeOutput) modeOutput.textContent = modeDescription(next.mode);
  if (startOutput) startOutput.textContent = start !== null ? `${start} °C of kouder` : "—";
  if (stop) stop.textContent = start !== null && delta !== null ? `${start + delta} °C of warmer` : "—";
  if (saveButton) {
    const status = getOduSettingsStatus(hp);
    const busy = status?.busy || String(state.busyAction || "").startsWith(`odu-settings-hp${hp}-`);
    saveButton.disabled = !status?.available || !status.identityReady || status.unsupported || busy || !validDraft(next);
  }
  return true;
}

function statusPresentation(status) {
  if (!status) return state.oduSettingsError
    ? ["Status niet beschikbaar", "warning"]
    : ["Status laden...", ""];
  const code = String(status?.status || "").toUpperCase();
  if (status?.writeUncertain || code === "VERIFY_FAILED") return ["Toepassen kon niet worden bevestigd", "warning"];
  if (code === "IN_SYNC") return ["Jouw waarden zijn actief", "success"];
  if (code === "PENDING_SAFE") return ["Wacht tot de buitenunit stilstaat", "warning"];
  if (code === "APPLYING" || status?.busy) return ["Waarden toepassen", ""];
  if (code === "IDENTITY_MISMATCH") return ["Opgeslagen waarden horen bij een andere buitenunit", "warning"];
  if (code === "PERSIST_FAILED") return ["Opslaan in OpenQuatt is mislukt", "warning"];
  if (code === "LOADED") return [status.autoReapply ? "De buitenunit gebruikt andere waarden" : "Automatisch opnieuw toepassen staat uit", ""];
  if (status?.unsupported) return ["Niet ondersteund door deze firmware", "warning"];
  if (!status?.available) return ["Buitenunit niet bereikbaar", "warning"];
  return ["Actuele waarden nog niet geladen", ""];
}

function variantLabel(variant) {
  if (variant === 1) return "Quatt buitenunit V1";
  if (variant === 2) return "Quatt buitenunit V1.5";
  if (variant === 3) return "Quatt buitenunit V2 oud model";
  if (variant === 4) return "Quatt buitenunit V2 nieuw model";
  return "Onbekend";
}

function renderPanel(hp) {
  const status = getOduSettingsStatus(hp);
  const draft = getDraft(hp);
  const busy = status?.busy || String(state.busyAction || "").startsWith(`odu-settings-hp${hp}-`);
  const enabled = status?.available && status.identityReady && !status.unsupported && !busy;
  const valid = validDraft(draft);
  const startTemperature = parseDraftInteger(draft.startTemperatureC);
  const stopDelta = parseDraftInteger(draft.stopDeltaC);
  const stopTemperature = startTemperature !== null && stopDelta !== null ? startTemperature + stopDelta : null;
  const [statusLabel, tone] = statusPresentation(status);
  return `
    <article class="oq-settings-odu-runtime-panel">
      <div class="oq-settings-odu-runtime-panel-head">
        <div><p class="oq-helper-label">HP${hp}</p><h4>Bodemplaatverwarming</h4><p>${escapeHtml(variantLabel(status?.variant))}</p></div>
        <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="odu-settings-load" data-hp="${hp}" ${!enabled ? "disabled" : ""}>Uit buitenunit laden</button>
      </div>
      <div class="oq-settings-odu-state${tone ? ` is-${tone}` : ""}" role="status"><strong>${escapeHtml(statusLabel)}</strong></div>
      ${status?.loaded || status?.profileAvailable ? `
        <div class="oq-settings-odu-fields">
          <label><span>Regelmethode</span><select class="oq-helper-select" data-oq-odu-settings-hp="${hp}" data-oq-odu-settings-field="mode" ${!enabled ? "disabled" : ""}>
            ${MODE_OPTIONS.some(([value]) => value === Number(draft.mode)) ? "" : '<option value="" selected disabled>Kies een regelmethode</option>'}
            ${MODE_OPTIONS.map(([value, label]) => `<option value="${value}"${Number(draft.mode) === value ? " selected" : ""}>${value} · ${escapeHtml(label)}</option>`).join("")}
          </select></label>
          <p class="oq-settings-odu-mode-description" data-oq-odu-mode-description aria-live="polite">${escapeHtml(modeDescription(draft.mode))}</p>
          <div class="oq-settings-odu-temperature-settings" data-oq-odu-temperature-settings${Number(draft.mode) === 1 ? "" : " hidden"}>
            <label><span>Temperatuurgrens voor inschakelen</span><span class="oq-helper-control oq-helper-control--suffix"><input class="oq-helper-input" type="number" min="-30" max="30" step="1" value="${escapeHtml(draft.startTemperatureC)}" data-oq-odu-settings-hp="${hp}" data-oq-odu-settings-field="startTemperatureC" ${!enabled ? "disabled" : ""}><span class="oq-helper-unit-chip">°C</span></span></label>
            <label><span>Uitschakelen nadat de buitentemperatuur is gestegen met</span><span class="oq-helper-control oq-helper-control--suffix"><input class="oq-helper-input" type="number" min="0" max="30" step="1" value="${escapeHtml(draft.stopDeltaC)}" data-oq-odu-settings-hp="${hp}" data-oq-odu-settings-field="stopDeltaC" ${!enabled ? "disabled" : ""}><span class="oq-helper-unit-chip">°C</span></span></label>
            <div class="oq-settings-odu-thresholds"><span>Verwarming aan vanaf <strong data-oq-odu-start-temperature>${escapeHtml(draft.startTemperatureC || "—")} °C of kouder</strong></span><span>Verwarming weer uit bij <strong data-oq-odu-stop-temperature>${stopTemperature !== null ? `${stopTemperature} °C of warmer` : "—"}</strong></span></div>
          </div>
        </div>
        <label class="oq-settings-odu-auto"><input type="checkbox" data-oq-odu-settings-hp="${hp}" data-oq-odu-settings-field="autoReapply" ${draft.autoReapply ? "checked" : ""} ${!enabled ? "disabled" : ""}><span><strong>Na herstart automatisch opnieuw toepassen</strong><small>OpenQuatt bewaart deze waarden en past ze na een herstart opnieuw toe, ook als de compressor draait.</small></span></label>
        <p class="oq-settings-odu-runtime-validation">Standaard voor ${escapeHtml(variantLabel(status?.variant))}: ${escapeHtml(settingsSummary(status?.defaults))}. Huidige buitenunit: ${escapeHtml(settingsSummary(status?.actual))}.</p>
        <div class="oq-helper-modal-actions"><button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="odu-settings-save" data-hp="${hp}" ${!enabled || !valid ? "disabled" : ""}>${busy ? "Bezig..." : "Opslaan en toepassen"}</button></div>
      ` : '<p class="oq-settings-odu-runtime-validation">Laad de actuele waarden uit de buitenunit voordat je iets wijzigt.</p>'}
    </article>`;
}

export function renderOduSettingsModal() {
  return renderModalShell({
    modalId: "odu-bottom-plate-settings",
    titleId: "oq-odu-settings-title",
    kicker: "Instellingen buitenunit",
    title: "Bodemplaatverwarming",
    titleBadge: "Experimenteel",
    closeAction: "close-system-modal",
    closeLabel: "Sluit bodemplaatinstellingen",
    modalClass: "oq-helper-modal--wide oq-settings-odu-modal",
    bodyMarkup: `
      <div class="oq-settings-odu-modal-body" data-oq-modal-scroll="body">
        <div class="oq-settings-odu-runtime-warning"><strong>Niet permanent opgeslagen in de buitenunit</strong><p>Na een herstart of stroomonderbreking gebruikt de buitenunit weer haar eigen opgeslagen waarden. OpenQuatt kan jouw keuze daarna veilig opnieuw toepassen.</p><p>Je kunt deze instellingen ook aanpassen terwijl de compressor draait.</p></div>
        ${state.oduSettingsError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.oduSettingsError)}</p>` : ""}
        ${state.controlNotice && String(state.controlNotice).startsWith("HP") ? `<p class="oq-helper-notice" role="status">${escapeHtml(state.controlNotice)}</p>` : ""}
        <div class="oq-settings-odu-runtime-panels">${getOduSettingsHpIndexes().map(renderPanel).join("")}</div>
      </div>`,
  });
}

const actionHandlers = {
  "odu-settings-load": (button) => runOperation(Number(button.dataset.hp) === 2 ? 2 : 1, "load"),
  "odu-settings-save": (button) => runOperation(Number(button.dataset.hp) === 2 ? 2 : 1, "save"),
};

export function handleOduSettingsAction(action, button) {
  return invokeActionMap(actionHandlers, action, button);
}

export async function getOduSettingsBackupProfiles() {
  const profiles = {};
  const statuses = await Promise.all(getOduSettingsHpIndexes().map(fetchStatus));
  statuses.forEach((status) => {
    if (!status.profileAvailable || !status.identityMatches) return;
    profiles[`hp${status.hp}`] = {
      variant: status.variant,
      control_board_item: status.controlBoardItem,
      mode: status.desired.mode,
      start_temperature_c: status.desired.startTemperatureC,
      stop_delta_c: status.desired.stopDeltaC,
      auto_reapply: status.autoReapply,
    };
  });
  return profiles;
}

export async function restoreOduSettingsBackupProfiles(profiles = {}) {
  const results = [];
  const availableHp = new Set(getOduSettingsHpIndexes());
  for (const [key, profile] of Object.entries(profiles)) {
    const hp = key === "hp2" ? 2 : key === "hp1" ? 1 : 0;
    if (!hp || !availableHp.has(hp)) {
      results.push({ key, applied: false, reason: "Buitenunit is niet aanwezig op deze installatie." });
      continue;
    }
    try {
      const current = await fetchStatus(hp);
      storeStatus(current);
      if (!current.identityReady || current.variant !== profile.variant
          || current.controlBoardItem !== profile.control_board_item) {
        results.push({ key, applied: false, reason: "Het opgeslagen profiel hoort bij een andere buitenunit." });
        continue;
      }
      let status = await postAction(hp, "save", {
        mode: profile.mode,
        start_temperature_c: profile.start_temperature_c,
        stop_delta_c: profile.stop_delta_c,
        auto_reapply: profile.auto_reapply,
      });
      if (status.busy) status = await waitForOperation(hp);
      if (/FAILED|MISMATCH/.test(status.status)) {
        results.push({ key, applied: false, reason: `Firmwarestatus: ${status.status}` });
      } else {
        results.push({ key, applied: true, pending: status.status === "PENDING_SAFE" });
      }
    } catch (error) {
      results.push({ key, applied: false, reason: `Resultaat niet bevestigd: ${error.message || String(error)}` });
    }
  }
  return results;
}
