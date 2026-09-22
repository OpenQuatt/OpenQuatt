import { getEntityNumericValue, getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";
import { getInputDraftValue } from "../core/control-drafts.js";
import { getNumberMeta, parseLooseNumber } from "../core/entity-store.js";
import { renderModalShell } from "../core/modal-shell.js";
import { renderNumberInputControl } from "../core/number-controls.js";
import { formatSettingsNumberValue, getSettingsTemperatureValue, renderSettingsNumberField, renderSettingsSection, renderSettingsSystemRow } from "./controls.js";
import { escapeHtml } from "../core/html.js";
import { t } from "../i18n/index.js";

  export function getHpWaterRawValue(rawKey, finalKey, offsetKey) {
    const finalValue = getEntityNumericValue(finalKey);
    const offset = getEntityNumericValue(offsetKey);
    if (Number.isFinite(finalValue) && Number.isFinite(offset)) {
      return finalValue - offset;
    }
    const raw = getEntityNumericValue(rawKey);
    return Number.isFinite(raw) ? raw : NaN;
  }

  export function getWaterSupplyCorrectionView() {
    const source = getEntityStateText("waterSupplyTempEffectiveSource", t("settingsWater.sourceActive"));
    const status = getEntityStateText("waterSupplyCalibrationStatus", "");
    const activeValue = getEntityNumericValue("supplyTemp");
    const storedOffset = getEntityNumericValue("waterSupplyCalibrationOffset");
    const fallbackActive = isEntityActive("waterSupplyTempFallbackActive") || /\(fallback\)/i.test(source);
    const calibrationRequired = isEntityActive("waterSupplyCalibrationRequired") || status.startsWith("Recalibration required:");
    const calibrationActive = status.startsWith("Calibrated:") && !calibrationRequired && !fallbackActive && Number.isFinite(storedOffset);
    const activeOffset = calibrationActive ? storedOffset : 0;

    let statusLabel = t("settingsWater.statusNone");
    if (fallbackActive) {
      statusLabel = t("settingsWater.statusFallback");
    } else if (calibrationRequired) {
      statusLabel = t("settingsWater.statusRecalibrate");
    } else if (calibrationActive) {
      statusLabel = t("settingsWater.statusSourceBound");
    }

    return {
      source,
      statusLabel,
      calibrationActive,
      calibrationRequired,
      rawValue: Number.isFinite(activeValue) ? activeValue - activeOffset : NaN,
      offsetValue: activeOffset,
      activeValue,
      uom: getNumberMeta("waterSupplyCalibrationOffset").uom || "°C",
    };
  }

  export function renderWaterSettingsFields(className = "oq-settings-grid", { includeSensorCorrections = true } = {}) {
    const offsetLauncher = includeSensorCorrections && hasHpWaterSensorOffsetSettings()
      ? renderSettingsSystemRow({
          label: t("settingsWater.launcherTitle"),
          value: t("settingsWater.launcherValue"),
          note: t("settingsWater.launcherNote"),
          action: `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="open-water-sensor-corrections-modal">${escapeHtml(t("settingsWater.launcherAction"))}</button>`,
          className: "oq-settings-hp-offset-launcher",
          dataAttribute: "",
        })
      : "";
    return `
      <div class="${escapeHtml(className)}">
        ${renderSettingsNumberField("maxWater", t("settingsWater.maxWaterTitle"), t("settingsWater.maxWaterCopy"))}
      </div>
      ${offsetLauncher}
    `;
  }

  function hasHpWaterSensorOffsetSettings() {
    const hpRows = [
      ["hp1WaterInOffset", "hp1WaterIn"],
      ["hp1WaterOutOffset", "hp1WaterOut"],
      ["hp2WaterInOffset", "hp2WaterIn"],
      ["hp2WaterOutOffset", "hp2WaterOut"],
    ];
    return hpRows.some(([offsetKey, finalKey]) => hasEntity(offsetKey) && hasEntity(finalKey)) ||
      (hasEntity("supplyTemp") && hasEntity("waterSupplyCalibrationOffset"));
  }

  export function renderHpWaterSensorOffsetSettings({ showHeader = true } = {}) {
    const rows = [
      { label: t("settingsWater.hp1WaterIn"), rawKey: "hp1WaterInRaw", offsetKey: "hp1WaterInOffset", finalKey: "hp1WaterIn" },
      { label: t("settingsWater.hp1WaterOut"), rawKey: "hp1WaterOutRaw", offsetKey: "hp1WaterOutOffset", finalKey: "hp1WaterOut" },
      { label: t("settingsWater.hp2WaterIn"), rawKey: "hp2WaterInRaw", offsetKey: "hp2WaterInOffset", finalKey: "hp2WaterIn" },
      { label: t("settingsWater.hp2WaterOut"), rawKey: "hp2WaterOutRaw", offsetKey: "hp2WaterOutOffset", finalKey: "hp2WaterOut" },
    ].filter((row) => hasEntity(row.offsetKey) && hasEntity(row.finalKey));
    const hasSupplyCorrection = hasEntity("supplyTemp") && hasEntity("waterSupplyCalibrationOffset");

    if (!rows.length && !hasSupplyCorrection) {
      return "";
    }

    const formatSupplyValue = (value, uom) => Number.isFinite(value)
      ? formatSettingsNumberValue(value, uom, 2)
      : "—";

    const renderRow = (row) => {
      const meta = getNumberMeta(row.offsetKey);
      const raw = getHpWaterRawValue(row.rawKey, row.finalKey, row.offsetKey);
      const offsetDraft = parseLooseNumber(getInputDraftValue(row.offsetKey));
      const finalFromDraft = Number.isFinite(raw) && Number.isFinite(offsetDraft)
        ? formatSettingsNumberValue(raw + offsetDraft, meta.uom || "°C", 2)
        : getSettingsTemperatureValue(row.finalKey, 2);

      return `
        <article class="oq-settings-hp-offset-row">
          <div class="oq-settings-hp-offset-copy">
            <strong>${escapeHtml(row.label)}</strong>
            <span>${escapeHtml(t("settingsWater.rowActiveSuffix", { value: getSettingsTemperatureValue(row.finalKey, 2) }))}</span>
          </div>
          <div class="oq-settings-hp-offset-equation" aria-label="${escapeHtml(t("settingsWater.rowCorrectionAria", { label: row.label }))}">
            <div class="oq-settings-hp-offset-readout">
              <span>${escapeHtml(t("settingsWater.rawLabel"))}</span>
              <strong>${escapeHtml(Number.isFinite(raw) ? formatSettingsNumberValue(raw, meta.uom || "°C", 2) : getSettingsTemperatureValue(row.rawKey, 2))}</strong>
            </div>
            <span class="oq-settings-hp-offset-operator">+</span>
            <label class="oq-settings-hp-offset-input">
              <span>${escapeHtml(t("settingsWater.correctionLabel"))}</span>
              ${renderNumberInputControl({
                key: row.offsetKey,
                value: getInputDraftValue(row.offsetKey),
                meta,
                controlClass: "oq-helper-control oq-helper-control--suffix",
                inputClass: "oq-helper-input oq-helper-input--compact-number",
                unitMarkup: meta.uom ? `<span class="oq-helper-unit-chip">${escapeHtml(meta.uom)}</span>` : "",
              })}
            </label>
            <span class="oq-settings-hp-offset-operator">=</span>
            <div class="oq-settings-hp-offset-readout oq-settings-hp-offset-final">
              <span>${escapeHtml(t("settingsWater.afterChangeLabel"))}</span>
              <strong>${escapeHtml(finalFromDraft)}</strong>
            </div>
          </div>
        </article>
      `;
    };

    const renderSupplyRow = () => {
      const view = getWaterSupplyCorrectionView();
      return `
        <article class="oq-settings-hp-offset-row is-readonly">
          <div class="oq-settings-hp-offset-copy">
            <strong>${escapeHtml(t("settingsWater.supplyTitle", { source: view.source }))}</strong>
            <span>${escapeHtml(t("settingsWater.supplyActiveSuffix", { value: formatSupplyValue(view.activeValue, view.uom), status: view.statusLabel }))}</span>
          </div>
          <div class="oq-settings-hp-offset-equation" aria-label="${escapeHtml(t("settingsWater.supplyCorrectionAria"))}">
            <div class="oq-settings-hp-offset-readout">
              <span>${escapeHtml(t("settingsWater.rawLabel"))}</span>
              <strong>${escapeHtml(formatSupplyValue(view.rawValue, view.uom))}</strong>
            </div>
            <span class="oq-settings-hp-offset-operator">+</span>
            <div class="oq-settings-hp-offset-readout">
              <span>${escapeHtml(t("settingsWater.supplyCorrectionActive"))}</span>
              <strong>${escapeHtml(formatSupplyValue(view.offsetValue, view.uom))}</strong>
            </div>
            <span class="oq-settings-hp-offset-operator">=</span>
            <div class="oq-settings-hp-offset-readout oq-settings-hp-offset-final">
              <span>${escapeHtml(t("settingsWater.supplyActiveLabel"))}</span>
              <strong>${escapeHtml(formatSupplyValue(view.activeValue, view.uom))}</strong>
            </div>
          </div>
        </article>
      `;
    };

    return `
      <div class="oq-settings-subpanel oq-settings-hp-offset-panel">
        ${showHeader ? `<div class="oq-settings-subpanel-head">
          <p class="oq-helper-label">${escapeHtml(t("settingsWater.panelKicker"))}</p>
          <h4>${escapeHtml(t("settingsWater.panelTitle"))}</h4>
          <p>${escapeHtml(t("settingsWater.panelCopy"))}</p>
        </div>` : ""}
        <div class="oq-settings-hp-offset-list">
          ${rows.map(renderRow).join("")}
          ${hasSupplyCorrection ? renderSupplyRow() : ""}
        </div>
        ${hasSupplyCorrection ? `
          <aside class="oq-settings-hp-offset-supply-note">
            <strong>${escapeHtml(t("settingsWater.supplyNoteTitle"))}</strong>
            <p>${escapeHtml(t("settingsWater.supplyNoteCopy"))}</p>
          </aside>
        ` : ""}
      </div>
    `;
  }

  export function renderHpWaterSensorOffsetsModal() {
    const body = renderHpWaterSensorOffsetSettings({ showHeader: false });
    if (!body) {
      return "";
    }
    return renderModalShell({
      id: "system",
      titleId: "oq-water-sensor-corrections-modal-title",
      kicker: t("settingsWater.modalKicker"),
      title: t("settingsWater.modalTitle"),
      copy: t("settingsWater.modalCopy"),
      className: "oq-helper-modal--wide oq-helper-modal--scrollable oq-settings-hp-offset-modal",
      sectionAttributes: "data-oq-water-offset-modal",
      closeAction: "close-system-modal",
      closeLabel: t("settingsWater.modalClose"),
      body,
      actions: `<button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-system-modal">${escapeHtml(t("settingsWater.modalDone"))}</button>`,
    });
  }

  export function renderSettingsWaterSection() {
    return renderSettingsSection(
      t("settingsWater.sectionGroup"),
      t("settingsWater.sectionTitle"),
      t("settingsWater.sectionCopy"),
      renderWaterSettingsFields(),
    );
  }
