import { hasEntity, isEntityActive } from "../core/app-shared.js";
import { ENTITY_DEFS, SETTINGS_BACKUP_KEYS, SETTINGS_BACKUP_SECTIONS } from "../core/config.js";
import { parseEnergyHistoryDateKey, parseEnergyHistoryMetadata } from "../core/energy-history-domain.js";
import { getEntityValue } from "../core/entity-store.js";
import { settingsBackupMqttNeedsPassword } from "../core/settings-backup-domain.js";
import { state } from "../core/state.js";
import { getInstallationLabel, getInstallationTopology } from "../features/device-context.js";
import { getFirmwareCurrentVersion } from "../features/firmware-update.js";
import { ENERGY_HISTORY_EXPORT_MODES, getSettingsBackupSelectionSummary, normalizeEnergyHistoryExportMode } from "../features/storage-history.js";
import { getSettingsStatValue, renderSettingsCompactSwitchControl, renderSettingsSection, renderSettingsSelectControl, renderSettingsSwitchCopy } from "./controls.js";
import { getSettingsSelectModel } from "./field-models.js";
import { getElectricalLimitBackupRestoreWarning } from "./electrical-limit.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";
import { formatDate, formatNumber, formatTime, t } from "../i18n/index.js";

  export function renderSettingsStorageSummaryMetric(label, value, meta = "", enabled = false) {
    return `
      <div class="oq-settings-storage-summary-metric${enabled ? " is-on" : ""}">
        <span>${escapeHtml(label)}</span>
        <strong>${escapeHtml(value)}</strong>
        ${meta ? `<em>${escapeHtml(meta)}</em>` : ""}
      </div>
    `;
  }

  export function formatSettingsStoredDaysLabel(value) {
    const text = String(value || "").trim();
    const match = text.match(/^(\d+(?:[.,]\d+)?)\s+records?$/i);
    if (!match) {
      return text;
    }
    return Number(match[1]) === 1
      ? t("settingsStorage.storedDaysOne", { count: match[1] })
      : t("settingsStorage.storedDaysOther", { count: match[1] });
  }

  export function renderSettingsStorageSwitchRow(key, title, copy, enabledCopy = "", disabledCopy = "", meta = "") {
    if (!hasEntity(key)) {
      return "";
    }

    const enabled = Boolean(getEntityValue(key));
    const busy = state.loadingEntities || state.busyAction === `switch-${key}`;
    return `
      <article class="oq-settings-storage-row" data-oq-settings-field="${escapeHtml(key)}">
        <div class="oq-settings-storage-row-copy">
          <div class="oq-settings-storage-row-title">
            <h4>${escapeHtml(title)}</h4>
            ${meta ? `<span>${escapeHtml(meta)}</span>` : ""}
          </div>
          <p>${escapeHtml(copy)}</p>
          ${renderSettingsSwitchCopy(key, enabled, enabledCopy, disabledCopy)}
        </div>
        ${renderSettingsCompactSwitchControl(key, title, enabled, busy)}
      </article>
    `;
  }

  export function renderSettingsStorageSelectRow(key, title, copy, meta = "") {
    const model = getSettingsSelectModel(key);
    if (!model.available || !model.options.length) {
      return "";
    }

    return `
      <article class="oq-settings-storage-row oq-settings-storage-row--select" data-oq-settings-field="${escapeHtml(key)}">
        <div class="oq-settings-storage-row-copy">
          <div class="oq-settings-storage-row-title">
            <h4>${escapeHtml(title)}</h4>
            ${meta ? `<span>${escapeHtml(meta)}</span>` : ""}
          </div>
          <p>${escapeHtml(copy)}</p>
        </div>
        <label class="oq-settings-storage-select">
          ${renderSettingsSelectControl(key, model)}
          <span class="oq-settings-select-caret" aria-hidden="true"></span>
        </label>
      </article>
    `;
  }

  export function shouldRenderSettingsStorageActionButton(key) {
    return hasEntity(key) || (Boolean(ENTITY_DEFS[key]) && !state.optionalMissingEntities?.[key]);
  }

  export function renderSettingsStorageActionButton(key, buttonLabel, action, options = {}) {
    if (!shouldRenderSettingsStorageActionButton(key)) {
      return "";
    }

    const entityAvailable = hasEntity(key);
    const busy = entityAvailable && (state.loadingEntities || state.busyAction === key);
    const disabled = options.disabled === true || !entityAvailable;
    const buttonClass = options.buttonClass || "oq-helper-button oq-helper-button--ghost";
    return `
      <button
        class="${escapeHtml(buttonClass)}"
        type="button"
        data-oq-action="${escapeHtml(action)}"
        ${busy || disabled ? "disabled" : ""}
      >
        ${escapeHtml(busy ? (options.busyLabel || buttonLabel) : buttonLabel)}
      </button>
    `;
  }

  export function getSettingsTrendHistoryMetadata() {
    return state.trendHistoryMetadata && typeof state.trendHistoryMetadata === "object"
      ? state.trendHistoryMetadata
      : {};
  }

  export function hasSettingsTrendHistoryMetadata() {
    return Boolean(state.trendHistoryMetadataSignature);
  }

  export function hasSettingsEnergyHistoryMetadata() {
    return Boolean(state.energyHistoryRaw || state.energyHistorySignature);
  }

  export function getSettingsStorageLoadingLabel(error) {
    return error ? t("settingsStorage.loadingError") : t("settingsStorage.loadingBusy");
  }

  export function getSettingsStorageStatOrFallback(key, fallback = "—") {
    if (hasEntity(key)) {
      return getSettingsStatValue(key);
    }
    const metadataValue = getSettingsStorageMetadataStat(key);
    return metadataValue === null || metadataValue === undefined || metadataValue === "" ? fallback : metadataValue;
  }

  export function getSettingsStorageMetadataStat(key) {
    const trendMetadata = getSettingsTrendHistoryMetadata();
    if (key === "trendHistoryFlashAvailable") {
      if (!hasSettingsTrendHistoryMetadata()) {
        return getSettingsStorageLoadingLabel(state.trendHistoryMetadataError);
      }
      return trendMetadata.available || t("settingsStorage.liveOnly");
    }
    if (key === "trendHistoryFlashOldest") {
      if (!hasSettingsTrendHistoryMetadata()) {
        return getSettingsStorageLoadingLabel(state.trendHistoryMetadataError);
      }
      return trendMetadata.oldest || t("settingsStorage.noData");
    }
    if (key === "trendHistoryFlashNewest") {
      if (!hasSettingsTrendHistoryMetadata()) {
        return getSettingsStorageLoadingLabel(state.trendHistoryMetadataError);
      }
      return trendMetadata.newest || t("settingsStorage.noData");
    }
    if (key === "trendHistoryFlashLastFlush") {
      if (!hasSettingsTrendHistoryMetadata()) {
        return getSettingsStorageLoadingLabel(state.trendHistoryMetadataError);
      }
      return trendMetadata.lastFlush || t("settingsStorage.noData");
    }
    if (key === "trendHistoryFlashSize") {
      if (!hasSettingsTrendHistoryMetadata()) {
        return getSettingsStorageLoadingLabel(state.trendHistoryMetadataError);
      }
      return formatSettingsStorageKb(trendMetadata.sizeKb);
    }
    if (key === "trendHistoryFlashWrites") {
      if (!hasSettingsTrendHistoryMetadata()) {
        return getSettingsStorageLoadingLabel(state.trendHistoryMetadataError);
      }
      return formatSettingsStorageCount(trendMetadata.writes);
    }
    if (key === "trendHistoryFlashErases") {
      return formatSettingsStorageCount(trendMetadata.eraseCount);
    }
    if (key === "trendHistoryFlashMaxEraseDuration") {
      return `${formatSettingsStorageCount(trendMetadata.maxEraseDurationMs)} ms`;
    }
    if (key === "trendHistoryFlashMaxWriteDuration") {
      return `${formatSettingsStorageCount(trendMetadata.maxWriteDurationMs)} ms`;
    }
    if (key === "trendHistoryFlashMaxFlushDuration") {
      return `${formatSettingsStorageCount(trendMetadata.maxFlushDurationMs)} ms`;
    }
    if (key === "trendHistoryFlashMaxIndexUpdateDuration") {
      return `${formatSettingsStorageCount(trendMetadata.maxIndexUpdateDurationMs)} ms`;
    }
    if (key === "trendHistoryFlashFailures") {
      return formatSettingsStorageCount(Number(trendMetadata.eraseFailures || 0) + Number(trendMetadata.writeFailures || 0));
    }

    const energyMetadata = getSettingsEnergyHistoryMetadata();
    const energyRaw = String(state.energyHistoryRaw || "");
    const hasDayRetentionMetadata = energyRaw.includes("@day_retention|");
    if (key === "lifetimeEnergyHistoryAvailable") {
      if (!hasSettingsEnergyHistoryMetadata()) {
        return getSettingsStorageLoadingLabel(state.energyHistoryError);
      }
      if (hasDayRetentionMetadata && !energyMetadata.dayPartitionAvailable) {
        return t("settingsStorage.notAvailable");
      }
      return formatSettingsStorageDayCount(energyMetadata.storedDayCount, t("settingsStorage.noData"));
    }
    if (key === "lifetimeEnergyHistoryOldest") {
      if (!hasSettingsEnergyHistoryMetadata()) {
        return getSettingsStorageLoadingLabel(state.energyHistoryError);
      }
      return formatSettingsStorageDateKey(energyMetadata.oldestDateKey);
    }
    if (key === "lifetimeEnergyHistoryNewest") {
      if (!hasSettingsEnergyHistoryMetadata()) {
        return getSettingsStorageLoadingLabel(state.energyHistoryError);
      }
      return formatSettingsStorageDateKey(energyMetadata.newestDateKey);
    }
    if (key === "lifetimeEnergyHistoryLastWrite") {
      if (!hasSettingsEnergyHistoryMetadata()) {
        return getSettingsStorageLoadingLabel(state.energyHistoryError);
      }
      return formatSettingsStorageTimestamp(energyMetadata.dayLastWriteTimestampS);
    }
    if (key === "lifetimeEnergyHistorySize") {
      if (!hasSettingsEnergyHistoryMetadata()) {
        return getSettingsStorageLoadingLabel(state.energyHistoryError);
      }
      return formatSettingsStorageKb(energyMetadata.dayStorageKb);
    }
    if (key === "lifetimeEnergyHistoryWrites") {
      if (!hasSettingsEnergyHistoryMetadata()) {
        return getSettingsStorageLoadingLabel(state.energyHistoryError);
      }
      return formatSettingsStorageCount(energyMetadata.dayWriteCount);
    }
    return null;
  }

  export function formatSettingsStorageDayCount(value, fallback = null) {
    const resolvedFallback = fallback ?? t("settingsStorage.noData");
    const count = Number(value);
    if (!Number.isFinite(count) || count <= 0) {
      return resolvedFallback;
    }
    const rounded = Math.round(count);
    return rounded === 1
      ? t("settingsStorage.dayCountOne", { count: formatNumber(rounded, { maximumFractionDigits: 0 }) })
      : t("settingsStorage.dayCountOther", { count: formatNumber(rounded, { maximumFractionDigits: 0 }) });
  }

  export function formatSettingsStorageEventCount(value, fallback = null) {
    const resolvedFallback = fallback ?? t("settingsStorage.eventCountNone");
    const count = Math.max(0, Math.round(Number(value) || 0));
    if (count <= 0) {
      return resolvedFallback;
    }
    return count === 1
      ? t("settingsStorage.eventCountOne", { count: formatNumber(count, { maximumFractionDigits: 0 }) })
      : t("settingsStorage.eventCountOther", { count: formatNumber(count, { maximumFractionDigits: 0 }) });
  }

  export function getSettingsDecisionLogStorageMetadata() {
    return state.decisionLogStorageMetadata && typeof state.decisionLogStorageMetadata === "object"
      ? state.decisionLogStorageMetadata
      : {};
  }

  export function formatSettingsStorageKb(value, fallback = "—") {
    const amount = Number(value);
    if (!Number.isFinite(amount) || amount <= 0) {
      return fallback;
    }
    return `${Math.round(amount)} kB`;
  }

  export function formatSettingsStorageCount(value, fallback = "0") {
    const count = Number(value);
    if (!Number.isFinite(count) || count <= 0) {
      return fallback;
    }
    return String(Math.round(count));
  }

  export function formatSettingsStorageDateKey(dateKey) {
    const parsed = parseEnergyHistoryDateKey(dateKey);
    if (!parsed) {
      return t("settingsStorage.noData");
    }
    return formatDate(parsed.date, { day: "2-digit", month: "2-digit", year: "numeric" });
  }

  export function formatSettingsStorageTimestamp(seconds, fallback = null) {
    const resolvedFallback = fallback ?? t("settingsStorage.noData");
    const timestamp = Number(seconds);
    if (!Number.isFinite(timestamp) || timestamp <= 0) {
      return resolvedFallback;
    }
    const date = new Date(timestamp * 1000);
    const day = formatDate(date, { day: "2-digit", month: "2-digit" });
    const time = formatTime(date, { hour: "2-digit", minute: "2-digit" });
    return `${day} ${time}`;
  }

  export function getSettingsEnergyHistoryMetadata() {
    return parseEnergyHistoryMetadata(state.energyHistoryRaw);
  }

  export function renderSettingsStorageTechnicalRow(row) {
    const items = Array.isArray(row.items) ? row.items : [];
    return `
      <article class="oq-settings-storage-technical-row">
        <div class="oq-settings-storage-technical-row-head">
          <span>${escapeHtml(row.meta || "")}</span>
          <strong>${escapeHtml(row.title)}</strong>
          ${row.note ? `<em>${escapeHtml(row.note)}</em>` : ""}
        </div>
        <div class="oq-settings-storage-technical-metrics">
          ${items.map((item) => `
            <div>
              <span>${escapeHtml(item.label)}</span>
              <strong>${escapeHtml(item.value)}</strong>
            </div>
          `).join("")}
        </div>
      </article>
    `;
  }

  export function renderSettingsStorageTechnicalDetails(rows) {
    const visibleRows = rows.filter(Boolean);
    if (!visibleRows.length) {
      return "";
    }

    return `
      <details class="oq-settings-storage-technical"${state.settingsStorageDetailsOpen ? " open" : ""}>
        <summary data-oq-action="toggle-storage-technical-details">
          <span>
            <strong>${escapeHtml(t("settingsStorage.technicalTitle"))}</strong>
            <em>${escapeHtml(t("settingsStorage.technicalCopy"))}</em>
          </span>
          <span class="oq-settings-storage-technical-summary">${escapeHtml(visibleRows.map((row) => `${row.shortLabel}: ${row.primary}`).join(" · "))}</span>
        </summary>
        <div class="oq-settings-storage-technical-list">
          ${visibleRows.map(renderSettingsStorageTechnicalRow).join("")}
        </div>
      </details>
    `;
  }

  export function renderSettingsTrendSection() {
    if (!hasEntity("trendHistoryEnabled") && !hasEntity("decisionLogHistoryEnabled") && !hasEntity("lifetimeEnergyHistoryEnabled")) {
      return "";
    }

    const trendHistoryEnabled = isEntityActive("trendHistoryEnabled");
    const trendHistoryFlashEnabled = trendHistoryEnabled && isEntityActive("trendHistoryFlashEnabled");
    const lifetimeEnergyHistoryAvailable = hasEntity("lifetimeEnergyHistoryEnabled");
    const lifetimeEnergyHistoryEnabled = lifetimeEnergyHistoryAvailable && isEntityActive("lifetimeEnergyHistoryEnabled");
    const decisionLogHistoryAvailable = hasEntity("decisionLogHistoryEnabled");
    const decisionLogHistoryEnabled = decisionLogHistoryAvailable && isEntityActive("decisionLogHistoryEnabled");
    const decisionMetadata = getSettingsDecisionLogStorageMetadata();
    const trendAvailableValue = trendHistoryFlashEnabled
      ? getSettingsStorageStatOrFallback("trendHistoryFlashAvailable", t("settingsStorage.liveOnly"))
      : t("settingsStorage.liveOnly");
    const lifetimeAvailableValue = lifetimeEnergyHistoryAvailable
      ? formatSettingsStoredDaysLabel(getSettingsStorageStatOrFallback("lifetimeEnergyHistoryAvailable", t("settingsStorage.noData")))
      : t("settingsStorage.noData");
    return renderSettingsSection(
      t("settingsStorage.sectionGroup"),
      t("settingsStorage.sectionTitle"),
      t("settingsStorage.sectionCopy"),
      `
        <article class="oq-settings-storage-summary">
          <div class="oq-settings-storage-summary-copy">
            <h3>${escapeHtml(t("settingsStorage.summaryTitle"))}</h3>
            <p>${escapeHtml(t("settingsStorage.summaryCopy"))}</p>
          </div>
          <div class="oq-settings-storage-summary-metrics" aria-label="${escapeHtml(t("settingsStorage.summaryStatusLabel"))}">
            ${hasEntity("trendHistoryEnabled") ? renderSettingsStorageSummaryMetric(t("settingsStorage.metricDiagnosis"), trendHistoryFlashEnabled ? trendAvailableValue : (trendHistoryEnabled ? t("settingsStorage.liveOnly") : t("settingsStorage.offValue")), trendHistoryFlashEnabled ? t("settingsStorage.metricKept") : t("settingsStorage.metricTemporary"), trendHistoryEnabled) : ""}
            ${decisionLogHistoryAvailable ? renderSettingsStorageSummaryMetric(t("settingsStorage.metricLog"), decisionLogHistoryEnabled ? formatSettingsStorageEventCount(decisionMetadata.storedEvents) : t("settingsStorage.metricLogLive"), decisionLogHistoryEnabled ? t("settingsStorage.metricLogMax") : t("settingsStorage.metricTemporary"), decisionLogHistoryEnabled) : ""}
            ${lifetimeEnergyHistoryAvailable ? renderSettingsStorageSummaryMetric(t("settingsStorage.metricEnergy"), lifetimeAvailableValue, lifetimeEnergyHistoryEnabled ? t("settingsStorage.metricKept") : t("settingsStorage.offValue"), lifetimeEnergyHistoryEnabled) : ""}
          </div>
          <button class="oq-helper-button oq-helper-button--ghost oq-settings-storage-summary-action" type="button" data-oq-action="open-history-storage-modal">
            ${escapeHtml(t("settingsStorage.manageAction"))}
          </button>
        </article>
      `,
    );
  }

  export function renderSettingsEnergyHistoryImportPanel() {
    if (!hasEntity("lifetimeEnergyHistoryEnabled")) {
      return "";
    }

    const dailyCount = state.energyHistoryImportRecords.length;
    const hourDayCount = new Set(state.energyHistoryImportHourRecords.map((record) => record.dateKey)).size;
    const recordParts = [];
    if (dailyCount > 0) {
      recordParts.push(t("settingsStorage.importDayRecords", { count: formatNumber(dailyCount, { maximumFractionDigits: 0 }) }));
    }
    if (hourDayCount > 0) {
      recordParts.push(t("settingsStorage.importHourDays", { count: formatNumber(hourDayCount, { maximumFractionDigits: 0 }) }));
    }
    if (state.energyHistoryImportRange) {
      recordParts.push(state.energyHistoryImportRange);
    }
    if (state.energyHistoryImportSource) {
      recordParts.push(state.energyHistoryImportSource);
    }
    if (state.energyHistoryImportInvalidCount > 0) {
      recordParts.push(t("settingsStorage.importInvalidLines", { count: formatNumber(state.energyHistoryImportInvalidCount, { maximumFractionDigits: 0 }) }));
    }

    const hasFile = Boolean(state.energyHistoryImportFileName);
    const hasRecords = dailyCount > 0 || hourDayCount > 0;
    const progress = Number(state.energyHistoryImportProgressPercent || 0);
    const importLabel = state.energyHistoryImportBusy
      ? progress > 0 ? t("settingsStorage.importBusyProgress", { progress }) : t("settingsStorage.importBusy")
      : t("settingsStorage.importAction");

    return `
      <div class="oq-settings-storage-import">
        <div class="oq-settings-storage-import-head">
          <div>
            <h4>${escapeHtml(t("settingsStorage.importTitle"))}</h4>
            <p>${escapeHtml(t("settingsStorage.importCopy"))}</p>
          </div>
          ${!hasFile ? `
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="select-energy-history-import-file">
              ${escapeHtml(t("settingsStorage.importChoose"))}
            </button>
          ` : ""}
        </div>
        ${hasFile ? `
          <div class="oq-settings-storage-import-card">
            <div class="oq-settings-storage-import-file">
              <strong>${escapeHtml(state.energyHistoryImportFileName)}</strong>
              ${recordParts.length ? `<p>${escapeHtml(recordParts.join(" · "))}</p>` : ""}
              ${state.energyHistoryImportNotice ? `<p class="oq-settings-storage-import-notice">${escapeHtml(state.energyHistoryImportNotice)}</p>` : ""}
              ${state.energyHistoryImportError ? `<p class="oq-settings-storage-import-error">${escapeHtml(state.energyHistoryImportError)}</p>` : ""}
            </div>
            <div class="oq-settings-storage-import-actions">
              <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="clear-energy-history-import-file" ${state.energyHistoryImportBusy ? "disabled" : ""}>
                ${escapeHtml(t("settingsStorage.importClear"))}
              </button>
              <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="import-energy-history-file" ${state.energyHistoryImportBusy || !hasRecords ? "disabled" : ""}>
                ${escapeHtml(importLabel)}
              </button>
            </div>
          </div>
        ` : `
          ${state.energyHistoryImportNotice ? `<p class="oq-settings-storage-import-notice">${escapeHtml(state.energyHistoryImportNotice)}</p>` : ""}
          ${state.energyHistoryImportError ? `<p class="oq-settings-storage-import-error">${escapeHtml(state.energyHistoryImportError)}</p>` : ""}
        `}
      </div>
    `;
  }

  export function renderSettingsEnergyHistoryExportPanel() {
    if (!hasEntity("lifetimeEnergyHistoryEnabled")) {
      return "";
    }

    const mode = normalizeEnergyHistoryExportMode(state.energyHistoryExportMode);
    const options = ENERGY_HISTORY_EXPORT_MODES.map((option) => `
      <option value="${escapeHtml(option.id)}" ${option.id === mode ? "selected" : ""}>
        ${escapeHtml(option.labelKey ? t(option.labelKey) : option.label)}
      </option>
    `).join("");
    const exportLabel = state.energyHistoryExportBusy ? t("settingsStorage.exportBusy") : t("settingsStorage.exportAction");

    return `
      <div class="oq-settings-storage-import oq-settings-storage-export">
        <div class="oq-settings-storage-import-head">
          <div>
            <h4>${escapeHtml(t("settingsStorage.exportTitle"))}</h4>
            <p>${escapeHtml(t("settingsStorage.exportCopy"))}</p>
          </div>
          <div class="oq-settings-storage-export-controls">
            <select class="oq-helper-select oq-settings-storage-export-select" data-oq-energy-history-export-mode="true" ${state.energyHistoryExportBusy ? "disabled" : ""}>
              ${options}
            </select>
            <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="export-energy-history" ${state.energyHistoryExportBusy ? "disabled" : ""}>
              ${escapeHtml(exportLabel)}
            </button>
          </div>
        </div>
        ${state.energyHistoryExportNotice ? `<p class="oq-settings-storage-import-notice">${escapeHtml(state.energyHistoryExportNotice)}</p>` : ""}
        ${state.energyHistoryExportError ? `<p class="oq-settings-storage-import-error">${escapeHtml(state.energyHistoryExportError)}</p>` : ""}
      </div>
    `;
  }

  export function renderSettingsHistoryStorageModal() {
    const page = ["diagnosis", "decision-log", "energy"].includes(state.settingsStoragePage)
      ? state.settingsStoragePage
      : "overview";
    const trendHistoryEnabled = hasEntity("trendHistoryEnabled") && isEntityActive("trendHistoryEnabled");
    const trendHistoryFlashEnabled = trendHistoryEnabled && hasEntity("trendHistoryFlashEnabled") && isEntityActive("trendHistoryFlashEnabled");
    const decisionLogHistoryAvailable = hasEntity("decisionLogHistoryEnabled");
    const decisionLogHistoryEnabled = decisionLogHistoryAvailable && isEntityActive("decisionLogHistoryEnabled");
    const decisionMetadata = getSettingsDecisionLogStorageMetadata();
    const decisionEventsLabel = formatSettingsStorageEventCount(decisionMetadata.storedEvents);
    const lifetimeEnergyHistoryAvailable = hasEntity("lifetimeEnergyHistoryEnabled");
    const lifetimeEnergyHistoryEnabled = lifetimeEnergyHistoryAvailable && isEntityActive("lifetimeEnergyHistoryEnabled");
    const lifetimeAvailableLabel = lifetimeEnergyHistoryAvailable
      ? getSettingsStorageStatOrFallback("lifetimeEnergyHistoryAvailable", t("settingsStorage.noData"))
      : t("settingsStorage.noData");
    const lifetimeAvailableDaysLabel = formatSettingsStoredDaysLabel(lifetimeAvailableLabel);
    const canClearLifetime = hasEntity("lifetimeEnergyHistoryClear") && ![t("settingsStorage.noData"), "—"].includes(lifetimeAvailableLabel);
    const canFlushTrend = trendHistoryEnabled && hasEntity("trendHistoryFlush");
    const canFlushDecision = decisionLogHistoryEnabled && hasEntity("decisionLogHistoryFlush");
    const canCaptureLifetime = hasEntity("lifetimeEnergyHistoryCapture");
    const energyMetadata = getSettingsEnergyHistoryMetadata();
    const hasHourMetadata = String(state.energyHistoryRaw || "").includes("@hour_retention|");
    const hourFlashUnavailable = hasHourMetadata && !energyMetadata.hourPartitionAvailable;
    const hourStoredLabel = hasHourMetadata
      ? hourFlashUnavailable ? t("settingsStorage.liveOnly") : formatSettingsStorageDayCount(energyMetadata.hourStoredDayCount, t("settingsStorage.noHourData"))
      : t("settingsStorage.loadingBusy");
    const hourStorageLabel = hasHourMetadata && !hourFlashUnavailable ? formatSettingsStorageKb(energyMetadata.hourStorageKb) : "—";
    const hourWriteLabel = hasHourMetadata && !hourFlashUnavailable ? formatSettingsStorageCount(energyMetadata.hourWriteCount) : "—";
    const hourLastWriteLabel = hasHourMetadata && !hourFlashUnavailable ? formatSettingsStorageTimestamp(energyMetadata.hourLastWriteTimestampS) : t("settingsStorage.noData");

    const backButton = page === "overview" ? "" : `
      <button class="oq-settings-storage-back" type="button" data-oq-action="back-storage-overview">
        <span aria-hidden="true">←</span> ${escapeHtml(t("settingsStorage.backOverview"))}
      </button>`;
    const renderHubItem = (action, eyebrow, title, summary, status, enabled) => `
      <button class="oq-settings-storage-hub-item${enabled ? " is-on" : ""}" type="button" data-oq-action="${escapeHtml(action)}">
        <span class="oq-settings-storage-hub-copy">
          <span>${escapeHtml(eyebrow)}</span>
          <strong>${escapeHtml(title)}</strong>
          <em>${escapeHtml(summary)}</em>
        </span>
        <span class="oq-settings-storage-hub-status">
          <strong>${escapeHtml(status)}</strong>
          <span aria-hidden="true">›</span>
        </span>
      </button>`;

    const diagnosisDetails = {
      title: t("settingsStorage.diagTitle"),
      meta: t("settingsStorage.diagMeta"),
      shortLabel: t("settingsStorage.diagShort"),
      primary: getSettingsStorageStatOrFallback("trendHistoryFlashAvailable", t("settingsStorage.liveOnly")),
      note: t("settingsStorage.diagNote", { value: getSettingsStorageStatOrFallback("trendHistoryFlashNewest", t("settingsStorage.noData")) }),
      items: [
        { label: t("settingsStorage.diagPeriod"), value: getSettingsStorageStatOrFallback("trendHistoryFlashAvailable", t("settingsStorage.liveOnly")) },
        { label: t("settingsStorage.diagSpace"), value: getSettingsStorageStatOrFallback("trendHistoryFlashSize") },
        { label: t("settingsStorage.diagActions"), value: getSettingsStorageStatOrFallback("trendHistoryFlashWrites", "0") },
        { label: t("settingsStorage.diagLongestFlush"), value: getSettingsStorageStatOrFallback("trendHistoryFlashMaxFlushDuration", "0 ms") },
        { label: t("settingsStorage.diagErases"), value: getSettingsStorageStatOrFallback("trendHistoryFlashErases", "0") },
        { label: t("settingsStorage.diagLongestErase"), value: getSettingsStorageStatOrFallback("trendHistoryFlashMaxEraseDuration", "0 ms") },
        { label: t("settingsStorage.diagLongestWrite"), value: getSettingsStorageStatOrFallback("trendHistoryFlashMaxWriteDuration", "0 ms") },
        { label: t("settingsStorage.diagLongestIndex"), value: getSettingsStorageStatOrFallback("trendHistoryFlashMaxIndexUpdateDuration", "0 ms") },
        { label: t("settingsStorage.diagFlashErrors"), value: getSettingsStorageStatOrFallback("trendHistoryFlashFailures", "0") },
        { label: t("settingsStorage.diagLastStored"), value: getSettingsStorageStatOrFallback("trendHistoryFlashLastFlush", t("settingsStorage.noData")) },
      ],
    };
    const decisionDetails = {
      title: t("settingsStorage.logTitle"),
      meta: t("settingsStorage.diagMeta"),
      shortLabel: t("settingsStorage.logShort"),
      primary: decisionLogHistoryEnabled ? decisionEventsLabel : t("settingsStorage.logLive"),
      note: decisionMetadata.lastFlushEpochS ? t("settingsStorage.logLastStored", { value: formatSettingsStorageTimestamp(decisionMetadata.lastFlushEpochS) }) : t("settingsStorage.logNeverStored"),
      items: [
        { label: t("settingsStorage.logCount"), value: formatSettingsStorageCount(decisionMetadata.storedEvents) },
        { label: t("settingsStorage.logSpace"), value: formatSettingsStorageKb(Number(decisionMetadata.storageBytes || 0) / 1024) },
        { label: t("settingsStorage.logWrites"), value: formatSettingsStorageCount(decisionMetadata.writeCount) },
        { label: t("settingsStorage.logLastWrite"), value: formatSettingsStorageTimestamp(decisionMetadata.lastFlushEpochS) },
      ],
    };
    const energyDetails = [
      {
        title: t("settingsStorage.dayTitle"),
        meta: t("settingsStorage.diagMeta"),
        shortLabel: t("settingsStorage.dayShort"),
        primary: lifetimeAvailableDaysLabel,
        note: t("settingsStorage.dayRange", { oldest: getSettingsStorageStatOrFallback("lifetimeEnergyHistoryOldest", t("settingsStorage.noData")), newest: getSettingsStorageStatOrFallback("lifetimeEnergyHistoryNewest", t("settingsStorage.noData")) }),
        items: [
          { label: t("settingsStorage.dayKept"), value: lifetimeAvailableDaysLabel },
          { label: t("settingsStorage.diagSpace"), value: getSettingsStorageStatOrFallback("lifetimeEnergyHistorySize") },
          { label: t("settingsStorage.diagActions"), value: getSettingsStorageStatOrFallback("lifetimeEnergyHistoryWrites", "0") },
          { label: t("settingsStorage.diagLastStored"), value: getSettingsStorageStatOrFallback("lifetimeEnergyHistoryLastWrite", t("settingsStorage.noData")) },
        ],
      },
      hasEntity("lifetimeEnergyHourRetention") ? {
        title: t("settingsStorage.hourTitle"),
        meta: t("settingsStorage.diagMeta"),
        shortLabel: t("settingsStorage.hourShort"),
        primary: hourStoredLabel,
        note: t("settingsStorage.hourNote"),
        items: [
          { label: t("settingsStorage.dayKept"), value: hourStoredLabel },
          { label: t("settingsStorage.diagSpace"), value: hourStorageLabel },
          { label: t("settingsStorage.diagActions"), value: hourWriteLabel },
          { label: t("settingsStorage.diagLastStored"), value: hourLastWriteLabel },
        ],
      } : null,
    ];

    let title = t("settingsStorage.hubTitle");
    let copy = t("settingsStorage.hubCopy");
    let body = `
      <div class="oq-settings-storage-hub">
        ${renderHubItem("open-storage-diagnosis", t("settingsStorage.hubDiagEyebrow"), t("settingsStorage.hubDiagTitle"), t("settingsStorage.hubDiagCopy"), trendHistoryFlashEnabled ? getSettingsStorageStatOrFallback("trendHistoryFlashAvailable", t("settingsStorage.hubDiagActive")) : (trendHistoryEnabled ? t("settingsStorage.liveOnly") : t("settingsStorage.offValue")), trendHistoryEnabled)}
        ${decisionLogHistoryAvailable ? renderHubItem("open-storage-decision-log", t("settingsStorage.hubLogEyebrow"), t("settingsStorage.hubLogTitle"), t("settingsStorage.hubLogCopy"), decisionLogHistoryEnabled ? t("settingsStorage.hubLogEnabled", { events: decisionEventsLabel }) : t("settingsStorage.logLive"), decisionLogHistoryEnabled) : ""}
        ${lifetimeEnergyHistoryAvailable ? renderHubItem("open-storage-energy", t("settingsStorage.hubEnergyEyebrow"), t("settingsStorage.hubEnergyTitle"), t("settingsStorage.hubEnergyCopy"), lifetimeEnergyHistoryEnabled ? lifetimeAvailableDaysLabel : t("settingsStorage.offValue"), lifetimeEnergyHistoryEnabled) : ""}
      </div>
      <p class="oq-settings-storage-footnote"><strong>${escapeHtml(t("settingsStorage.hubFootnote"))}</strong> ${escapeHtml(t("settingsStorage.hubFootnoteCopy"))}</p>`;

    if (page === "diagnosis") {
      title = t("settingsStorage.pageDiagnosisTitle");
      copy = t("settingsStorage.pageDiagnosisCopy");
      body = `${backButton}<section class="oq-settings-storage-domain oq-settings-storage-domain--single">
        <div class="oq-settings-storage-domain-rows">
          ${renderSettingsStorageSwitchRow("trendHistoryEnabled", t("settingsStorage.rowRecentData"), t("settingsStorage.rowRecentDataCopy"), t("settingsStorage.rowRecentOn"), t("settingsStorage.rowRecentOff"), t("settingsStorage.rowRecentMeta"))}
          ${renderSettingsStorageSwitchRow("trendHistoryFlashEnabled", t("settingsStorage.rowFlashData"), t("settingsStorage.rowFlashDataCopy"), t("settingsStorage.rowFlashOn"), t("settingsStorage.rowFlashOff"), t("settingsStorage.rowFlashMeta"))}
          ${canFlushTrend ? `<div class="oq-settings-storage-inline-action"><div><h4>${escapeHtml(t("settingsStorage.flushNowTitle"))}</h4><p>${escapeHtml(t("settingsStorage.flushNowCopy"))}</p></div>${renderSettingsStorageActionButton("trendHistoryFlush", t("settingsStorage.flushNowAction"), "flush-trend-history", { disabled: !trendHistoryFlashEnabled, busyLabel: t("settingsStorage.flushingBusy") })}</div>` : ""}
        </div>
      </section>${renderSettingsStorageTechnicalDetails([diagnosisDetails])}`;
    } else if (page === "decision-log") {
      title = t("settingsStorage.pageLogTitle");
      copy = t("settingsStorage.pageLogCopy");
      body = `${backButton}<section class="oq-settings-storage-domain oq-settings-storage-domain--single">
        <div class="oq-settings-storage-domain-rows">
          ${renderSettingsStorageSwitchRow("decisionLogHistoryEnabled", t("settingsStorage.rowLogKeep"), t("settingsStorage.rowLogKeepCopy"), t("settingsStorage.rowLogOn"), t("settingsStorage.rowLogOff"), t("settingsStorage.rowFlashMeta"))}
          ${canFlushDecision ? `<div class="oq-settings-storage-inline-action"><div><h4>${escapeHtml(t("settingsStorage.flushLogTitle"))}</h4><p>${escapeHtml(t("settingsStorage.flushLogCopy"))}</p></div>${renderSettingsStorageActionButton("decisionLogHistoryFlush", t("settingsStorage.flushNowAction"), "flush-decision-log-history", { disabled: !decisionLogHistoryEnabled, busyLabel: t("settingsStorage.flushingBusy") })}</div>` : ""}
        </div>
      </section>${renderSettingsStorageTechnicalDetails([decisionDetails])}
      ${hasEntity("decisionLogHistoryClear") ? `<details class="oq-settings-storage-advanced"${state.settingsStorageAdvancedOpen ? " open" : ""}><summary data-oq-action="toggle-storage-advanced">${escapeHtml(t("settingsStorage.advancedTitle"))}</summary><div class="oq-settings-storage-inline-action oq-settings-storage-inline-action--danger"><div><h4>${escapeHtml(t("settingsStorage.clearLogTitle"))}</h4><p>${escapeHtml(t("settingsStorage.clearLogCopy"))}</p></div>${renderSettingsStorageActionButton("decisionLogHistoryClear", t("settingsStorage.clearHistoryAction"), "clear-decision-log-history", { disabled: Number(decisionMetadata.storedEvents || 0) <= 0, buttonClass: "oq-helper-button oq-helper-button--warning", busyLabel: t("settingsStorage.clearingBusy") })}</div></details>` : ""}`;
    } else if (page === "energy") {
      title = t("settingsStorage.pageEnergyTitle");
      copy = t("settingsStorage.pageEnergyCopy");
      body = `${backButton}<section class="oq-settings-storage-domain oq-settings-storage-domain--single">
        <div class="oq-settings-storage-domain-rows">
          ${renderSettingsStorageSwitchRow("lifetimeEnergyHistoryEnabled", t("settingsStorage.rowDayKeep"), t("settingsStorage.rowDayKeepCopy"), t("settingsStorage.rowDayOn"), t("settingsStorage.rowDayOff"), t("settingsStorage.rowFlashMeta"))}
          ${renderSettingsStorageSelectRow("lifetimeEnergyHourRetention", t("settingsStorage.rowHourKeep"), t("settingsStorage.rowHourKeepCopy"), t("settingsStorage.rowHourMeta"))}
          ${canCaptureLifetime ? `<div class="oq-settings-storage-inline-action"><div><h4>${escapeHtml(t("settingsStorage.captureTodayTitle"))}</h4><p>${escapeHtml(t("settingsStorage.captureTodayCopy"))}</p></div>${renderSettingsStorageActionButton("lifetimeEnergyHistoryCapture", t("settingsStorage.captureTodayAction"), "save-lifetime-energy-history", { disabled: !lifetimeEnergyHistoryEnabled, busyLabel: t("settingsStorage.flushingBusy") })}</div>` : ""}
        </div>
      </section>${renderSettingsStorageTechnicalDetails(energyDetails)}
      <details class="oq-settings-storage-advanced"${state.settingsStorageAdvancedOpen ? " open" : ""}><summary data-oq-action="toggle-storage-advanced">${escapeHtml(t("settingsStorage.advancedTitle"))}</summary><div class="oq-settings-storage-advanced-body">${renderSettingsEnergyHistoryExportPanel()}${renderSettingsEnergyHistoryImportPanel()}${hasEntity("lifetimeEnergyHistoryClear") ? `<div class="oq-settings-storage-inline-action oq-settings-storage-inline-action--danger"><div><h4>${escapeHtml(t("settingsStorage.clearEnergyTitle"))}</h4><p>${escapeHtml(t("settingsStorage.clearEnergyCopy"))}</p></div>${renderSettingsStorageActionButton("lifetimeEnergyHistoryClear", t("settingsStorage.clearHistoryAction"), "clear-lifetime-energy-history", { disabled: !canClearLifetime, buttonClass: "oq-helper-button oq-helper-button--warning", busyLabel: t("settingsStorage.clearingBusy") })}</div>` : ""}</div></details>`;
    }

    return renderModalShell({
      id: "system",
      titleId: "oq-history-storage-modal-title",
      kicker: page === "overview" ? t("settingsStorage.modalKicker") : t("settingsStorage.modalKickerSub"),
      title,
      copy,
      className: "oq-helper-modal--scrollable oq-settings-storage-modal",
      sectionAttributes: "data-oq-history-storage-scroller",
      closeAction: "close-system-modal",
      closeLabel: t("settingsStorage.modalClose"),
      body,
      actions: `<button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-system-modal">${escapeHtml(t("settingsStorage.modalDone"))}</button>`,
    });
  }

  export function renderSettingsBackupSection() {
    const busy = state.settingsBackupBusy;
    const totalFields = SETTINGS_BACKUP_KEYS.length;
    const sectionCount = SETTINGS_BACKUP_SECTIONS.length;

    return renderSettingsSection(
      t("settingsStorage.backupGroup"),
      t("settingsStorage.backupTitle"),
      t("settingsStorage.backupCopy"),
      `
        <div class="oq-settings-backup-shell">
          <div class="oq-settings-backup-summary">
            <div class="oq-settings-backup-stat">
              <span class="oq-settings-backup-stat-label">${escapeHtml(t("settingsStorage.backupStatSettings"))}</span>
              <strong class="oq-settings-backup-stat-value">${escapeHtml(String(totalFields))}</strong>
            </div>
            <div class="oq-settings-backup-stat">
              <span class="oq-settings-backup-stat-label">${escapeHtml(t("settingsStorage.backupStatSections"))}</span>
              <strong class="oq-settings-backup-stat-value">${escapeHtml(String(sectionCount))}</strong>
            </div>
            <div class="oq-settings-backup-stat">
              <span class="oq-settings-backup-stat-label">${escapeHtml(t("settingsStorage.backupStatMqtt"))}</span>
              <strong class="oq-settings-backup-stat-value">${escapeHtml(t("settingsStorage.backupStatMqttValue"))}</strong>
            </div>
          </div>
          <div class="oq-settings-backup-actions">
            <button
              class="oq-helper-button oq-helper-button--primary"
              type="button"
              data-oq-action="download-settings-backup"
              ${busy ? "disabled" : ""}
            >
              ${busy ? escapeHtml(t("settingsStorage.backupDownloading")) : escapeHtml(t("settingsStorage.backupDownload"))}
            </button>
            <button
              class="oq-helper-button oq-helper-button--ghost"
              type="button"
              data-oq-action="open-settings-backup-import"
              ${busy ? "disabled" : ""}
            >
              ${escapeHtml(t("settingsStorage.backupRestore"))}
            </button>
          </div>
          <p class="oq-settings-action-note">${escapeHtml(t("settingsStorage.backupNote"))}</p>
          ${state.settingsBackupError ? `<p class="oq-settings-backup-error">${escapeHtml(state.settingsBackupError)}</p>` : ""}
        </div>
      `,
    );
  }

  export function renderSettingsBackupImportModal() {
    const busy = state.settingsBackupBusy;
    return renderModalShell({
      id: "system",
      titleId: "oq-backup-import-modal-title",
      kicker: t("settingsStorage.restoreImportKicker"),
      title: t("settingsStorage.restoreImportTitle"),
      copy: t("settingsStorage.restoreImportCopy"),
      className: "oq-helper-modal--wide",
      closeAction: "close-system-modal",
      closeLabel: t("settingsStorage.restoreImportClose"),
      body: `
          <div class="oq-helper-modal-row">
            <span class="oq-helper-modal-label">${escapeHtml(t("settingsStorage.restoreImportFileLabel"))}</span>
            <input
              class="oq-settings-backup-input oq-settings-backup-import-input"
              type="file"
              accept=".json,application/json"
              data-oq-backup-file-input="true"
              ${busy ? "disabled" : ""}
            >
            <span class="oq-helper-modal-subvalue">${escapeHtml(t("settingsStorage.restoreImportFileHint"))}</span>
          </div>
          ${state.settingsBackupError ? `<p class="oq-settings-backup-error">${escapeHtml(state.settingsBackupError)}</p>` : ""}`,
      actions: `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${busy ? "disabled" : ""}>${escapeHtml(t("settingsStorage.restoreCancel"))}</button>`,
    });
  }

  export function renderSettingsBackupRestoreModal() {
    const draft = state.settingsBackupDraft;
    if (!draft) {
      return "";
    }

    const summary = draft.summary || getSettingsBackupSelectionSummary(draft);
    const sourceInstallation = String(draft.source?.installation || draft.source?.device || t("common.unknown"));
    const currentInstallation = getInstallationLabel();
    const sourceVersion = String(draft.source?.firmware_version || t("common.unknown"));
    const sourceChannel = String(draft.source?.firmware_channel || "").trim() || t("common.unknown");
    const sourceTopology = String(draft.source?.topology || "").trim() || t("common.unknown");
    const currentVersion = getFirmwareCurrentVersion();
    const currentTopology = typeof getInstallationTopology === "function"
      ? getInstallationTopology()
      : "";
    const topologyMismatch = sourceTopology !== t("common.unknown") && currentTopology && sourceTopology !== currentTopology;
    const installationMismatch = sourceInstallation !== t("common.unknown") && sourceInstallation !== currentInstallation;
    const mqtt = draft.mqtt;
    const mqttNeedsPassword = settingsBackupMqttNeedsPassword(mqtt);
    const mqttPasswordMissing = mqttNeedsPassword && !String(state.settingsBackupMqttPassword || "");
    const mqttValue = mqtt ? (mqtt.enabled ? t("settingsStorage.restoreMqttOn") : t("settingsStorage.restoreMqttOff")) : t("settingsStorage.restoreMqttMissing");
    const mqttMeta = mqtt
      ? t("settingsStorage.restoreMqttMeta", { broker: mqtt.broker || t("settingsStorage.restoreMqttNoBroker"), port: mqtt.port, pass: mqtt.password_was_set ? t("settingsStorage.restoreMqttPassSet") : t("settingsStorage.restoreMqttPassUnset") })
      : t("settingsStorage.restoreMqttMetaMissing");
    const warningText = topologyMismatch || installationMismatch
      ? t("settingsStorage.restoreWarnMismatch")
      : summary.requiredMissing
        ? t("settingsStorage.restoreWarnMissing")
        : t("settingsStorage.restoreWarnSkip");
    const electricalRestoreWarning = hasEntity("electricalCurrentLimit")
      ? getElectricalLimitBackupRestoreWarning(draft.settings)
      : "";

    return renderModalShell({
      id: "system",
      titleId: "oq-backup-modal-title",
      kicker: t("settingsStorage.restoreKicker"),
      title: t("settingsStorage.restoreTitle"),
      copy: t("settingsStorage.restoreCopy"),
      className: "oq-helper-modal--wide oq-helper-modal--scrollable",
      sectionAttributes: "data-oq-settings-backup-restore-scroller",
      closeAction: "close-system-modal",
      closeLabel: t("settingsStorage.restoreClose"),
      body: `
          <div class="oq-helper-modal-grid oq-settings-backup-modal-grid">
            <div class="oq-helper-modal-row">
              <span class="oq-helper-modal-label">${escapeHtml(t("settingsStorage.restoreFrom"))}</span>
              <strong class="oq-helper-modal-value">${escapeHtml(sourceInstallation)}</strong>
              <span class="oq-helper-modal-subvalue">${escapeHtml(t("settingsStorage.restoreTopo", { topo: sourceTopology, version: sourceVersion }))}</span>
            </div>
            <div class="oq-helper-modal-row">
              <span class="oq-helper-modal-label">${escapeHtml(t("settingsStorage.restoreCurrent"))}</span>
              <strong class="oq-helper-modal-value">${escapeHtml(currentInstallation)}</strong>
              <span class="oq-helper-modal-subvalue">${escapeHtml(t("settingsStorage.restoreCurrentSub", { topo: currentTopology, version: currentVersion || t("common.unknown") }))}</span>
            </div>
            <div class="oq-helper-modal-row">
              <span class="oq-helper-modal-label">${escapeHtml(t("settingsStorage.restoreChannel"))}</span>
              <strong class="oq-helper-modal-value">${escapeHtml(sourceChannel)}</strong>
              <span class="oq-helper-modal-subvalue">${escapeHtml(t("settingsStorage.restoreSchema", { version: String(draft.schema_version || 1) }))}</span>
            </div>
            <div class="oq-helper-modal-row">
              <span class="oq-helper-modal-label">${escapeHtml(t("settingsStorage.restoreSettings"))}</span>
              <strong class="oq-helper-modal-value">${escapeHtml(t("settingsStorage.restoreSettingsValue", { total: formatNumber(summary.total, { maximumFractionDigits: 0 }) }))}</strong>
              <span class="oq-helper-modal-subvalue">${escapeHtml(summary.differenceCount ? t("settingsStorage.restoreSettingsDiff", { diff: formatNumber(summary.differenceCount, { maximumFractionDigits: 0 }), unit: summary.differenceCount === 1 ? t("settingsStorage.restoreDiffOne") : t("settingsStorage.restoreDiffOther"), present: formatNumber(summary.currentPresent, { maximumFractionDigits: 0 }), unknown: formatNumber(summary.unknown, { maximumFractionDigits: 0 }) }) : t("settingsStorage.restoreSettingsMatch", { present: formatNumber(summary.currentPresent, { maximumFractionDigits: 0 }), unknown: formatNumber(summary.unknown, { maximumFractionDigits: 0 }) }))}</span>
            </div>
            <div class="oq-helper-modal-row">
              <span class="oq-helper-modal-label">${escapeHtml(t("settingsStorage.restoreMqtt"))}</span>
              <strong class="oq-helper-modal-value">${escapeHtml(mqttValue)}</strong>
              <span class="oq-helper-modal-subvalue">${escapeHtml(mqttMeta)}</span>
            </div>
          </div>
          ${mqttNeedsPassword ? `
            <label class="oq-settings-backup-mqtt-password">
              <span class="oq-helper-modal-label">${escapeHtml(t("settingsStorage.restoreMqttPassLabel"))}</span>
              <input
                class="oq-helper-input"
                type="password"
                autocomplete="current-password"
                data-oq-backup-mqtt-password="true"
                placeholder="${escapeHtml(t("settingsStorage.restoreMqttPassPlaceholder"))}"
                ${state.settingsBackupBusy ? "disabled" : ""}
              >
              <span class="oq-helper-modal-subvalue">${escapeHtml(t("settingsStorage.restoreMqttPassHint"))}</span>
            </label>
          ` : ""}
          <div class="oq-settings-backup-modal-sections">
            ${summary.sectionSummaries.map((section) => `
              <details class="oq-settings-backup-modal-section">
                <summary class="oq-settings-backup-modal-section-head">
                  <span class="oq-settings-backup-modal-section-head-copy">
                    <strong>${escapeHtml(section.label)}</strong>
                    <em>${escapeHtml(t("settingsStorage.restoreSectionSettings", { total: formatNumber(section.total, { maximumFractionDigits: 0 }), unit: section.total === 1 ? t("settingsStorage.restoreSectionOne") : t("settingsStorage.restoreSectionOther"), diff: section.differenceCount ? t("settingsStorage.restoreSectionDiff", { diff: formatNumber(section.differenceCount, { maximumFractionDigits: 0 }), unit: section.differenceCount === 1 ? t("settingsStorage.restoreDiffOne") : t("settingsStorage.restoreDiffOther") }) : t("settingsStorage.restoreSectionMatch") }))}</em>
                  </span>
                </summary>
                <div class="oq-settings-backup-modal-section-body">
                  <p>${escapeHtml(section.differenceCount ? (section.differenceCount === 1 ? t("settingsStorage.restoreSectionDiffOneCopy", { count: formatNumber(section.differenceCount, { maximumFractionDigits: 0 }) }) : t("settingsStorage.restoreSectionDiffOtherCopy", { count: formatNumber(section.differenceCount, { maximumFractionDigits: 0 }) })) : t("settingsStorage.restoreSectionMatchCopy"))}</p>
                  <div class="oq-settings-backup-compare-list">
                    ${section.rows.map((row) => `
                      <div class="oq-settings-backup-compare oq-settings-backup-compare--${escapeHtml(row.status)}">
                        <div class="oq-settings-backup-compare-head">
                          <strong>${escapeHtml(row.label)}</strong>
                          <span>${escapeHtml(row.statusLabel)}</span>
                        </div>
                        <div class="oq-settings-backup-compare-values">
                          <div class="oq-settings-backup-compare-value" data-change="${escapeHtml(row.status)}">
                            <span>${escapeHtml(t("settingsStorage.restoreCompareBackup"))}</span>
                            <strong>${escapeHtml(row.backupDisplay)}</strong>
                          </div>
                          <div class="oq-settings-backup-compare-value" data-change="${escapeHtml(row.status)}">
                            <span>${escapeHtml(t("settingsStorage.restoreCompareNow"))}</span>
                            <strong>${escapeHtml(row.currentDisplay)}</strong>
                          </div>
                        </div>
                      </div>
                    `).join("")}
                  </div>
                </div>
              </details>
            `).join("")}
          </div>
          <p class="oq-settings-action-note${summary.unknown || summary.requiredMissing || installationMismatch ? " oq-settings-action-note--warning" : ""}">${escapeHtml(warningText)}</p>
          ${electricalRestoreWarning ? `<p class="oq-settings-action-note oq-settings-action-note--warning" role="alert">${escapeHtml(electricalRestoreWarning)}</p>` : ""}
          ${state.settingsBackupError ? `<p class="oq-settings-backup-error">${escapeHtml(state.settingsBackupError)}</p>` : ""}`,
      actions: `
        <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${state.settingsBackupBusy ? "disabled" : ""}>${escapeHtml(t("settingsStorage.restoreCancel"))}</button>
        <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="confirm-settings-backup-restore" ${state.settingsBackupBusy || mqttPasswordMissing ? "disabled" : ""}>${state.settingsBackupBusy ? escapeHtml(t("settingsStorage.restoreBusy")) : mqttPasswordMissing ? escapeHtml(t("settingsStorage.restoreNeedPass")) : escapeHtml(t("settingsStorage.restoreConfirm"))}</button>
      `,
    });
  }
