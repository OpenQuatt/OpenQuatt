import { hasEntity } from "../core/app-shared.js";
import { invokeActionMap } from "../core/action-router.js";
import { fetchWithTimeout } from "../core/browser-utils.js";
import { getEntityValue, parseLooseNumber } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";
import { render } from "../core/render-scheduler.js";
import { state } from "../core/state.js";
import { getBasePath } from "../core/url-path.js";
import { getInstallationTopology } from "./device-context.js";

export const ODU_RUNTIME_FREQUENCY_LEVELS = Array.from({ length: 21 }, (_item, index) => index);
export const ODU_RUNTIME_FREQUENCY_MODES = ["cooling", "heating"];

export function getOduRuntimeFrequencyEndpoint(hp, action) {
  const hpIndex = Number(hp) === 2 ? 2 : 1;
  return `${getBasePath()}/openquatt/odu-runtime/hp${hpIndex}/${action}`;
}

export function getOduRuntimeFrequencyHpIndexes() {
  return getInstallationTopology() === "duo" || hasEntity("hp2ExcludeMinHz") ? [1, 2] : [1];
}

export function normalizeOduRuntimeFrequencyStatus(payload = {}, hp = 1) {
  const levelCount = Number(payload.level_count) === 21 ? 21 : 11;
  const normalizeValues = (values) => Array.from({ length: levelCount }, (_item, index) => {
    const value = Number(values?.[index]);
    return Number.isInteger(value) && value >= 0 && value <= 120 ? value : null;
  });
  return {
    available: payload.available !== false,
    unsupported: payload.unsupported === true,
    hp: Number(payload.hp || hp) === 2 ? 2 : 1,
    busy: payload.busy === true,
    loaded: payload.loaded === true,
    armed: payload.armed === true,
    extendedLayout: payload.extended_layout === true,
    levelCount,
    status: String(payload.status || "READY: load ODU runtime table"),
    csrfToken: String(payload.csrf_token || ""),
    cooling: normalizeValues(payload.cooling),
    heating: normalizeValues(payload.heating),
  };
}

function getDraftKey(hp, mode, level) {
  return `${Number(hp) === 2 ? 2 : 1}:${mode === "cooling" ? "cooling" : "heating"}:${level}`;
}

export function getOduRuntimeFrequencyStatus(hp) {
  return state.oduRuntimeFrequencyStatuses?.[Number(hp) === 2 ? 2 : 1] || null;
}

function storeStatus(hp, status) {
  state.oduRuntimeFrequencyStatuses = { ...(state.oduRuntimeFrequencyStatuses || {}), [hp]: status };
}

export function getOduRuntimeFrequencyDraftValue(hp, mode, level) {
  const key = getDraftKey(hp, mode, level);
  if (Object.prototype.hasOwnProperty.call(state.oduRuntimeFrequencyDrafts || {}, key)) {
    return state.oduRuntimeFrequencyDrafts[key];
  }
  const value = getOduRuntimeFrequencyStatus(hp)?.[mode]?.[level];
  return Number.isFinite(value) ? String(value) : "";
}

export function updateOduRuntimeFrequencyDraft(input) {
  const hp = Number(input.dataset.oqOduRuntimeHp) === 2 ? 2 : 1;
  const mode = input.dataset.oqOduRuntimeMode === "cooling" ? "cooling" : "heating";
  const level = Number(input.dataset.oqOduRuntimeLevel);
  if (!Number.isInteger(level) || level < 0 || level > 20) return false;
  state.oduRuntimeFrequencyDrafts = {
    ...(state.oduRuntimeFrequencyDrafts || {}),
    [getDraftKey(hp, mode, level)]: String(input.value || ""),
  };
  return true;
}

function hydrateDrafts(status) {
  if (!status?.loaded) return;
  const next = { ...(state.oduRuntimeFrequencyDrafts || {}) };
  ODU_RUNTIME_FREQUENCY_MODES.forEach((mode) => {
    for (let level = 0; level < status.levelCount; level += 1) {
      next[getDraftKey(status.hp, mode, level)] = String(status[mode][level] ?? "");
    }
  });
  state.oduRuntimeFrequencyDrafts = next;
}

async function fetchStatus(hp) {
  const response = await fetchWithTimeout(
    getOduRuntimeFrequencyEndpoint(hp, "status"),
    { cache: "no-store", headers: { "Cache-Control": "no-store" } },
    8000,
    `HP${hp} status reageert niet`,
  );
  if (response.status === 404) {
    return normalizeOduRuntimeFrequencyStatus({ available: false, unsupported: true, hp }, hp);
  }
  if (!response.ok) throw new Error(`HP${hp} status HTTP ${response.status}`);
  return normalizeOduRuntimeFrequencyStatus(await response.json(), hp);
}

export function shouldRefreshOduRuntimeFrequencySurface() {
  return state.systemModal === "odu-frequency-settings";
}

export async function refreshOduRuntimeFrequencyStatuses(options = {}) {
  if (!shouldRefreshOduRuntimeFrequencySurface() && options.force !== true) return false;
  if (state.oduRuntimeFrequencyFetchPromise) return state.oduRuntimeFrequencyFetchPromise;
  const now = Date.now();
  if (!options.force && now - Number(state.oduRuntimeFrequencyLastFetchAt || 0) < 5000) {
    return false;
  }
  const previousSignature = JSON.stringify(state.oduRuntimeFrequencyStatuses || {});
  state.oduRuntimeFrequencyFetchPromise = (async () => {
    try {
      const entries = await Promise.all(getOduRuntimeFrequencyHpIndexes().map(async (hp) => [hp, await fetchStatus(hp)]));
      state.oduRuntimeFrequencyStatuses = Object.fromEntries(entries);
      state.oduRuntimeFrequencyLastFetchAt = Date.now();
      state.oduRuntimeFrequencyError = "";
      const changed = JSON.stringify(state.oduRuntimeFrequencyStatuses) !== previousSignature;
      if (changed && shouldRefreshOduRuntimeFrequencySurface()) render();
      return changed;
    } catch (error) {
      state.oduRuntimeFrequencyError = `Status mislukt. ${error.message || String(error)}`;
      if (!options.silent) render();
      return false;
    } finally {
      state.oduRuntimeFrequencyFetchPromise = null;
    }
  })();
  return state.oduRuntimeFrequencyFetchPromise;
}

function getRequestErrorMessage(error) {
  if (error === "busy") return "ODU of Modbus-bus is bezig";
  if (error === "load_required") return "laad de actuele ODU-tabel eerst";
  if (error === "arm_required") return "geef runtime writes eerst vrij";
  if (error === "invalid_table") return "de frequentietabel is ongeldig";
  if (error === "forbidden") return "de beveiligingscontrole is verlopen; laad de pagina opnieuw";
  return error || "actie geweigerd";
}

async function postAction(hp, action, values = {}) {
  let status = getOduRuntimeFrequencyStatus(hp);
  if (!status?.csrfToken) {
    status = await fetchStatus(hp);
    storeStatus(hp, status);
  }
  if (!status.csrfToken) throw new Error("CSRF ontbreekt");

  const body = new URLSearchParams();
  body.set("csrf_token", status.csrfToken);
  Object.entries(values).forEach(([key, value]) => body.set(key, String(value)));
  const response = await fetchWithTimeout(
    getOduRuntimeFrequencyEndpoint(hp, action),
    {
      method: "POST",
      cache: "no-store",
      headers: {
        "Cache-Control": "no-store",
        "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8",
      },
      body: body.toString(),
    },
    8000,
    `HP${hp} actie reageert niet`,
  );
  const payload = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(getRequestErrorMessage(payload.error || `HTTP ${response.status}`));
  const next = normalizeOduRuntimeFrequencyStatus(payload, hp);
  storeStatus(hp, next);
  return next;
}

async function waitForOperation(hp) {
  const deadline = Date.now() + 65000;
  while (Date.now() < deadline) {
    await new Promise((resolve) => window.setTimeout(resolve, 500));
    const status = await fetchStatus(hp);
    storeStatus(hp, status);
    render();
    if (!status.busy) return status;
  }
  throw new Error("firmware bleef langer dan 65 seconden bezig");
}

function collectDraftTable(hp) {
  const status = getOduRuntimeFrequencyStatus(hp);
  const count = status?.levelCount === 21 ? 21 : 11;
  const collect = (mode) => Array.from({ length: count }, (_item, level) => {
    const value = Number(String(getOduRuntimeFrequencyDraftValue(hp, mode, level)).replace(",", "."));
    return Number.isInteger(value) ? value : Number.NaN;
  });
  return { cooling: collect("cooling"), heating: collect("heating") };
}

async function runOperation(hp, action) {
  state.busyAction = `odu-runtime-hp${hp}-${action}`;
  state.controlNotice = "";
  state.controlError = "";
  render();
  try {
    let values = {};
    if (action === "apply") {
      const table = collectDraftTable(hp);
      values = { cooling: table.cooling.join(","), heating: table.heating.join(",") };
    }
    let status = await postAction(hp, action, values);
    if (status.busy) status = await waitForOperation(hp);
    if (action === "load" && status.loaded) hydrateDrafts(status);
    const upper = status.status.toUpperCase();
    if (upper.includes("FAILED") || upper.includes("BLOCKED")) {
      throw new Error(status.status);
    }
    state.controlNotice = action === "load"
      ? `HP${hp}: frequentietabel geladen.`
      : `HP${hp}: frequentietabel gecontroleerd en toegepast.`;
  } catch (error) {
    state.controlError = `HP${hp}: actie mislukt. ${error.message || String(error)}`;
  } finally {
    state.busyAction = "";
    state.oduRuntimeFrequencyLastFetchAt = 0;
    render();
  }
}

async function toggleArm(hp) {
  const enabled = getOduRuntimeFrequencyStatus(hp)?.armed !== true;
  state.busyAction = `odu-runtime-hp${hp}-arm`;
  state.controlNotice = "";
  state.controlError = "";
  render();
  try {
    await postAction(hp, "arm", { enabled });
    state.controlNotice = `HP${hp}: wijzigingen ${enabled ? "vrijgegeven" : "vergrendeld"}.`;
  } catch (error) {
    state.controlError = `HP${hp} write-lock aanpassen mislukt. ${error.message || String(error)}`;
  } finally {
    state.busyAction = "";
    render();
  }
}

export function handleOduRuntimeFrequencyInputKeyDown(event) {
  if (event.key !== "Tab" || event.altKey || event.ctrlKey || event.metaKey) return;
  const input = event.target?.closest?.("input[data-oq-odu-runtime-tab-index]");
  const table = input?.closest(".oq-settings-odu-runtime-table");
  if (!input || !table) return;
  const inputs = Array.from(table.querySelectorAll("input[data-oq-odu-runtime-tab-index]:not(:disabled)"))
    .sort((left, right) => Number(left.dataset.oqOduRuntimeTabIndex || 0) - Number(right.dataset.oqOduRuntimeTabIndex || 0));
  const currentIndex = inputs.indexOf(input);
  const nextInput = inputs[currentIndex + (event.shiftKey ? -1 : 1)];
  if (currentIndex < 0 || !nextInput) return;
  event.preventDefault();
  nextInput.focus();
  nextInput.select?.();
}

const actionHandlers = {
  "odu-runtime-load": (button) => runOperation(Number(button.dataset.hp) === 2 ? 2 : 1, "load"),
  "odu-runtime-arm": (button) => toggleArm(Number(button.dataset.hp) === 2 ? 2 : 1),
  "odu-runtime-apply": (button) => runOperation(Number(button.dataset.hp) === 2 ? 2 : 1, "apply"),
};

export function handleOduRuntimeFrequencyAction(action, button) {
  return invokeActionMap(actionHandlers, action, button);
}

function getOperationState(hp) {
  const devWriteState = __OQ_PREVIEW__ && typeof window !== "undefined"
    ? String(window.__OQ_DEV_ODU_WRITE_STATE__ || "")
    : "";
  const mode = devWriteState === "standby"
    ? "Standby"
    : devWriteState === "running"
      ? "Heating"
      : String(getEntityValue(`hp${hp}Mode`) || "").trim();
  const frequency = devWriteState === "standby"
    ? 0
    : devWriteState === "running"
      ? 30
      : parseLooseNumber(getEntityValue(`hp${hp}Freq`));
  const modeKnown = mode && !/onbekend|unknown/i.test(mode);
  const frequencyKnown = Number.isFinite(frequency);
  const standby = modeKnown && /standby|stand-by/i.test(mode);
  const stopped = frequencyKnown && frequency <= 0.5;
  return {
    safe: standby && stopped,
    copy: !modeKnown
      ? "De toestand van de buitenunit is nog niet bekend."
      : !standby
        ? `De buitenunit staat in ${mode}.`
        : !frequencyKnown
          ? "De compressorfrequentie is nog niet bekend."
          : !stopped
            ? `De compressor draait op ${frequency.toFixed(0)} Hz.`
            : "De buitenunit staat stil en kan veilig worden gewijzigd.",
  };
}

function getTableValidation(hp) {
  const status = getOduRuntimeFrequencyStatus(hp);
  const levelCount = status?.levelCount === 21 ? 21 : 11;
  const invalid = [];
  ODU_RUNTIME_FREQUENCY_MODES.forEach((mode) => {
    let previous = -1;
    for (let level = 0; level < levelCount; level += 1) {
      const value = Number(String(getOduRuntimeFrequencyDraftValue(hp, mode, level)).replace(",", "."));
      if (!Number.isInteger(value) || value < previous || value > 120 || (level === 0 ? value !== 0 : value < 1)) {
        invalid.push(`${mode === "cooling" ? "C" : "H"}F${level}`);
      }
      if (Number.isFinite(value)) previous = value;
    }
  });
  return { valid: invalid.length === 0, invalid };
}

function getStatusPresentation(status) {
  const code = String(status || "").toUpperCase();
  if (code.includes("APPLIED")) return ["De gekozen waarden zijn actief", "success"];
  if (code.includes("LOADED")) return ["Waarden uit de buitenunit geladen", "success"];
  if (code.includes("BLOCKED")) return ["Wacht tot de buitenunit stilstaat", "warning"];
  if (code.includes("FAILED")) return ["Toepassen kon niet worden bevestigd", "warning"];
  if (code.includes("WRITE") || code.includes("GUARD") || code.includes("REQUESTED")) return ["Bezig met controleren", ""];
  return ["Laad eerst de actuele waarden", ""];
}

function renderFrequencyInput(hp, mode, level, tabIndex) {
  const status = getOduRuntimeFrequencyStatus(hp);
  return `
    <label class="oq-helper-control oq-helper-control--suffix oq-settings-odu-runtime-control">
      <input class="oq-helper-input oq-helper-input--compact-number oq-settings-odu-runtime-input"
        type="number" min="0" max="120" step="1" inputmode="numeric"
        value="${escapeHtml(getOduRuntimeFrequencyDraftValue(hp, mode, level))}"
        data-oq-odu-runtime-hp="${hp}" data-oq-odu-runtime-mode="${mode}"
        data-oq-odu-runtime-level="${level}" data-oq-odu-runtime-tab-index="${tabIndex}"
        aria-label="${escapeHtml(`HP${hp} ${mode === "cooling" ? "koelen" : "verwarmen"} F${level}`)}"
        ${!status?.loaded || status.busy ? "disabled" : ""}>
      <span class="oq-helper-unit-chip">Hz</span>
    </label>`;
}

function renderFrequencyTable(hp) {
  const status = getOduRuntimeFrequencyStatus(hp);
  const levels = ODU_RUNTIME_FREQUENCY_LEVELS.slice(0, status?.levelCount === 21 ? 21 : 11);
  return `
    <div class="oq-settings-odu-runtime-table" role="table" aria-label="${escapeHtml(`HP${hp} frequentietabel`)}">
      <div class="oq-settings-odu-runtime-row oq-settings-odu-runtime-row--head" role="row">
        <span role="columnheader">Niveau</span><span role="columnheader">Koelen</span><span role="columnheader">Verwarmen</span>
      </div>
      ${levels.map((level) => `
        <div class="oq-settings-odu-runtime-row" role="row">
          <span class="oq-settings-odu-runtime-level" role="cell">F${level}</span>
          <div role="cell">${renderFrequencyInput(hp, "cooling", level, level)}</div>
          <div role="cell">${renderFrequencyInput(hp, "heating", level, levels.length + level)}</div>
        </div>`).join("")}
    </div>`;
}

function renderFrequencyPanel(hp) {
  const status = getOduRuntimeFrequencyStatus(hp);
  const operation = getOperationState(hp);
  const validation = getTableValidation(hp);
  const available = status && status.available !== false && !status.unsupported;
  const busy = status?.busy === true || String(state.busyAction || "").startsWith(`odu-runtime-hp${hp}-`);
  const armed = status?.armed === true;
  const [statusLabel, statusTone] = getStatusPresentation(status?.status);
  const applyDisabled = busy || !status?.loaded || !armed || !validation.valid || !operation.safe || !available;
  return `
    <article class="oq-settings-odu-runtime-panel">
      <div class="oq-settings-odu-runtime-panel-head">
        <div><p class="oq-helper-label">HP${hp}</p><h4>Frequentietabel</h4><p>${escapeHtml(operation.copy)}</p></div>
        <div class="oq-settings-odu-runtime-actions">
          <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="odu-runtime-load" data-hp="${hp}" ${busy || !available ? "disabled" : ""}>Uit buitenunit laden</button>
          <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="odu-runtime-arm" data-hp="${hp}" ${busy || !status?.loaded || !available ? "disabled" : ""}>${armed ? "Wijzigingen vergrendelen" : "Wijzigingen vrijgeven"}</button>
          <button class="oq-helper-button oq-helper-button--warning" type="button" data-oq-action="odu-runtime-apply" data-hp="${hp}" ${applyDisabled ? "disabled" : ""}>${busy ? "Bezig..." : "Toepassen"}</button>
        </div>
      </div>
      <div class="oq-settings-odu-runtime-status${statusTone ? ` is-${statusTone}` : ""}"><strong>${escapeHtml(statusLabel)}</strong></div>
      ${status?.loaded ? renderFrequencyTable(hp) : '<p class="oq-settings-odu-runtime-validation is-warning">Laad eerst de actuele tabel; er worden geen standaardwaarden ingevuld.</p>'}
      ${!status?.loaded || validation.valid ? "" : `<p class="oq-settings-odu-runtime-validation is-warning">Controleer ${escapeHtml(validation.invalid.slice(0, 6).join(", "))}.</p>`}
    </article>`;
}

export function renderOduRuntimeFrequencyModal() {
  const hpIndexes = getOduRuntimeFrequencyHpIndexes();
  return renderModalShell({
    modalId: "odu-frequency-settings",
    titleId: "oq-odu-frequency-title",
    kicker: "Instellingen buitenunit",
    title: "Frequentietabel",
    titleBadge: "Experimenteel",
    closeAction: "close-system-modal",
    closeLabel: "Sluit frequentietabel",
    modalClass: "oq-helper-modal--wide oq-settings-odu-modal",
    bodyMarkup: `
      <div class="oq-settings-odu-modal-body" data-oq-modal-scroll="body">
        <div class="oq-settings-odu-runtime-warning" role="note">
          <strong>Niet permanent opgeslagen</strong>
          <p>Als de buitenunit volledig stroomloos is geweest, worden jouw aanpassingen teruggezet naar de oorspronkelijke frequenties.</p>
          <p>Wijzig alleen waarden waarvan je het effect op de compressor kent. Toepassen is alleen mogelijk in standby met de compressor uit.</p>
          <p>Wees bij Quatt buitenunits V1 en V1.5 voorzichtig met koelwaarden onder 30 Hz: bij een te lage frequentie kan vloeibaar koudemiddel terugstromen en de compressor beschadigen. Bij V2 is 20 Hz toegestaan.</p>
        </div>
        ${state.oduRuntimeFrequencyError ? `<p class="oq-helper-error" role="alert">${escapeHtml(state.oduRuntimeFrequencyError)}</p>` : ""}
        <div class="oq-settings-odu-runtime-panels">${hpIndexes.map(renderFrequencyPanel).join("")}</div>
        <details class="oq-settings-odu-technical"${state.oduRuntimeFrequencyTechnicalDetailsOpen ? " open" : ""}><summary data-oq-action="toggle-odu-frequency-technical-details">Technische details</summary><p>De tabel wordt direct naar tijdelijke Modbus-registers van de buitenunit geschreven. OpenQuatt bewaart deze frequenties niet.</p></details>
      </div>`,
  });
}
