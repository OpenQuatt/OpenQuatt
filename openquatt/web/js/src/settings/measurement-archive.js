import { createCancellationController, downloadBlobFile } from "../core/browser-utils.js";
import { getEntityValue, hasEntity } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { archiveDate, archiveDownloadPath, assembleMeasurementArchive } from "../features/measurement-archive-export.js";
import { formatDateTime, formatNumber, getIntlLocale, t } from "../i18n/index.js";
import { renderSettingsCompactSwitchControl } from "./controls.js";
import { getSettingsSwitchModel } from "./field-models.js";

const switchKey = "measurementArchiveEnabled";
const statusKey = "measurementArchiveStatus";
const statusTranslations = new Map([
  ["off", "measurementArchive.off"],
  ["waiting", "measurementArchive.waiting"],
  ["waiting_clock", "measurementArchive.waiting_clock"],
  ["waiting_sources", "measurementArchive.waiting_sources"],
  ["paused", "measurementArchive.paused"],
  ["recording", "measurementArchive.recording"],
  ["recording_unregistered", "measurementArchive.recording_unregistered"],
  ["storage_unavailable", "measurementArchive.storage_unavailable"],
  ["storage_error", "measurementArchive.storage_error"],
  ["preference_error", "measurementArchive.preference_error"],
  ["archive_unlinked", "measurementArchive.archive_unlinked"],
]);

let exportRequest = null;
let exportFeedback = null;

const supported = () => hasEntity(switchKey) && hasEntity(statusKey);
const label = (key) => escapeHtml(t(key));

function archiveModel() {
  const [code, ...values] = String(getEntityValue(statusKey) ?? "").split("|");
  const [first, last, count] = values.map(Number);
  const locked = code === "archive_unlinked" || code === "storage_unavailable";
  const hasRecords = !locked && values.length === 3 && values.every((value) => /^\d+$/.test(value))
    && first > 0 && last >= first && last <= 0xffffffff && Number.isSafeInteger(count) && count > 0;
  const toggle = getSettingsSwitchModel(switchKey, { title: t("measurementArchive.enable") });
  return {
    status: t(statusTranslations.get(code) ?? "measurementArchive.loading"),
    snapshot: hasRecords ? { first, last, count } : null,
    firstDay: hasRecords ? archiveDate(first) : "",
    lastDay: hasRecords ? archiveDate(last) : "",
    empty: locked ? "" : t(!toggle.enabled && code === "off" ? "measurementArchive.emptyOff" : "measurementArchive.empty"),
    toggle, locked,
  };
}

function periodText({ first, last }) {
  const start = new Date(first * 1000);
  const end = new Date(last * 1000);
  const options = { dateStyle: "medium" };
  if (start.toDateString() === end.toDateString()) options.timeStyle = "short";
  const formatter = new Intl.DateTimeFormat(getIntlLocale(), options);
  if (typeof formatter.formatRange === "function") return formatter.formatRange(start, end);
  return `${formatDateTime(start, options)} – ${formatDateTime(end, options)}`;
}

const measureCount = ({ count }) => t("measurementArchive.blockCount", { count: formatNumber(count) });
const progressText = () => exportRequest
  ? t("measurementArchive.progress", { done: formatNumber(exportRequest.done), total: formatNumber(exportRequest.total) })
  : exportFeedback === null ? "" : t(exportFeedback);

export function renderMeasurementArchive() {
  if (!supported()) return "";
  const { status, snapshot, firstDay, lastDay, empty, toggle, locked } = archiveModel();
  const inProgress = Boolean(exportRequest);
  return `<section class="oq-settings-section oq-measurement-archive" data-oq-measurement-archive>
    <div class="oq-archive-heading">
      <div><h4 class="oq-learning-diagnostic-title">${label("measurementArchive.title")}</h4>
        <p data-oq-archive-status role="status">${escapeHtml(status)}</p></div>
      ${renderSettingsCompactSwitchControl(switchKey, t("measurementArchive.enable"), toggle.enabled, toggle.busy || locked, null, null, false)}
    </div>
    <p data-oq-archive-empty ${snapshot || !empty ? "hidden" : ""}>${escapeHtml(empty)}</p>
    <div class="oq-archive-data" data-oq-archive-data ${snapshot ? "" : "hidden"}>
      <div class="oq-archive-summary">
        <div>
          <span class="oq-archive-label">${label("measurementArchive.periodLabel")}</span>
          <strong data-oq-archive-period>${snapshot ? escapeHtml(periodText(snapshot)) : ""}</strong>
          <p data-oq-archive-count>${snapshot ? escapeHtml(measureCount(snapshot)) : ""}</p>
        </div>
        <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="download-measurement-archive" ${inProgress ? "disabled" : ""}>${label("measurementArchive.downloadAll")}</button>
      </div>
      <details class="oq-archive-day">
        <summary>${label("measurementArchive.chooseDay")}</summary>
        <form action="${escapeHtml(archiveDownloadPath())}" method="get">
          <label>${label("measurementArchive.date")}
            <input class="oq-helper-input" type="date" name="date" required
              min="${firstDay}" max="${lastDay}" value="${lastDay}" ${!snapshot || inProgress ? "disabled" : ""}>
          </label>
          <button class="oq-helper-button oq-helper-button--ghost" type="submit" ${!snapshot || inProgress ? "disabled" : ""}>${label("measurementArchive.download")}</button>
        </form>
      </details>
    </div>
    <div class="oq-archive-feedback" data-oq-archive-feedback ${inProgress || exportFeedback !== null ? "" : "hidden"}>
      <p data-oq-archive-progress role="status">${escapeHtml(progressText())}</p>
      <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="cancel-measurement-archive-download" ${inProgress ? "" : "hidden"}>${label("measurementArchive.cancel")}</button>
    </div>
    <details class="oq-archive-day oq-archive-info">
      <summary>${label("measurementArchive.fields")}</summary>
      <ul>
        <li>${label("measurementArchive.fieldsTemperature")}</li>
        <li>${label("measurementArchive.fieldsHeat")}</li>
        <li>${label("measurementArchive.fieldsOperation")}</li>
      </ul>
      <p>${label("measurementArchive.fieldsWeather")}</p>
    </details>
  </section>`;
}

function patchText(panel, selector, value) {
  const element = panel.querySelector(selector);
  if (element.textContent !== value) element.textContent = value;
  return element;
}

export function patchMeasurementArchive() {
  const panel = state.root?.querySelector("[data-oq-measurement-archive]");
  if (Boolean(panel) !== supported()) return false;
  if (!panel) return true;
  const { status, snapshot, firstDay, lastDay, empty, toggle, locked } = archiveModel();
  const control = panel.querySelector(`[data-control-key="${switchKey}"]`);
  const input = panel.querySelector("[name=date]");
  if (!control || !input) return false;
  patchText(panel, "[data-oq-archive-status]", status);
  patchText(panel, "[data-oq-archive-empty]", empty).hidden = Boolean(snapshot) || !empty;
  patchText(panel, "[data-oq-archive-period]", snapshot ? periodText(snapshot) : "");
  patchText(panel, "[data-oq-archive-count]", snapshot ? measureCount(snapshot) : "");
  const inProgress = Boolean(exportRequest);
  panel.querySelector("[data-oq-archive-data]").hidden = !snapshot;
  panel.querySelector('[data-oq-action="download-measurement-archive"]').disabled = !snapshot || inProgress;
  panel.querySelector('[data-oq-action="cancel-measurement-archive-download"]').hidden = !inProgress;
  patchText(panel, "[data-oq-archive-progress]", progressText());
  panel.querySelector("[data-oq-archive-feedback]").hidden = !inProgress && exportFeedback === null;
  control.disabled = toggle.busy || locked;
  control.setAttribute("aria-checked", String(toggle.enabled));
  control.setAttribute("aria-label", toggle.ariaLabel);
  control.dataset.controlState = toggle.nextState;
  control.classList.toggle("is-on", toggle.enabled);
  if (document.activeElement !== input && (!input.value || input.value === input.max || input.value < firstDay || input.value > lastDay)) {
    input.value = lastDay;
  }
  input.min = firstDay;
  input.max = lastDay;
  input.disabled = panel.querySelector("[type=submit]").disabled = !snapshot || inProgress;
  return true;
}

async function downloadArchive() {
  if (!supported() || exportRequest) return;
  const { snapshot, firstDay, lastDay } = archiveModel();
  if (!snapshot) return;
  const request = { cancel: createCancellationController(), done: 0, total: 0 };
  exportRequest = request;
  exportFeedback = null;
  try {
    const blob = await assembleMeasurementArchive(snapshot, request.cancel.signal, (done, total) => {
      if (state.appView !== "learning" || state.learningPanel !== "diagnostics") request.cancel.abort();
      Object.assign(request, { done, total });
      patchMeasurementArchive();
    });
    downloadBlobFile(blob, `openquatt-meetarchief-${firstDay}_${lastDay}.json`);
    exportFeedback = "measurementArchive.downloadStarted";
  } catch (_error) {
    exportFeedback = request.cancel.signal.aborted ? "measurementArchive.downloadCancelled" : "measurementArchive.downloadFailed";
  } finally {
    exportRequest = null;
    patchMeasurementArchive();
  }
}

export function handleMeasurementArchiveAction(action) {
  if (action === "download-measurement-archive") {
    void downloadArchive();
    return true;
  }
  if (action === "cancel-measurement-archive-download") {
    exportRequest?.cancel.abort();
    return true;
  }
  return false;
}
