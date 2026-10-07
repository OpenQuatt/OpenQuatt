import { getEntityNumericValue, hasEntity } from "../core/app-shared.js";
import { STRATEGY_OPTION_CURVE, STRATEGY_OPTION_POWER_HOUSE } from "../core/config.js";
import { isCurveMode, isManualFlowMode } from "../core/domain-helpers.js";
import { getCurveFallbackSuggestion, getEntityValue, parseLooseNumber } from "../core/entity-store.js";
import { getHeatingEnableAdvice } from "../core/heating-strategy-matrix.js";
import { state } from "../core/state.js";
import { getSettingsSelectModel, getSettingsSwitchModel } from "./field-models.js";
import { getSettingsTextStatValue, renderSettingsAdvancedDisclosure, renderSettingsCompactSwitchControl, renderSettingsSwitchCopy, renderSettingsChoiceOption, renderSettingsFieldCard, renderSettingsFrequencyRangeField, renderSettingsMiniNumberField, renderSettingsNumberField, renderSettingsSection, renderSettingsSelectField, renderSettingsSwitchField } from "./controls.js";
import { formatNumericState } from "../core/formatting.js";
import { escapeHtml } from "../core/html.js";
import { formatNumber, t } from "../i18n/index.js";
import { getInputDraftValue } from "../core/control-drafts.js";
import { getCurvePointDraft, getSimpleCurveDraft } from "../core/simple-curve.js";

  export function renderCurveFallbackSuggestionMarkup(helper = false) {
    const suggestion = getCurveFallbackSuggestion();
    if (!suggestion) {
      return "";
    }
    return `
      <div class="oq-curve-fallback-suggest oq-curve-fallback-suggest--inside${helper ? " oq-curve-fallback-suggest--helper" : ""}">
        <div class="oq-curve-fallback-suggest-copy">
          <strong>${escapeHtml(t("settingsHeating.suggestTitle", { label: suggestion.label }))}</strong>
          <span>${escapeHtml(suggestion.basis)}</span>
        </div>
        <button
          class="oq-helper-button oq-helper-button--ghost"
          type="button"
          data-oq-action="suggest-curve-fallback"
          ${state.loadingEntities || state.busyAction === "save-curveFallbackSupply" || suggestion.isCurrent ? "disabled" : ""}
        >
          ${suggestion.isCurrent ? escapeHtml(t("settingsHeating.suggestActive")) : escapeHtml(t("settingsHeating.suggestUse"))}
        </button>
      </div>
    `;
  }

  export function renderSettingsCurveInputs() {
    const draft = getSimpleCurveDraft();
    const previewLimit = getSimpleCurvePreviewLimit();
    return `
      <div class="oq-simple-curve-editor">
        <div class="oq-simple-curve-heading">
          <h4>${escapeHtml(t("settingsHeating.simpleTitle"))}</h4>
          <p>${escapeHtml(t("settingsHeating.simpleCopy"))}</p>
        </div>
        <div class="oq-simple-curve-workspace">
          <div class="oq-simple-curve-controls">
            <label class="oq-simple-curve-control">
              <span>${escapeHtml(t("settingsHeating.simpleSlope"))}</span>
              <output data-oq-simple-curve-value="slope">${draft.slope.toFixed(1)} K / 10°C</output>
              <input type="range" min="0" max="15" step="0.5" value="${draft.slope}" data-oq-simple-curve="slope" />
            </label>
            <label class="oq-simple-curve-control">
              <span>${escapeHtml(t("settingsHeating.simpleLevel"))}</span>
              <output data-oq-simple-curve-value="level">${draft.level.toFixed(1)} °C</output>
              <input type="range" min="20" max="70" step="0.5" value="${draft.level}" data-oq-simple-curve="level" />
            </label>
            <div class="oq-simple-curve-action">
              <button type="button" class="oq-helper-button oq-helper-button--primary" data-oq-action="apply-simple-curve" ${state.simpleCurveApplying || state.loadingEntities ? "disabled" : ""}>${escapeHtml(t("settingsHeating.simpleApply"))}</button>
              <p>${escapeHtml(t("settingsHeating.simplePreviewCopy"))}</p>
            </div>
          </div>
          <div class="oq-simple-curve-visual">
            <div class="oq-simple-curve-visual-heading">
              <strong>${escapeHtml(t("settingsHeating.simplePreview"))}</strong>
              <span>${escapeHtml(previewLimit === null ? t("settingsHeating.simpleAxes") : t("settingsHeating.simpleLimit", { value: formatNumber(previewLimit, { maximumFractionDigits: 1 }) }))}</span>
            </div>
            <div data-oq-simple-curve-preview>${renderSimpleCurvePreview()}</div>
          </div>
        </div>
        ${renderCurveTargetBreakdown()}
      </div>
      ${renderSettingsAdvancedDisclosure("curve-fallback", t("settingsHeating.fallbackTitle"), t("settingsHeating.fallbackCopy"), `
        <div class="oq-settings-grid">
          ${renderSettingsNumberField("curveFallbackSupply", t("settingsHeating.fallbackTitle"), t("settingsHeating.fallbackCopy"), "oq-settings-field--curve-fallback-card", { footerMarkup: renderCurveFallbackSuggestionMarkup() })}
        </div>
      `)}
    `;
  }

  function getSimpleCurvePreviewLimit() {
    const value = getEntityValue("maxWater");
    if (value === null || value === undefined || value === "") return null;
    const limit = Number(value);
    return Number.isFinite(limit) && limit >= 25 && limit <= 75 ? limit : null;
  }

  export function renderSimpleCurvePreview() {
    const points = getCurvePointDraft() || [];
    return `<div class="oq-curve-edit-state${state.curvePointDraft ? " is-pending" : ""}">${escapeHtml(t(state.curvePointDraft ? "settingsHeating.simpleUnsaved" : "settingsHeating.simpleSaved"))}</div>${renderCurvePreviewChart(points)}`;
  }

  function renderCurvePreviewChart(points) {
    const limit = getSimpleCurvePreviewLimit();
    const x = (outdoor) => 64 + ((outdoor + 20) / 35) * 432;
    const y = (value) => 20 + ((70 - value) / 50) * 158;
    const visiblePoints = points.flatMap((point, index) => {
      if (limit === null || index === 0) return [point];
      const previous = points[index - 1];
      if ((previous.value - limit) * (point.value - limit) >= 0) return [point];
      const outdoor = previous.outdoor + ((limit - previous.value) / (point.value - previous.value)) * (point.outdoor - previous.outdoor);
      return [{ outdoor, value: limit }, point];
    });
    const line = visiblePoints.map((point) => `${x(point.outdoor)},${y(limit === null ? point.value : Math.min(point.value, limit))}`).join(" ");
    const rawLine = points.map((point) => `${x(point.outdoor)},${y(point.value)}`).join(" ");
    const clipped = limit !== null && points.some((point) => point.value > limit);
    return `
      <div class="oq-simple-curve-plot">
      <svg class="oq-simple-curve-chart oq-helper-curve-svg" viewBox="0 0 560 186" role="img" aria-label="${escapeHtml(t("settingsHeating.curveEditorAria"))}">
        ${[20, 30, 40, 50, 60, 70].map((value) => `<line x1="64" y1="${y(value)}" x2="496" y2="${y(value)}" class="oq-simple-curve-gridline" /><text x="54" y="${y(value) + 4}" text-anchor="end" class="oq-simple-curve-axis">${value}°C</text>`).join("")}
        ${clipped ? `<line x1="64" y1="${y(limit)}" x2="496" y2="${y(limit)}" class="oq-simple-curve-gridline" stroke-dasharray="5 4" /><polyline points="${rawLine}" class="oq-simple-curve-line" stroke-dasharray="4 4" opacity="0.4" />` : ""}
        <polygon points="64,178 ${line} 496,178" class="oq-simple-curve-area" />
        <polyline points="${line}" class="oq-simple-curve-line" />
        ${points.map((point) => `<circle cx="${x(point.outdoor)}" cy="${y(point.value)}" r="16" class="oq-simple-curve-hit" data-curve-key="${escapeHtml(point.key)}" /><circle cx="${x(point.outdoor)}" cy="${y(point.value)}" r="6.5" class="oq-simple-curve-point oq-helper-curve-point${state.draggingCurveKey === point.key ? " is-dragging" : ""}" />`).join("")}
      </svg>
      <div class="oq-simple-curve-points">${points.map((point) => `<label style="left:${(x(point.outdoor) / 560) * 100}%"><small>${escapeHtml(point.label)}</small><span class="oq-simple-curve-point-entry"><input type="number" min="20" max="70" step="0.5" inputmode="decimal" value="${point.value.toFixed(1)}" data-oq-curve-point-input="${escapeHtml(point.key)}" aria-label="${escapeHtml(t("settingsHeating.curvePointTitle", { label: point.label }))}" ${state.simpleCurveApplying || state.loadingEntities ? "disabled" : ""} /></span>${limit !== null && point.value > limit ? `<small>${escapeHtml(t("settingsHeating.simpleCappedPoint", { value: limit.toFixed(1) }))}</small>` : ""}</label>`).join("")}</div>
      </div>
    `;
  }

  function renderCurveTargetBreakdown() {
    if (!hasEntity("curveBaseTarget")) return "";
    const rows = [
      [t("settingsHeating.baseTargetLabel"), "curveBaseTarget", "°C"],
      [t("settingsHeating.modifierLabel"), "curveModifier", "°C"],
      [t("settingsHeating.roomTrimLabel"), "curveRoomTrim", "°C"],
      [t("settingsHeating.effectiveTargetLabel"), "curveEffectiveTarget", "°C"],
    ];
    return `<div class="oq-simple-curve-breakdown">
      ${rows.map(([label, key, unit]) => `<div><span>${escapeHtml(label)}</span><strong>${escapeHtml(formatNumericState(getEntityNumericValue(key), 1, unit))}</strong></div>`).join("")}
    </div>`;
  }

  export function renderHeatingCurveAdvancedFields() {
    const fields = [
      renderSettingsNumberField("heatingCurvePidKp", t("settingsHeating.pidKpTitle"), t("settingsHeating.pidKpCopy")),
      renderSettingsNumberField("heatingCurvePidKi", t("settingsHeating.pidKiTitle"), t("settingsHeating.pidKiCopy")),
      renderSettingsNumberField("heatingCurvePidKd", t("settingsHeating.pidKdTitle"), t("settingsHeating.pidKdCopy")),
    ].filter(Boolean).join("");

    return renderSettingsAdvancedDisclosure(
      "heating-curve",
      t("settingsHeating.advancedCurveTitle"),
      t("settingsHeating.advancedCurveCopy"),
      fields ? `<div class="oq-settings-grid oq-settings-grid--pid">${fields}</div>` : "",
    );
  }

  export function renderStrategySelectionFields(className = "oq-settings-grid") {
    return `
      <div class="${escapeHtml(className)}">
        ${renderSettingsSelectField("strategy", t("settingsHeating.strategyTitle"), t("settingsHeating.strategyCopy"))}
      </div>
    `;
  }

  export function renderFlowSettingsFields(className = "oq-settings-grid") {
    const autoFields = [
      renderSettingsNumberField("flowSetpoint", t("settingsHeating.flowHeatTitle"), t("settingsHeating.flowHeatCopy")),
      renderSettingsNumberField("coolingFlowSetpoint", t("settingsHeating.flowCoolTitle"), t("settingsHeating.flowCoolCopy")),
    ].filter(Boolean).join("");
    return `
      <div class="${escapeHtml(className)}">
        ${renderSettingsSelectField("flowControlMode", t("settingsHeating.flowModeTitle"), t("settingsHeating.flowModeCopy"))}
        ${isManualFlowMode()
          ? renderSettingsNumberField("manualIpwm", t("settingsHeating.flowManualTitle"), t("settingsHeating.flowManualCopy"))
          : autoFields}
      </div>
    `;
  }

  export function renderFlowTuningFields(className = "oq-settings-grid") {
    const fields = [
      renderSettingsNumberField("flowKp", t("settingsHeating.flowKpTitle"), t("settingsHeating.flowKpCopy")),
      renderSettingsNumberField("flowKi", t("settingsHeating.flowKiTitle"), t("settingsHeating.flowKiCopy")),
    ].filter(Boolean);
    if (!fields.length) {
      return "";
    }
    return `
      <div class="${escapeHtml(className)}">
        ${fields.join("")}
      </div>
    `;
  }

  export function renderPowerHouseBaseFields(className = "oq-settings-grid") {
    return `
      <div class="${escapeHtml(className)}">
        ${renderSettingsNumberField("houseColdTemp", t("settingsHeating.houseColdTitle"), t("settingsHeating.houseColdCopy"))}
        ${renderSettingsNumberField("houseOutdoorMax", t("settingsHeating.houseOutdoorMaxTitle"), t("settingsHeating.houseOutdoorMaxCopy"))}
        ${renderSettingsNumberField("housePower", t("settingsHeating.housePowerTitle"), t("settingsHeating.housePowerCopy"))}
      </div>
    `;
  }

  export function renderHeatingStrategyExplainCards() {
    const model = getSettingsSelectModel("strategy");
    const powerHouseActive = model.value === STRATEGY_OPTION_POWER_HOUSE;
    const curveActive = model.value === STRATEGY_OPTION_CURVE;
    return `
      <div class="oq-settings-strategy-grid">
        <button
          class="oq-helper-surface oq-settings-strategy-card${powerHouseActive ? " is-active" : ""}"
          type="button"
          data-oq-action="select-settings-option"
          data-select-key="strategy"
          data-oq-select-model="true"
          data-select-option="${escapeHtml(STRATEGY_OPTION_POWER_HOUSE)}"
          aria-pressed="${powerHouseActive ? "true" : "false"}"
          ${model.busy || !model.available ? "disabled" : ""}
        >
          <p class="oq-helper-label">Power House</p>
          <h4>${escapeHtml(t("settingsHeating.strategyPhTitle"))}</h4>
          <p>${escapeHtml(t("settingsHeating.strategyPhCopy"))}</p>
          <ul class="oq-settings-strategy-points">
            <li>${escapeHtml(t("settingsHeating.strategyPhPoint1"))}</li>
            <li>${escapeHtml(t("settingsHeating.strategyPhPoint2"))}</li>
            <li>${escapeHtml(t("settingsHeating.strategyPhPoint3"))}</li>
          </ul>
        </button>
        <button
          class="oq-helper-surface oq-settings-strategy-card${curveActive ? " is-active" : ""}"
          type="button"
          data-oq-action="select-settings-option"
          data-select-key="strategy"
          data-oq-select-model="true"
          data-select-option="${escapeHtml(STRATEGY_OPTION_CURVE)}"
          aria-pressed="${curveActive ? "true" : "false"}"
          ${model.busy || !model.available ? "disabled" : ""}
        >
          <p class="oq-helper-label">${escapeHtml(t("overview.strategyCurve"))}</p>
          <h4>${escapeHtml(t("settingsHeating.strategyCurveTitle"))}</h4>
          <p>${escapeHtml(t("settingsHeating.strategyCurveCopy"))}</p>
          <ul class="oq-settings-strategy-points">
            <li>${t("settingsHeating.strategyCurvePoint1", { range: `<strong>${escapeHtml(t("settingsHeating.curveRange"))}</strong>` })}</li>
            <li>${escapeHtml(t("settingsHeating.strategyCurvePoint2"))}</li>
            <li>${escapeHtml(t("settingsHeating.strategyCurvePoint3"))}</li>
          </ul>
        </button>
      </div>
    `;
  }

  export function renderPowerHouseResponseProfilesField() {
    const model = getSettingsSelectModel("phResponseProfile");
    if (!model.available) {
      return "";
    }

    const options = [
      {
        value: "Calm",
        label: t("settingsHeating.profileCalm"),
        rise: "12 min",
        fall: "5 min",
        meta: t("settingsHeating.profileCalmMeta", { rise: "12 min", fall: "5 min" }),
        copy: t("settingsHeating.profileCalmCopy"),
      },
      {
        value: "Balanced",
        label: t("settingsHeating.profileBalanced"),
        rise: "8 min",
        fall: "3 min",
        meta: t("settingsHeating.profileBalancedMeta", { rise: "8 min", fall: "3 min" }),
        copy: t("settingsHeating.profileBalancedCopy"),
      },
      {
        value: "Responsive",
        label: t("settingsHeating.profileResponsive"),
        rise: "5 min",
        fall: "2 min",
        meta: t("settingsHeating.profileResponsiveMeta", { rise: "5 min", fall: "2 min" }),
        copy: t("settingsHeating.profileResponsiveCopy"),
      },
      {
        value: "Custom",
        label: t("settingsHeating.profileCustom"),
        rise: t("settingsHeating.profileCustomRise"),
        fall: t("settingsHeating.profileCustomFall"),
        meta: t("settingsHeating.profileCustomMeta"),
        copy: t("settingsHeating.profileCustomCopy"),
      },
    ];
    const controlMarkup = `
      <div class="oq-settings-choice-grid oq-settings-choice-grid--response">
        ${options.map((option) => {
          const isActive = option.value === model.value;
          if (option.value === "Custom" && isActive) {
            return `
              <div class="oq-helper-surface oq-settings-choice-card oq-settings-choice-card--static oq-settings-choice-card--custom is-active">
                <span class="oq-settings-choice-title">${escapeHtml(option.label)}</span>
                <div class="oq-settings-choice-meta">
                  <span class="oq-settings-choice-meta-text">${escapeHtml(option.meta)}</span>
                </div>
                <span class="oq-settings-choice-copy">${escapeHtml(option.copy)}</span>
                <div class="oq-settings-choice-inline-grid oq-settings-choice-inline-grid--inside-card">
                  ${renderSettingsMiniNumberField("phDemandRiseTime", t("settingsHeating.riseTitle"), t("settingsHeating.riseCopy"), { compact: true, showCopy: false, infoId: "phDemandRiseTime-inline", embedded: true })}
                  ${renderSettingsMiniNumberField("phDemandFallTime", t("settingsHeating.fallTitle"), t("settingsHeating.fallCopy"), { compact: true, showCopy: false, infoId: "phDemandFallTime-inline", embedded: true })}
                </div>
              </div>
            `;
          }
          return renderSettingsChoiceOption({ key: "phResponseProfile", option: option.value, model, copy: option.copy, meta: option.meta });
        }).join("")}
      </div>
    `;

    return renderSettingsFieldCard(
      "phResponseProfile",
      t("settingsHeating.responseProfileTitle"),
      t("settingsHeating.responseProfileCopy"),
      controlMarkup,
      "oq-settings-field--span-2",
    );
  }

  export function renderHeatingCurveProfileField() {
    const model = getSettingsSelectModel("curveControlProfile");
    if (!model.available) {
      return "";
    }

    const options = [
      {
        value: "Comfort",
        label: t("settingsHeating.curveComfortLabel"),
        meta: t("settingsHeating.curveComfortMeta"),
        copy: t("settingsHeating.curveComfortCopy"),
      },
      {
        value: "Balanced",
        label: t("settingsHeating.profileBalanced"),
        meta: t("settingsHeating.curveBalancedMeta"),
        copy: t("settingsHeating.curveBalancedCopy"),
      },
      {
        value: "Stable",
        label: t("settingsHeating.curveStableLabel"),
        meta: t("settingsHeating.curveStableMeta"),
        copy: t("settingsHeating.curveStableCopy"),
      },
    ];

    const controlMarkup = `
      <div class="oq-settings-choice-grid oq-settings-choice-grid--curve">
        ${options.map((option) => renderSettingsChoiceOption({ key: "curveControlProfile", option: option.value, model, copy: option.copy, meta: option.meta })).join("")}
      </div>
    `;

    return renderSettingsFieldCard(
      "curveControlProfile",
      t("settingsHeating.curveProfileTitle"),
      t("settingsHeating.curveProfileCopy"),
      controlMarkup,
      "oq-settings-field--span-2",
    );
  }

  export function renderPowerHouseAdvancedField() {
    const fields = [
      renderSettingsNumberField("phKp", t("settingsHeating.phKpTitle"), t("settingsHeating.phKpCopy"), "", { unitOverride: "W/K" }),
      renderPowerHouseResponseProfilesField(),
    ].filter(Boolean);

    if (!fields.length && !hasEntity("phComfortBelow") && !hasEntity("phComfortAbove") && !hasEntity("phRunExtension")) {
      return "";
    }

    return `
      <div class="oq-settings-subpanel oq-settings-subpanel--nested oq-ph-comfort">
        <div class="oq-settings-subpanel-head">
          <h4>${escapeHtml(t("settingsHeating.tuningTitle"))}</h4>
          <p>${escapeHtml(t("settingsHeating.tuningCopy"))}</p>
        </div>
        <section class="oq-settings-subpanel oq-settings-subpanel--nested" aria-label="${escapeHtml(t("settingsHeating.maintainTitle"))}">
          <div class="oq-settings-subpanel-head">
            <h4>${escapeHtml(t("settingsHeating.maintainTitle"))}</h4>
            <p>${escapeHtml(t("settingsHeating.maintainCopy"))}</p>
          </div>
          <div class="oq-settings-grid">
            ${renderSettingsNumberField("phComfortBelow", t("settingsHeating.phComfortBelowTitle"), t("settingsHeating.phComfortBelowCopy"), "", { footerMarkup: `<p class="oq-run-extension-note">${escapeHtml(t("settingsHeating.phComfortBelowDirection"))}</p>` })}
            ${renderSettingsNumberField("phComfortAbove", t("settingsHeating.phComfortAboveTitle"), t("settingsHeating.phComfortAboveCopy"), "", { footerMarkup: `<p class="oq-run-extension-note">${escapeHtml(t("settingsHeating.phComfortAboveDirection"))}</p>` })}
          </div>
          <div class="oq-run-extension-summary" data-oq-power-house-comfort-thresholds>${renderPowerHouseComfortThresholds()}</div>
        </section>
        ${renderPowerHouseRunExtensionField()}
        ${renderSettingsAdvancedDisclosure("power-house", t("settingsHeating.phAdvancedTitle"), t("settingsHeating.phAdvancedCopy"), `<div class="oq-settings-grid">${fields.join("")}</div>`)}
      </div>
    `;
  }

  export function formatRunExtensionTemp(value) {
    const numeric = Number(value);
    return Number.isFinite(numeric) ? `${formatNumber(numeric, { minimumFractionDigits: 1, maximumFractionDigits: 2 })} °C` : "—";
  }

  export function getRunExtensionThresholds(useDrafts = true) {
    const numeric = (key) => {
      const entity = state.entities[key];
      const value = useDrafts ? getInputDraftValue(key) : entity?.value ?? entity?.state ?? "";
      return value === "" || value == null ? NaN : Number(value);
    };
    const setpoint = hasEntity("roomSetpoint") ? numeric("roomSetpoint") : NaN;
    const margin = hasEntity("phRunExtensionStopMargin") ? numeric("phRunExtensionStopMargin") : 0.5;
    const configurable = hasEntity("phRunExtensionRestartCooldown");
    const cooldown = configurable ? numeric("phRunExtensionRestartCooldown") : 0.2;
    const coldEdge = setpoint - numeric("phComfortBelow");
    const warmEdge = setpoint + numeric("phComfortAbove");
    const stop = Number.isFinite(setpoint) && Number.isFinite(margin) ? setpoint + margin : NaN;
    const configuredRestart = stop - cooldown;
    // Older firmware still uses its fixed 0.2 K threshold without the new cold-edge guard.
    const restart = configurable ? Math.max(configuredRestart, coldEdge) : configuredRestart;
    return { setpoint, margin, cooldown, stop, configuredRestart, coldEdge, warmEdge, restart, limited: restart > configuredRestart };
  }

  const RUN_EXTENSION_STATUS_COPY = {
    extending: "runExtension.extending",
    comfort_stop: "runExtension.comfortStop",
    wait_warm_restart: "runExtension.waitRestart",
    warm_restart: "runExtension.warmRestart",
    normal: "runExtension.normal",
    blocked: "runExtension.blocked",
  };

  export function getRunExtensionStatusCopy(status) {
    return t(RUN_EXTENSION_STATUS_COPY[String(status || "").trim().toLowerCase()] || "runExtension.disabled");
  }

  function getRunExtensionCurrentStatus() {
    if (!getEntityValue("phRunExtension")) return t("runExtension.disabled");
    const status = String(getSettingsTextStatValue("phRunExtensionStatus", "inactive") || "").trim().toLowerCase();
    return status === "inactive" ? t("runExtension.waiting") : getRunExtensionStatusCopy(status);
  }

  export function renderPowerHouseComfortThresholds() {
    const model = getRunExtensionThresholds();
    const active = getRunExtensionThresholds(false);
    const preview = ["setpoint", "coldEdge", "warmEdge"].some((key) => !Object.is(model[key], active[key]));
    return `
      <p class="oq-run-extension-note">${escapeHtml(t(preview ? "runExtension.preview" : "runExtension.confirmed"))}</p>
      <div class="oq-run-extension-thresholds">
        <div><span>${escapeHtml(t("runExtension.desired"))}</span><strong>${escapeHtml(formatRunExtensionTemp(model.setpoint))}</strong><small>${escapeHtml(t("settingsHeating.desiredSource"))}</small></div>
        <div><span>${escapeHtml(t("settingsHeating.coldEdgeTitle"))}</span><strong>${escapeHtml(formatRunExtensionTemp(model.coldEdge))}</strong><small>${escapeHtml(t("settingsHeating.coldEdgeCopy"))}</small></div>
        ${hasEntity("phComfortAbove") ? `<div><span>${escapeHtml(t("settingsHeating.warmEdgeTitle"))}</span><strong>${escapeHtml(formatRunExtensionTemp(model.warmEdge))}</strong><small>${escapeHtml(t("settingsHeating.warmEdgeCopy"))}</small></div>` : ""}
      </div>
      ${preview ? `<p class="oq-run-extension-note">${escapeHtml(t("settingsHeating.activeColdEdge", { value: formatRunExtensionTemp(active.coldEdge) }))}</p>` : ""}
      ${preview && hasEntity("phComfortAbove") ? `<p class="oq-run-extension-note">${escapeHtml(t("settingsHeating.activeWarmEdge", { value: formatRunExtensionTemp(active.warmEdge) }))}</p>` : ""}
    `;
  }

  export function renderRunExtensionThresholds() {
    const thresholdsModel = getRunExtensionThresholds();
    const active = getRunExtensionThresholds(false);
    const preview = ["setpoint", "margin", "cooldown", "coldEdge"].some((key) => !Object.is(thresholdsModel[key], active[key]));
    const relative = (offset) => `setpoint ${offset < 0 ? "−" : "+"} ${formatRunExtensionTemp(Math.abs(offset))}`;
    const thresholds = [
      [t("runExtension.stop"), Number.isFinite(thresholdsModel.stop) ? formatRunExtensionTemp(thresholdsModel.stop) : relative(thresholdsModel.margin), t("runExtension.stopNote")],
      [t("runExtension.restart"), Number.isFinite(thresholdsModel.restart) ? formatRunExtensionTemp(thresholdsModel.restart) : !hasEntity("phRunExtensionRestartCooldown") ? relative(thresholdsModel.margin - thresholdsModel.cooldown) : "—", t("runExtension.restartNote")],
    ];
    return `
          <p class="oq-run-extension-note">${escapeHtml(t(preview ? "runExtension.preview" : "runExtension.confirmed"))}</p>
          <div class="oq-run-extension-thresholds">
            ${thresholds.map(([label, value, note]) => `<div><span>${escapeHtml(label)}</span><strong>${escapeHtml(value)}</strong><small>${escapeHtml(note)}</small></div>`).join("")}
          </div>
          ${thresholdsModel.limited ? `<p class="oq-run-extension-note">${escapeHtml(t("runExtension.limited", { configured: formatRunExtensionTemp(thresholdsModel.configuredRestart), effective: formatRunExtensionTemp(thresholdsModel.restart) }))}</p>` : ""}
          ${preview ? `<p class="oq-run-extension-note">${escapeHtml(t("runExtension.activeThresholds", { stop: formatRunExtensionTemp(active.stop), restart: formatRunExtensionTemp(active.restart) }))}</p>` : ""}
    `;
  }

  export function patchRunExtensionThresholds() {
    const comfort = state.root?.querySelector("[data-oq-power-house-comfort-thresholds]");
    if (comfort) comfort.innerHTML = renderPowerHouseComfortThresholds();
    const target = state.root?.querySelector("[data-oq-run-extension-thresholds]");
    if (target) target.innerHTML = renderRunExtensionThresholds();
    const status = state.root?.querySelector("[data-oq-run-extension-status]");
    if (status) status.textContent = getRunExtensionCurrentStatus();
  }

  export function renderPowerHouseRunExtensionField() {
    if (!hasEntity("phRunExtension")) return "";
    const enabled = Boolean(getEntityValue("phRunExtension"));
    return `
      <section class="oq-settings-subpanel oq-settings-subpanel--nested oq-run-extension" aria-label="${escapeHtml(t("runExtension.title"))}">
        <div class="oq-run-extension-intro">
          <div class="oq-settings-subpanel-head">
            <h4>${escapeHtml(t("runExtension.title"))}</h4>
            <p>${escapeHtml(t("runExtension.copy"))}</p>
          </div>
          <span class="oq-run-extension-status" data-oq-run-extension-status>${escapeHtml(getRunExtensionCurrentStatus())}</span>
        </div>
        <div class="oq-settings-grid">
          ${renderSettingsSwitchField("phRunExtension", t("runExtension.allow"), t("runExtension.allowCopy"), t("runExtension.on"), t("runExtension.off"))}
          ${enabled ? renderSettingsNumberField("phRunExtensionStopMargin", t("runExtension.margin"), t("runExtension.marginCopy"), "", { footerMarkup: `<p class="oq-run-extension-note">${escapeHtml(t("runExtension.marginDirection"))}</p>` }) : ""}
          ${enabled ? renderSettingsNumberField("phRunExtensionRestartCooldown", t("runExtension.cooldown"), t("runExtension.cooldownCopy"), "", { footerMarkup: `<p class="oq-run-extension-note">${escapeHtml(t("runExtension.cooldownDirection"))}</p>` }) : ""}
        </div>
        ${enabled ? `
          <div class="oq-run-extension-summary" data-oq-run-extension-thresholds>${renderRunExtensionThresholds()}</div>
          <p class="oq-run-extension-note">${escapeHtml(t("runExtension.hysteresis"))}</p>
        ` : ""}
        <p class="oq-run-extension-note">${escapeHtml(t("runExtension.disableCopy"))}</p>
      </section>
    `;
  }

  function getControlledWarmupDisplayModel() {
    const { enabled } = getSettingsSwitchModel("warmupEnabled");
    const active = getEntityValue("warmupActive");
    const status = !enabled ? t("warmup.disabled") :
      t(active === true ? "warmup.warming" : active === false ? "warmup.idle" : "warmup.unknown");
    const value = (key) => {
      const numeric = hasEntity(key) ? parseLooseNumber(getEntityValue(key)) : NaN;
      return Number.isFinite(numeric) ? `${formatNumber(numeric, { minimumFractionDigits: 2, maximumFractionDigits: 2 })} °C` : "—";
    };
    const readings = [
      ["roomTemp", t("warmup.roomTemperature"), value("roomTemp")],
      ["warmupEffectiveTarget", t("warmup.target"), value("warmupEffectiveTarget")],
      ["roomSetpoint", t("warmup.finalTarget"), value("roomSetpoint")],
    ];
    return { status, readings };
  }

  export function patchControlledWarmupField(root) {
    const statusNode = root.querySelector("[data-oq-warmup-status]");
    if (!statusNode) return;
    const { status, readings } = getControlledWarmupDisplayModel();
    if (statusNode.textContent !== status) statusNode.textContent = status;
    readings.forEach(([key, , reading]) => {
      const node = root.querySelector(`[data-oq-warmup-reading="${key}"]`);
      if (node && node.textContent !== reading) node.textContent = reading;
    });
  }

  export function renderControlledWarmupField() {
    if (!hasEntity("warmupEnabled")) return "";
    const { enabled, busy } = getSettingsSwitchModel("warmupEnabled");
    const { status, readings } = getControlledWarmupDisplayModel();
    const switchField = renderSettingsFieldCard("warmupEnabled", t("warmup.allow"), t("warmup.allowCopy"), `
      <div class="oq-settings-compact-switch-field">
        ${renderSettingsCompactSwitchControl("warmupEnabled", t("warmup.allow"), enabled, busy, t("common.on"), t("common.off"))}
        ${renderSettingsSwitchCopy("warmupEnabled", enabled, t("warmup.on"), t("warmup.off"))}
      </div>
    `);
    return `
      <section class="oq-settings-subpanel oq-run-extension" aria-label="${escapeHtml(t("warmup.title"))}">
        <div class="oq-run-extension-intro">
          <div class="oq-settings-subpanel-head">
            <h4>${escapeHtml(t("warmup.title"))}</h4>
            <p>${escapeHtml(t("warmup.copy"))}</p>
          </div>
          <span class="oq-run-extension-status" data-oq-warmup-status>${escapeHtml(status)}</span>
        </div>
        <div class="oq-settings-grid">
          ${switchField}
          ${enabled ? [
            renderSettingsNumberField("warmupTrigger", t("warmup.trigger"), t("warmup.triggerCopy")),
            renderSettingsNumberField("warmupStep", t("warmup.step"), t("warmup.stepCopy")),
            renderSettingsNumberField("warmupStepTime", t("warmup.stepTime"), t("warmup.stepTimeCopy")),
          ].join("") : ""}
        </div>
        ${enabled ? `<div class="oq-run-extension-thresholds oq-warmup-readings">${readings.map(([key, label, reading]) => `<div><span>${escapeHtml(label)}</span><strong data-oq-warmup-reading="${key}">${escapeHtml(reading)}</strong></div>`).join("")}</div>` : ""}
        <p class="oq-run-extension-note">${escapeHtml(t("warmup.limits"))}</p>
      </section>
    `;
  }

  export function renderSettingsHeatPumpLimiterCard(title, hpPrefix) {
    const firstFrequencyKey = `${hpPrefix}ExcludeMinHz`;
    const fields = renderSettingsFrequencyRangeField(
      firstFrequencyKey,
      `${hpPrefix}ExcludeMaxHz`,
      t("settingsHeating.limiterExcludedTitle"),
      t("settingsHeating.limiterExcludedCopy"),
    );

    if (!fields) {
      return "";
    }

    return `
      <article class="oq-settings-hp-group">
        <header>
          <p class="oq-helper-label">${escapeHtml(t("settingsHeating.limiterKicker"))}</p>
          <h4>${escapeHtml(title)}</h4>
          <p>${escapeHtml(t("settingsHeating.limiterCopy"))}</p>
        </header>
        <div class="oq-settings-hp-group-grid">
          ${fields}
        </div>
      </article>
    `;
  }

  export function renderSettingsFlowSection() {
    const flowTuning = renderFlowTuningFields();
    return renderSettingsSection(
      t("settingsHeating.flowSectionGroup"),
      t("settingsHeating.flowSectionTitle"),
      t("settingsHeating.flowSectionCopy"),
      `
        ${renderFlowSettingsFields()}
        ${flowTuning ? `
          ${renderSettingsAdvancedDisclosure(
            "flow",
            t("settingsHeating.flowAdvancedTitle"),
            t("settingsHeating.flowAdvancedCopy"),
            flowTuning,
          )}
        ` : ""}
      `,
    );
  }

  export function renderHeatingEnableStrategyAdvice() {
    if (!hasEntity("heatingEnableSource")) {
      return "";
    }
    const advice = getHeatingEnableAdvice();
    const deviant = Boolean(advice.deviant);
    return `
      <div class="oq-settings-subpanel oq-settings-subpanel--advice${deviant ? " is-warning" : ""}">
        <div class="oq-settings-subpanel-head">
          <p class="oq-helper-label">${escapeHtml(t("settingsHeating.adviceKicker"))}</p>
          <h4>${escapeHtml(t("settingsHeating.adviceTitle"))}</h4>
          <p>${escapeHtml(t("settingsHeating.adviceCopy"))}</p>
        </div>
        <div class="oq-helper-actions">
          <button class="oq-helper-button ${deviant ? "oq-helper-button--warning-soft" : "oq-helper-button--ghost"}" type="button" data-oq-action="open-heating-strategy-advice-modal">${deviant ? '<span class="oq-advice-warn-icon"><svg viewBox="0 0 20 18" aria-hidden="true"><path d="M10 1.6 L18.2 16.4 H1.8 Z"/><rect x="9.1" y="5.4" width="1.8" height="5.8" rx="0.9"/><circle cx="10" cy="13.6" r="1.1"/></svg></span> ' : ""}${escapeHtml(t("settingsHeating.adviceButton"))}</button>
        </div>
      </div>
    `;
  }

  export function renderSettingsHeatingSection() {
    const strategyContent = isCurveMode()
      ? `
        <div class="oq-settings-subpanel">
          <div class="oq-settings-subpanel-head">
            <p class="oq-helper-label">${escapeHtml(t("settingsHeating.heatingCurveKicker"))}</p>
            <h4>${escapeHtml(t("settingsHeating.heatingCurveTitle"))}</h4>
            <p>${escapeHtml(t("settingsHeating.heatingCurveCopy"))}</p>
          </div>
          <div class="oq-settings-grid">
            ${renderHeatingCurveProfileField()}
          </div>
          ${renderSettingsCurveInputs()}
          ${renderHeatingCurveAdvancedFields()}
        </div>
      `
      : `
        <div class="oq-settings-subpanel">
          <div class="oq-settings-subpanel-head">
            <p class="oq-helper-label">Power House</p>
            <h4>Power House</h4>
            <p>${escapeHtml(t("settingsHeating.phouseCopy"))}</p>
          </div>
          ${renderPowerHouseBaseFields()}
        </div>
        ${renderPowerHouseAdvancedField()}
      `;

    return renderSettingsSection(
      t("settingsHeating.sectionGroup"),
      t("settingsHeating.sectionTitle"),
      t("settingsHeating.sectionCopy"),
      `
        ${renderStrategySelectionFields()}
        ${renderHeatingStrategyExplainCards()}
        ${renderHeatingEnableStrategyAdvice()}
        ${strategyContent}
        ${!isCurveMode() ? renderControlledWarmupField() : ""}
      `,
    );
  }
