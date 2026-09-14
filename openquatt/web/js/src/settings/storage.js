import { hasEntity, isEntityActive } from "../core/app-shared.js";
import { ENTITY_DEFS, SETTINGS_BACKUP_KEYS, SETTINGS_BACKUP_SECTIONS } from "../core/config.js";
import { parseEnergyHistoryDateKey, parseEnergyHistoryMetadata } from "../core/energy-history-domain.js";
import { getEntityValue } from "../core/entity-store.js";
import { settingsBackupMqttNeedsPassword } from "../core/settings-backup-domain.js";
import { state } from "../core/state.js";
import { getInstallationLabel, getInstallationTopology } from "../features/device-context.js";
import { getFirmwareCurrentVersion } from "../features/firmware-update.js";
import { ENERGY_HISTORY_EXPORT_MODES, getSettingsBackupSelectionSummary, normalizeEnergyHistoryExportMode } from "../features/storage-history.js";
import { getSettingsStatValue, renderSettingsCompactSwitchControl, renderSettingsFieldCard, renderSettingsSection, renderSettingsSelectControl, renderSettingsSwitchCopy } from "./controls.js";
import { getSettingsSelectModel } from "./field-models.js";
import { getElectricalLimitBackupRestoreWarning } from "./electrical-limit.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";

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
    return `${match[1]} ${match[1] === "1" ? "dag" : "dagen"}`;
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
    return error ? "Niet geladen" : "Laden...";
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
      return trendMetadata.available || "Alleen live";
    }
    if (key === "trendHistoryFlashOldest") {
      if (!hasSettingsTrendHistoryMetadata()) {
        return getSettingsStorageLoadingLabel(state.trendHistoryMetadataError);
      }
      return trendMetadata.oldest || "Geen data";
    }
    if (key === "trendHistoryFlashNewest") {
      if (!hasSettingsTrendHistoryMetadata()) {
        return getSettingsStorageLoadingLabel(state.trendHistoryMetadataError);
      }
      return trendMetadata.newest || "Geen data";
    }
    if (key === "trendHistoryFlashLastFlush") {
      if (!hasSettingsTrendHistoryMetadata()) {
        return getSettingsStorageLoadingLabel(state.trendHistoryMetadataError);
      }
      return trendMetadata.lastFlush || "Geen data";
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
        return "Niet beschikbaar";
      }
      return formatSettingsStorageDayCount(energyMetadata.storedDayCount, "Geen data");
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

  export function formatSettingsStorageDayCount(value, fallback = "Geen data") {
    const count = Number(value);
    if (!Number.isFinite(count) || count <= 0) {
      return fallback;
    }
    return `${Math.round(count)} ${Math.round(count) === 1 ? "dag" : "dagen"}`;
  }

  export function formatSettingsStorageEventCount(value, fallback = "Nog geen historie") {
    const count = Math.max(0, Math.round(Number(value) || 0));
    if (count <= 0) {
      return fallback;
    }
    return `${count} ${count === 1 ? "gebeurtenis" : "gebeurtenissen"}`;
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
      return "Geen data";
    }
    return parsed.date.toLocaleDateString("nl-NL", { day: "2-digit", month: "2-digit", year: "numeric" });
  }

  export function formatSettingsStorageTimestamp(seconds, fallback = "Geen data") {
    const timestamp = Number(seconds);
    if (!Number.isFinite(timestamp) || timestamp <= 0) {
      return fallback;
    }
    const date = new Date(timestamp * 1000);
    const day = date.toLocaleDateString("nl-NL", { day: "2-digit", month: "2-digit" });
    const time = date.toLocaleTimeString("nl-NL", { hour: "2-digit", minute: "2-digit" });
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
            <strong>Opslagdetails</strong>
            <em>Bewaartermijn, ruimte en opslagmomenten</em>
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
    const decisionLogMetadata = getSettingsDecisionLogStorageMetadata();
    const trendAvailableValue = trendHistoryFlashEnabled
      ? getSettingsStorageStatOrFallback("trendHistoryFlashAvailable", "Alleen live")
      : "Alleen live";
    const lifetimeAvailableValue = lifetimeEnergyHistoryAvailable
      ? formatSettingsStoredDaysLabel(getSettingsStorageStatOrFallback("lifetimeEnergyHistoryAvailable", "Geen data"))
      : "Geen data";
    return renderSettingsSection(
      "Diagnose",
      "Gegevens bewaren",
      "Bepaal welke gegevens OpenQuatt bewaart voor grafieken, resultaten en hulp bij problemen.",
      `
        <article class="oq-settings-storage-summary">
          <div class="oq-settings-storage-summary-copy">
            <h3>Wat wordt bewaard?</h3>
            <p>Kies welke gegevens tijdelijk beschikbaar blijven en wat in permanent geheugen wordt bewaard.</p>
          </div>
          <div class="oq-settings-storage-summary-metrics" aria-label="Opslagstatus">
            ${hasEntity("trendHistoryEnabled") ? renderSettingsStorageSummaryMetric("Diagnose", trendHistoryFlashEnabled ? trendAvailableValue : (trendHistoryEnabled ? "Alleen live" : "Uit"), trendHistoryFlashEnabled ? "Blijft bewaard na herstart" : "Tijdelijk", trendHistoryEnabled) : ""}
            ${decisionLogHistoryAvailable ? renderSettingsStorageSummaryMetric("Beslislog", decisionLogHistoryEnabled ? formatSettingsStorageEventCount(decisionLogMetadata.storedEvents) : "Alleen sinds herstart", decisionLogHistoryEnabled ? "Maximaal 7 dagen" : "Tijdelijk", decisionLogHistoryEnabled) : ""}
            ${lifetimeEnergyHistoryAvailable ? renderSettingsStorageSummaryMetric("Energie", lifetimeAvailableValue, lifetimeEnergyHistoryEnabled ? "Blijft bewaard na herstart" : "Uit", lifetimeEnergyHistoryEnabled) : ""}
          </div>
          <button class="oq-helper-button oq-helper-button--ghost oq-settings-storage-summary-action" type="button" data-oq-action="open-history-storage-modal">
            Beheren
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
      recordParts.push(`${dailyCount} dagrecords`);
    }
    if (hourDayCount > 0) {
      recordParts.push(`${hourDayCount} uurdagen`);
    }
    if (state.energyHistoryImportRange) {
      recordParts.push(state.energyHistoryImportRange);
    }
    if (state.energyHistoryImportSource) {
      recordParts.push(state.energyHistoryImportSource);
    }
    if (state.energyHistoryImportInvalidCount > 0) {
      recordParts.push(`${state.energyHistoryImportInvalidCount} regels niet gebruikt`);
    }

    const hasFile = Boolean(state.energyHistoryImportFileName);
    const hasRecords = dailyCount > 0 || hourDayCount > 0;
    const progress = Number(state.energyHistoryImportProgressPercent || 0);
    const importLabel = state.energyHistoryImportBusy
      ? `Importeren...${progress > 0 ? ` (${progress}%)` : ""}`
      : "Importeren";

    return `
      <div class="oq-settings-storage-import">
        <div class="oq-settings-storage-import-head">
          <div>
            <h4>Historie importeren</h4>
            <p>Vul ontbrekende dagtotalen en uurdetail aan vanuit een OpenQuatt- of Quatt-exportbestand.</p>
          </div>
          ${!hasFile ? `
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="select-energy-history-import-file">
              Bestand kiezen
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
                Wissen
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
        ${escapeHtml(option.label)}
      </option>
    `).join("");
    const exportLabel = state.energyHistoryExportBusy ? "Exporteren..." : "Exporteren";

    return `
      <div class="oq-settings-storage-import oq-settings-storage-export">
        <div class="oq-settings-storage-import-head">
          <div>
            <h4>Historie exporteren</h4>
            <p>Download bewaarde energiegegevens om ze later op een andere OpenQuatt te importeren.</p>
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
      ? getSettingsStorageStatOrFallback("lifetimeEnergyHistoryAvailable", "Geen data")
      : "Geen data";
    const lifetimeAvailableDaysLabel = formatSettingsStoredDaysLabel(lifetimeAvailableLabel);
    const canClearLifetime = hasEntity("lifetimeEnergyHistoryClear") && !["Geen data", "—"].includes(lifetimeAvailableLabel);
    const canFlushTrend = trendHistoryEnabled && hasEntity("trendHistoryFlush");
    const canFlushDecision = decisionLogHistoryEnabled && hasEntity("decisionLogHistoryFlush");
    const canCaptureLifetime = hasEntity("lifetimeEnergyHistoryCapture");
    const energyMetadata = getSettingsEnergyHistoryMetadata();
    const hasHourMetadata = String(state.energyHistoryRaw || "").includes("@hour_retention|");
    const hourFlashUnavailable = hasHourMetadata && !energyMetadata.hourPartitionAvailable;
    const hourStoredLabel = hasHourMetadata
      ? hourFlashUnavailable ? "Alleen live" : formatSettingsStorageDayCount(energyMetadata.hourStoredDayCount, "Geen uurdata")
      : "Laden...";
    const hourStorageLabel = hasHourMetadata && !hourFlashUnavailable ? formatSettingsStorageKb(energyMetadata.hourStorageKb) : "—";
    const hourWriteLabel = hasHourMetadata && !hourFlashUnavailable ? formatSettingsStorageCount(energyMetadata.hourWriteCount) : "—";
    const hourLastWriteLabel = hasHourMetadata && !hourFlashUnavailable ? formatSettingsStorageTimestamp(energyMetadata.hourLastWriteTimestampS) : "Geen data";

    const backButton = page === "overview" ? "" : `
      <button class="oq-settings-storage-back" type="button" data-oq-action="back-storage-overview">
        <span aria-hidden="true">←</span> Opslagoverzicht
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
      title: "Diagnosegeschiedenis",
      meta: "Technische details",
      shortLabel: "Diagnose",
      primary: getSettingsStorageStatOrFallback("trendHistoryFlashAvailable", "Alleen live"),
      note: `Laatste meting: ${getSettingsStorageStatOrFallback("trendHistoryFlashNewest", "Geen data")}`,
      items: [
        { label: "Bewaarperiode", value: getSettingsStorageStatOrFallback("trendHistoryFlashAvailable", "Alleen live") },
        { label: "Opslagruimte", value: getSettingsStorageStatOrFallback("trendHistoryFlashSize") },
        { label: "Opslagacties", value: getSettingsStorageStatOrFallback("trendHistoryFlashWrites", "0") },
        { label: "Langste volledige opslagactie", value: getSettingsStorageStatOrFallback("trendHistoryFlashMaxFlushDuration", "0 ms") },
        { label: "Sector-erases sinds start", value: getSettingsStorageStatOrFallback("trendHistoryFlashErases", "0") },
        { label: "Langste sector-erase", value: getSettingsStorageStatOrFallback("trendHistoryFlashMaxEraseDuration", "0 ms") },
        { label: "Langste flashwrite", value: getSettingsStorageStatOrFallback("trendHistoryFlashMaxWriteDuration", "0 ms") },
        { label: "Langste index-update", value: getSettingsStorageStatOrFallback("trendHistoryFlashMaxIndexUpdateDuration", "0 ms") },
        { label: "Flashfouten sinds start", value: getSettingsStorageStatOrFallback("trendHistoryFlashFailures", "0") },
        { label: "Laatst opgeslagen", value: getSettingsStorageStatOrFallback("trendHistoryFlashLastFlush", "Geen data") },
      ],
    };
    const decisionDetails = {
      title: "Beslisloghistorie",
      meta: "Technische details",
      shortLabel: "Beslislog",
      primary: decisionLogHistoryEnabled ? decisionEventsLabel : "Alleen sinds herstart",
      note: decisionMetadata.lastFlushEpochS ? `Laatst opgeslagen: ${formatSettingsStorageTimestamp(decisionMetadata.lastFlushEpochS)}` : "Nog niet opgeslagen",
      items: [
        { label: "Aantal", value: formatSettingsStorageCount(decisionMetadata.storedEvents) },
        { label: "Ruimte", value: formatSettingsStorageKb(Number(decisionMetadata.storageBytes || 0) / 1024) },
        { label: "Schrijfacties", value: formatSettingsStorageCount(decisionMetadata.writeCount) },
        { label: "Laatste opslag", value: formatSettingsStorageTimestamp(decisionMetadata.lastFlushEpochS) },
      ],
    };
    const energyDetails = [
      {
        title: "Dagtotalen",
        meta: "Technische details",
        shortLabel: "Dag",
        primary: lifetimeAvailableDaysLabel,
        note: `${getSettingsStorageStatOrFallback("lifetimeEnergyHistoryOldest", "Geen data")} t/m ${getSettingsStorageStatOrFallback("lifetimeEnergyHistoryNewest", "Geen data")}`,
        items: [
          { label: "Dagen bewaard", value: lifetimeAvailableDaysLabel },
          { label: "Opslagruimte", value: getSettingsStorageStatOrFallback("lifetimeEnergyHistorySize") },
          { label: "Opslagacties", value: getSettingsStorageStatOrFallback("lifetimeEnergyHistoryWrites", "0") },
          { label: "Laatst opgeslagen", value: getSettingsStorageStatOrFallback("lifetimeEnergyHistoryLastWrite", "Geen data") },
        ],
      },
      hasEntity("lifetimeEnergyHourRetention") ? {
        title: "Uurdetail",
        meta: "Technische details",
        shortLabel: "Uur",
        primary: hourStoredLabel,
        note: "Detail voor de daggrafiek",
        items: [
          { label: "Dagen bewaard", value: hourStoredLabel },
          { label: "Opslagruimte", value: hourStorageLabel },
          { label: "Opslagacties", value: hourWriteLabel },
          { label: "Laatst opgeslagen", value: hourLastWriteLabel },
        ],
      } : null,
    ];

    let title = "Gegevens bewaren";
    let copy = "Kies welk soort historie je wilt bekijken of aanpassen. Dit verandert niets aan de aansturing van je warmtepomp.";
    let body = `
      <div class="oq-settings-storage-hub">
        ${renderHubItem("open-storage-diagnosis", "Diagnose", "Technische meetgegevens", "Temperaturen, doorstroming en vermogen voor grafieken en support.", trendHistoryFlashEnabled ? getSettingsStorageStatOrFallback("trendHistoryFlashAvailable", "Historie actief") : (trendHistoryEnabled ? "Alleen live" : "Uit"), trendHistoryEnabled)}
        ${decisionLogHistoryAvailable ? renderHubItem("open-storage-decision-log", "Beslislog", "Keuzes van de controller", "Exacte momenten, redenen, bronwissels en bescherming.", decisionLogHistoryEnabled ? `${decisionEventsLabel} · max. 7 dagen` : "Alleen sinds herstart", decisionLogHistoryEnabled) : ""}
        ${lifetimeEnergyHistoryAvailable ? renderHubItem("open-storage-energy", "Resultaten", "Energiehistorie", "Dagtotalen en uurdetail voor opbrengst, verbruik en rendement.", lifetimeEnergyHistoryEnabled ? lifetimeAvailableDaysLabel : "Uit", lifetimeEnergyHistoryEnabled) : ""}
      </div>
      <p class="oq-settings-storage-footnote"><strong>Goed om te weten:</strong> gegevens die worden bewaard, blijven beschikbaar na een herstart. Tijdelijke gegevens bestaan alleen zolang de controller online is.</p>`;

    if (page === "diagnosis") {
      title = "Diagnosegegevens";
      copy = "Beheer technische meetreeksen voor diagnosegrafieken en hulp bij problemen.";
      body = `${backButton}<section class="oq-settings-storage-domain oq-settings-storage-domain--single">
        <div class="oq-settings-storage-domain-rows">
          ${renderSettingsStorageSwitchRow("trendHistoryEnabled", "Recente diagnosegegevens", "Bewaar de laatste meetpunten zolang de controller online is.", "Deze gegevens zijn tijdelijk en verdwijnen na een herstart.", "Nieuwe tijdelijke diagnosegegevens worden niet bijgehouden.", "Tijdelijk")}
          ${renderSettingsStorageSwitchRow("trendHistoryFlashEnabled", "Diagnosegeschiedenis bewaren", "Bewaar recente diagnosegegevens ook na een herstart of update.", "OpenQuatt slaat ongeveer ieder uur een blok op.", "Bestaande geschiedenis blijft staan.", "Blijft bewaard na herstart")}
          ${canFlushTrend ? `<div class="oq-settings-storage-inline-action"><div><h4>Diagnose nu opslaan</h4><p>Maak vóór een update of herstart een extra opslagmoment.</p></div>${renderSettingsStorageActionButton("trendHistoryFlush", "Nu opslaan", "flush-trend-history", { disabled: !trendHistoryFlashEnabled, busyLabel: "Opslaan..." })}</div>` : ""}
        </div>
      </section>${renderSettingsStorageTechnicalDetails([diagnosisDetails])}`;
    } else if (page === "decision-log") {
      title = "Beslisloghistorie";
      copy = "Bewaar exacte controllerkeuzes en gebeurtenissen, maximaal zeven dagen.";
      body = `${backButton}<section class="oq-settings-storage-domain oq-settings-storage-domain--single">
        <div class="oq-settings-storage-domain-rows">
          ${renderSettingsStorageSwitchRow("decisionLogHistoryEnabled", "Beslisloghistorie bewaren", "Bewaar exacte momenten en redenen uit de beslislog.", "De laatste zeven dagen blijven beschikbaar na een herstart of update.", "De actuele beslislog blijft tijdelijk beschikbaar; bestaande historie blijft staan.", "Blijft bewaard na herstart")}
          ${canFlushDecision ? `<div class="oq-settings-storage-inline-action"><div><h4>Beslislog nu opslaan</h4><p>Sla nieuwe gebeurtenissen alvast op vóór een update of herstart.</p></div>${renderSettingsStorageActionButton("decisionLogHistoryFlush", "Nu opslaan", "flush-decision-log-history", { disabled: !decisionLogHistoryEnabled, busyLabel: "Opslaan..." })}</div>` : ""}
        </div>
      </section>${renderSettingsStorageTechnicalDetails([decisionDetails])}
      ${hasEntity("decisionLogHistoryClear") ? `<details class="oq-settings-storage-advanced"${state.settingsStorageAdvancedOpen ? " open" : ""}><summary data-oq-action="toggle-storage-advanced">Geavanceerd</summary><div class="oq-settings-storage-inline-action oq-settings-storage-inline-action--danger"><div><h4>Beslisloghistorie wissen</h4><p>Verwijder alle bewaarde gebeurtenissen. De actuele beslislog blijft staan.</p></div>${renderSettingsStorageActionButton("decisionLogHistoryClear", "Historie wissen", "clear-decision-log-history", { disabled: Number(decisionMetadata.storedEvents || 0) <= 0, buttonClass: "oq-helper-button oq-helper-button--warning", busyLabel: "Wissen..." })}</div></details>` : ""}`;
    } else if (page === "energy") {
      title = "Energiehistorie";
      copy = "Beheer dagtotalen en uurdetail voor de Resultatenpagina.";
      body = `${backButton}<section class="oq-settings-storage-domain oq-settings-storage-domain--single">
        <div class="oq-settings-storage-domain-rows">
          ${renderSettingsStorageSwitchRow("lifetimeEnergyHistoryEnabled", "Dagtotalen bewaren", "Bewaar elke dag een samenvatting van je energiegegevens.", "Resultaten blijven beschikbaar na een herstart of update.", "Nieuwe dagtotalen worden niet bewaard; bestaande historie blijft staan.", "Blijft bewaard na herstart")}
          ${renderSettingsStorageSelectRow("lifetimeEnergyHourRetention", "Uurdetail bewaren", "Kies hoelang OpenQuatt detail per uur bewaart voor de daggrafiek.", "Bewaartermijn")}
          ${canCaptureLifetime ? `<div class="oq-settings-storage-inline-action"><div><h4>Vandaag alvast opslaan</h4><p>Maak vóór een update of herstart een extra opslagmoment.</p></div>${renderSettingsStorageActionButton("lifetimeEnergyHistoryCapture", "Vandaag opslaan", "save-lifetime-energy-history", { disabled: !lifetimeEnergyHistoryEnabled, busyLabel: "Opslaan..." })}</div>` : ""}
        </div>
      </section>${renderSettingsStorageTechnicalDetails(energyDetails)}
      <details class="oq-settings-storage-advanced"${state.settingsStorageAdvancedOpen ? " open" : ""}><summary data-oq-action="toggle-storage-advanced">Geavanceerd</summary><div class="oq-settings-storage-advanced-body">${renderSettingsEnergyHistoryExportPanel()}${renderSettingsEnergyHistoryImportPanel()}${hasEntity("lifetimeEnergyHistoryClear") ? `<div class="oq-settings-storage-inline-action oq-settings-storage-inline-action--danger"><div><h4>Energiehistorie wissen</h4><p>Verwijder alle bewaarde dagtotalen en begin opnieuw.</p></div>${renderSettingsStorageActionButton("lifetimeEnergyHistoryClear", "Historie wissen", "clear-lifetime-energy-history", { disabled: !canClearLifetime, buttonClass: "oq-helper-button oq-helper-button--warning", busyLabel: "Wissen..." })}</div>` : ""}</div></details>`;
    }

    return renderModalShell({
      id: "system",
      titleId: "oq-history-storage-modal-title",
      kicker: page === "overview" ? "Gegevens" : "Gegevens bewaren",
      title,
      copy,
      className: "oq-helper-modal--scrollable oq-settings-storage-modal",
      sectionAttributes: "data-oq-history-storage-scroller",
      closeAction: "close-system-modal",
      closeLabel: "Sluit gegevens bewaren",
      body,
      actions: '<button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-system-modal">Gereed</button>',
    });
  }

  export function renderSettingsBackupSection() {
    const busy = state.settingsBackupBusy;
    const totalFields = SETTINGS_BACKUP_KEYS.length;
    const sectionCount = SETTINGS_BACKUP_SECTIONS.length;

    return renderSettingsSection(
      "Beheer",
      "Backup en restore",
      "Sla een JSON-backup op van de instellingen die OpenQuatt in deze web-app beheert, en zet die later weer terug na een factory-bin update.",
      `
        <div class="oq-settings-backup-shell">
          <div class="oq-settings-backup-summary">
            <div class="oq-settings-backup-stat">
              <span class="oq-settings-backup-stat-label">Instellingen</span>
              <strong class="oq-settings-backup-stat-value">${escapeHtml(String(totalFields))}</strong>
            </div>
            <div class="oq-settings-backup-stat">
              <span class="oq-settings-backup-stat-label">Secties</span>
              <strong class="oq-settings-backup-stat-value">${escapeHtml(String(sectionCount))}</strong>
            </div>
            <div class="oq-settings-backup-stat">
              <span class="oq-settings-backup-stat-label">MQTT</span>
              <strong class="oq-settings-backup-stat-value">Zonder wachtwoord</strong>
            </div>
          </div>
          <div class="oq-settings-backup-actions">
            <button
              class="oq-helper-button oq-helper-button--primary"
              type="button"
              data-oq-action="download-settings-backup"
              ${busy ? "disabled" : ""}
            >
              ${busy ? "Bezig..." : "Backup downloaden"}
            </button>
            <button
              class="oq-helper-button oq-helper-button--ghost"
              type="button"
              data-oq-action="open-settings-backup-import"
              ${busy ? "disabled" : ""}
            >
              Backup herstellen
            </button>
          </div>
          <p class="oq-settings-action-note">Sensorcorrecties en de MQTT-configuratie worden meegenomen, maar het MQTT-wachtwoord nooit. Ontbrekende en onbekende velden worden na restore benoemd.</p>
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
      kicker: "Beheer",
      title: "Backup herstellen",
      copy: "Kies een JSON-backup om de instellingen te vergelijken en daarna gericht terug te zetten.",
      className: "oq-helper-modal--wide",
      closeAction: "close-system-modal",
      closeLabel: "Sluit backup import popup",
      body: `
          <div class="oq-helper-modal-row">
            <span class="oq-helper-modal-label">Backupbestand</span>
            <input
              class="oq-settings-backup-input oq-settings-backup-import-input"
              type="file"
              accept=".json,application/json"
              data-oq-backup-file-input="true"
              ${busy ? "disabled" : ""}
            >
            <span class="oq-helper-modal-subvalue">Na selectie openen we automatisch het vergelijkingsoverzicht.</span>
          </div>
          ${state.settingsBackupError ? `<p class="oq-settings-backup-error">${escapeHtml(state.settingsBackupError)}</p>` : ""}`,
      actions: `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${busy ? "disabled" : ""}>Annuleren</button>`,
    });
  }

  export function renderSettingsBackupRestoreModal() {
    const draft = state.settingsBackupDraft;
    if (!draft) {
      return "";
    }

    const summary = draft.summary || getSettingsBackupSelectionSummary(draft);
    const sourceInstallation = String(draft.source?.installation || draft.source?.device || "Onbekend");
    const currentInstallation = getInstallationLabel();
    const sourceVersion = String(draft.source?.firmware_version || "Onbekend");
    const sourceChannel = String(draft.source?.firmware_channel || "").trim() || "Onbekend";
    const sourceTopology = String(draft.source?.topology || "").trim() || "Onbekend";
    const currentVersion = getFirmwareCurrentVersion();
    const currentTopology = typeof getInstallationTopology === "function"
      ? getInstallationTopology()
      : "";
    const topologyMismatch = sourceTopology !== "Onbekend" && currentTopology && sourceTopology !== currentTopology;
    const installationMismatch = sourceInstallation !== "Onbekend" && sourceInstallation !== currentInstallation;
    const mqtt = draft.mqtt;
    const mqttNeedsPassword = settingsBackupMqttNeedsPassword(mqtt);
    const mqttPasswordMissing = mqttNeedsPassword && !String(state.settingsBackupMqttPassword || "");
    const mqttValue = mqtt ? (mqtt.enabled ? "Ingeschakeld" : "Uitgeschakeld") : "Niet in backup";
    const mqttMeta = mqtt
      ? `${mqtt.broker || "Geen broker"}:${mqtt.port} · ${mqtt.password_was_set ? "Wachtwoord niet opgeslagen" : "Geen wachtwoord ingesteld"}`
      : "MQTT-configuratie en MQTT-afhankelijke bronselecties worden niet hersteld.";
    const warningText = topologyMismatch || installationMismatch
      ? "De backup lijkt van een andere installatie te komen. Je kunt nog steeds doorzetten, maar controleer de secties even goed."
      : summary.requiredMissing
        ? "Ontbrekende velden houden hun firmware-default."
        : "Velden zonder waarde worden overgeslagen.";
    const electricalRestoreWarning = hasEntity("electricalCurrentLimit")
      ? getElectricalLimitBackupRestoreWarning(draft.settings)
      : "";

    return renderModalShell({
      id: "system",
      titleId: "oq-backup-modal-title",
      kicker: "Beheer",
      title: "Backup herstellen",
      copy: "Deze backup zet alleen de instellingen terug die OpenQuatt in de web-app beheert. Klap een sectie open om backup- en huidige waarden naast elkaar te vergelijken.",
      className: "oq-helper-modal--wide oq-helper-modal--scrollable",
      sectionAttributes: "data-oq-settings-backup-restore-scroller",
      closeAction: "close-system-modal",
      closeLabel: "Sluit backup-popup",
      body: `
          <div class="oq-helper-modal-grid oq-settings-backup-modal-grid">
            <div class="oq-helper-modal-row">
              <span class="oq-helper-modal-label">Backup van</span>
              <strong class="oq-helper-modal-value">${escapeHtml(sourceInstallation)}</strong>
              <span class="oq-helper-modal-subvalue">Topo: ${escapeHtml(sourceTopology)} · Firmware: ${escapeHtml(sourceVersion)}</span>
            </div>
            <div class="oq-helper-modal-row">
              <span class="oq-helper-modal-label">Huidige installatie</span>
              <strong class="oq-helper-modal-value">${escapeHtml(currentInstallation)}</strong>
              <span class="oq-helper-modal-subvalue">Topo: ${escapeHtml(currentTopology)} · Firmware: ${escapeHtml(currentVersion || "Onbekend")}</span>
            </div>
            <div class="oq-helper-modal-row">
              <span class="oq-helper-modal-label">Backupkanaal</span>
              <strong class="oq-helper-modal-value">${escapeHtml(sourceChannel)}</strong>
              <span class="oq-helper-modal-subvalue">Schema v${escapeHtml(String(draft.schema_version || 1))}</span>
            </div>
            <div class="oq-helper-modal-row">
              <span class="oq-helper-modal-label">Backupinstellingen</span>
              <strong class="oq-helper-modal-value">${escapeHtml(`${summary.total} instellingen`)}</strong>
              <span class="oq-helper-modal-subvalue">${escapeHtml(summary.differenceCount ? `${summary.differenceCount} ${summary.differenceCount === 1 ? "verschil" : "verschillen"} · ${summary.currentPresent} op huidige installatie · ${summary.unknown} onbekend` : `Alles komt overeen · ${summary.currentPresent} op huidige installatie · ${summary.unknown} onbekend`)}</span>
            </div>
            <div class="oq-helper-modal-row">
              <span class="oq-helper-modal-label">MQTT-configuratie</span>
              <strong class="oq-helper-modal-value">${escapeHtml(mqttValue)}</strong>
              <span class="oq-helper-modal-subvalue">${escapeHtml(mqttMeta)}</span>
            </div>
          </div>
          ${mqttNeedsPassword ? `
            <label class="oq-settings-backup-mqtt-password">
              <span class="oq-helper-modal-label">MQTT-wachtwoord</span>
              <input
                class="oq-helper-input"
                type="password"
                autocomplete="current-password"
                data-oq-backup-mqtt-password="true"
                placeholder="Vul het MQTT-wachtwoord opnieuw in"
                ${state.settingsBackupBusy ? "disabled" : ""}
              >
              <span class="oq-helper-modal-subvalue">Het wachtwoord stond bewust niet in de backup en wordt alleen voor deze restore gebruikt.</span>
            </label>
          ` : ""}
          <div class="oq-settings-backup-modal-sections">
            ${summary.sectionSummaries.map((section) => `
              <details class="oq-settings-backup-modal-section">
                <summary class="oq-settings-backup-modal-section-head">
                  <span class="oq-settings-backup-modal-section-head-copy">
                    <strong>${escapeHtml(section.label)}</strong>
                    <em>${escapeHtml(`${section.total} ${section.total === 1 ? "instelling" : "instellingen"} · ${section.differenceCount ? `${section.differenceCount} ${section.differenceCount === 1 ? "verschil" : "verschillen"}` : "Alles gelijk"}`)}</em>
                  </span>
                </summary>
                <div class="oq-settings-backup-modal-section-body">
                  <p>${escapeHtml(section.differenceCount ? `${section.differenceCount} instelling${section.differenceCount === 1 ? "" : "en"} wijkt af of ontbreekt.` : "Alle instellingen komen overeen.")}</p>
                  <div class="oq-settings-backup-compare-list">
                    ${section.rows.map((row) => `
                      <div class="oq-settings-backup-compare oq-settings-backup-compare--${escapeHtml(row.status)}">
                        <div class="oq-settings-backup-compare-head">
                          <strong>${escapeHtml(row.label)}</strong>
                          <span>${escapeHtml(row.statusLabel)}</span>
                        </div>
                        <div class="oq-settings-backup-compare-values">
                          <div class="oq-settings-backup-compare-value" data-change="${escapeHtml(row.status)}">
                            <span>Backup</span>
                            <strong>${escapeHtml(row.backupDisplay)}</strong>
                          </div>
                          <div class="oq-settings-backup-compare-value" data-change="${escapeHtml(row.status)}">
                            <span>Nu</span>
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
        <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="close-system-modal" ${state.settingsBackupBusy ? "disabled" : ""}>Annuleren</button>
        <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="confirm-settings-backup-restore" ${state.settingsBackupBusy || mqttPasswordMissing ? "disabled" : ""}>${state.settingsBackupBusy ? "Herstellen..." : mqttPasswordMissing ? "Vul MQTT-wachtwoord in" : "Herstellen"}</button>
      `,
    });
  }
