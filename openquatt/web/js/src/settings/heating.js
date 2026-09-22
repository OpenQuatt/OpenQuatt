import { getEntityNumericValue, hasEntity } from "../core/app-shared.js";
import { CURVE_POINTS, STRATEGY_OPTION_CURVE, STRATEGY_OPTION_POWER_HOUSE } from "../core/config.js";
import { isCurveMode, isManualFlowMode } from "../core/domain-helpers.js";
import { getCurveFallbackSuggestion, getEntityValue, normalizeNumber } from "../core/entity-store.js";
import { getHeatingEnableAdvice } from "../core/heating-strategy-matrix.js";
import { state } from "../core/state.js";
import { getSettingsSelectModel } from "./field-models.js";
import { renderSettingsAdvancedDisclosure, renderSettingsChoiceOption, renderSettingsFieldCard, renderSettingsFrequencyRangeField, renderSettingsMiniNumberField, renderSettingsNumberField, renderSettingsSection, renderSettingsSelectField } from "./controls.js";
import { formatNumericState } from "../core/formatting.js";
import { escapeHtml } from "../core/html.js";
import { t } from "../i18n/index.js";

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
    return `
      <div class="oq-settings-curve-grid">
        ${CURVE_POINTS.map((point) => renderSettingsNumberField(point.key, t("settingsHeating.curvePointTitle", { label: point.label }), t("settingsHeating.curvePointCopy", { label: point.label }))).join("")}
        ${renderSettingsNumberField("curveFallbackSupply", t("settingsHeating.fallbackTitle"), t("settingsHeating.fallbackCopy"), "oq-settings-field--curve-fallback-card", { footerMarkup: renderCurveFallbackSuggestionMarkup() })}
      </div>
    `;
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
        ${renderPowerHouseResponseProfilesField()}
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

  export function renderPowerHouseConceptGraphic() {
    const safe = (key, fallback = 0) => {
      const numeric = getEntityNumericValue(key);
      return Number.isNaN(numeric) ? fallback : Math.max(0, numeric);
    };
    const exampleSetpoint = 20;
    const comfortBelow = safe("phComfortBelow", 0.1);
    const comfortAbove = safe("phComfortAbove", 0.3);
    const temperatureReaction = safe("phKp", 3000);

    const quietMin = exampleSetpoint - comfortBelow;
    const quietMax = exampleSetpoint + comfortAbove;

    const width = 620;
    const height = 184;
    const left = 46;
    const right = 24;
    const top = 18;
    const bottom = 40;
    const axisY = 96;
    const plotWidth = width - left - right;
    const minTemp = Math.min(exampleSetpoint - 1.2, quietMin - 0.35);
    const maxTemp = Math.max(exampleSetpoint + 1.2, quietMax + 0.35);
    const toX = (temp) => left + ((temp - minTemp) / Math.max(0.01, maxTemp - minTemp)) * plotWidth;

    const leftX = toX(minTemp);
    const rightX = toX(maxTemp);
    const quietMinX = toX(quietMin);
    const setpointX = toX(exampleSetpoint);
    const quietMaxX = toX(quietMax);
    const showQuietMinTick = Math.abs(quietMin - exampleSetpoint) > 0.001;
    const showQuietMaxTick = Math.abs(quietMax - exampleSetpoint) > 0.001;
    const curveTopY = top + 24;
    const curveBottomY = height - bottom;
    const tooltipY = axisY - 44;
    const renderConceptTooltip = (x, kicker, detail, modifier = "") => {
      const width = 110;
      const height = 36;
      const tooltipX = Math.max(leftX + 4, Math.min(rightX - width - 4, x - width / 2));
      const hitX = x - 14;
      const hitY = tooltipY;
      const hitWidth = 28;
      const hitHeight = axisY - tooltipY + 16;
      return `
        <g class="oq-ph-concept-hotspot" tabindex="0" role="img" aria-label="${escapeHtml(`${kicker} ${detail}`)}">
          <rect class="oq-ph-concept-hit" x="${hitX}" y="${hitY}" width="${hitWidth}" height="${hitHeight}" rx="10"></rect>
          <circle class="oq-ph-concept-hit" cx="${x}" cy="${axisY}" r="14"></circle>
          <g class="oq-ph-concept-tooltip${modifier ? ` oq-ph-concept-tooltip--${modifier}` : ""}" transform="translate(${tooltipX} ${tooltipY})">
            <rect class="oq-ph-concept-tooltip-panel" width="${width}" height="${height}" rx="10"></rect>
            <text x="${width / 2}" y="14" text-anchor="middle" class="oq-ph-concept-tooltip-kicker">${escapeHtml(kicker)}</text>
            <text x="${width / 2}" y="27" text-anchor="middle" class="oq-ph-concept-tooltip-detail">${escapeHtml(detail)}</text>
          </g>
        </g>
      `;
    };
    const linePath = [
      `M ${leftX.toFixed(1)} ${curveTopY.toFixed(1)}`,
      `L ${quietMinX.toFixed(1)} ${axisY.toFixed(1)}`,
      `L ${quietMaxX.toFixed(1)} ${axisY.toFixed(1)}`,
      `L ${rightX.toFixed(1)} ${curveBottomY.toFixed(1)}`,
    ].join(" ");

    return `
      <div class="oq-ph-concept-card">
        <div class="oq-ph-concept-visual">
          <p class="oq-ph-concept-kicker">${escapeHtml(t("settingsHeating.conceptKicker"))}</p>
          <div class="oq-ph-concept-caption">
            ${escapeHtml(t("settingsHeating.conceptCaption"))}
          </div>
          <div class="oq-ph-concept-meta">
            <span class="oq-ph-concept-meta-pill">${escapeHtml(t("settingsHeating.conceptSetpoint"))} <strong>${escapeHtml(formatNumericState(exampleSetpoint, 1, "°C"))}</strong></span>
            <span class="oq-ph-concept-meta-pill">${escapeHtml(t("settingsHeating.conceptComfortBand"))} <strong>${escapeHtml(formatNumericState(quietMin, 1, "°C"))} – ${escapeHtml(formatNumericState(quietMax, 1, "°C"))}</strong></span>
            <span class="oq-ph-concept-meta-pill">${escapeHtml(t("settingsHeating.conceptReaction"))} <strong>${escapeHtml(formatNumericState(temperatureReaction, 0, " W/K"))}</strong></span>
          </div>
          <svg class="oq-ph-concept-svg" viewBox="0 0 ${width} ${height}" role="img" aria-label="${escapeHtml(t("settingsHeating.conceptAria"))}">
            <rect x="${leftX.toFixed(1)}" y="${top}" width="${Math.max(20, quietMinX - leftX).toFixed(1)}" height="${(height - top - bottom).toFixed(1)}" rx="18" class="oq-ph-concept-band oq-ph-concept-band--below"></rect>
            <rect x="${quietMinX.toFixed(1)}" y="${top}" width="${Math.max(20, quietMaxX - quietMinX).toFixed(1)}" height="${(height - top - bottom).toFixed(1)}" rx="18" class="oq-ph-concept-band oq-ph-concept-band--calm"></rect>
            <rect x="${quietMaxX.toFixed(1)}" y="${top}" width="${Math.max(20, rightX - quietMaxX).toFixed(1)}" height="${(height - top - bottom).toFixed(1)}" rx="18" class="oq-ph-concept-band oq-ph-concept-band--above"></rect>

            <line x1="${leftX}" y1="${top}" x2="${leftX}" y2="${height - bottom}" class="oq-ph-concept-axis"></line>
            <line x1="${leftX}" y1="${axisY}" x2="${rightX}" y2="${axisY}" class="oq-ph-concept-axis"></line>
            <line x1="${setpointX}" y1="${top}" x2="${setpointX}" y2="${height - bottom}" class="oq-ph-concept-axis oq-ph-concept-axis--vertical"></line>

            <path d="${linePath}" class="oq-ph-concept-curve"></path>

            ${showQuietMinTick ? `<line x1="${quietMinX}" y1="${axisY - 12}" x2="${quietMinX}" y2="${axisY + 12}" class="oq-ph-concept-marker oq-ph-concept-marker--below"></line>` : ""}
            <line x1="${setpointX}" y1="${axisY - 14}" x2="${setpointX}" y2="${axisY + 14}" class="oq-ph-concept-marker oq-ph-concept-marker--setpoint"></line>
            ${showQuietMaxTick ? `<line x1="${quietMaxX}" y1="${axisY - 12}" x2="${quietMaxX}" y2="${axisY + 12}" class="oq-ph-concept-marker oq-ph-concept-marker--above"></line>` : ""}
            ${showQuietMinTick ? `<circle cx="${quietMinX}" cy="${axisY}" r="5" class="oq-ph-concept-point oq-ph-concept-point--below"></circle>` : ""}
            <circle cx="${setpointX}" cy="${axisY}" r="6" class="oq-ph-concept-point oq-ph-concept-point--setpoint"></circle>
            ${showQuietMaxTick ? `<circle cx="${quietMaxX}" cy="${axisY}" r="5" class="oq-ph-concept-point oq-ph-concept-point--above"></circle>` : ""}
            ${showQuietMinTick ? renderConceptTooltip(quietMinX, t("settingsHeating.conceptComfortBelow"), formatNumericState(quietMin, 1, "°C"), "below") : ""}
            ${renderConceptTooltip(setpointX, t("settingsHeating.conceptSetpoint"), formatNumericState(exampleSetpoint, 1, "°C"), "setpoint")}
            ${showQuietMaxTick ? renderConceptTooltip(quietMaxX, t("settingsHeating.conceptComfortAbove"), formatNumericState(quietMax, 1, "°C"), "above") : ""}

            <text x="${leftX + 8}" y="${top + 18}" text-anchor="start" class="oq-ph-concept-label oq-ph-concept-label--heat">${escapeHtml(t("settingsHeating.conceptMoreHeat"))}</text>
            <text x="${leftX + 8}" y="${height - bottom - 8}" text-anchor="start" class="oq-ph-concept-label">${escapeHtml(t("settingsHeating.conceptLessHeat"))}</text>
            <text x="${leftX}" y="${height - 26}" text-anchor="start" class="oq-ph-concept-label">${escapeHtml(t("settingsHeating.conceptColder"))}</text>
            <text x="${rightX}" y="${height - 26}" text-anchor="end" class="oq-ph-concept-label">${escapeHtml(t("settingsHeating.conceptWarmer"))}</text>

            ${showQuietMinTick ? `<text x="${quietMinX - 5}" y="${height - 14}" text-anchor="end" class="oq-ph-concept-tick-value">${escapeHtml(formatNumericState(quietMin, 1, "°C"))}</text>` : ""}
            <text x="${setpointX}" y="${height - 14}" text-anchor="middle" class="oq-ph-concept-tick-value oq-ph-concept-tick-value--setpoint">${escapeHtml(formatNumericState(exampleSetpoint, 1, "°C"))}</text>
            ${showQuietMaxTick ? `<text x="${quietMaxX + 5}" y="${height - 14}" text-anchor="start" class="oq-ph-concept-tick-value">${escapeHtml(formatNumericState(quietMax, 1, "°C"))}</text>` : ""}
          </svg>
        </div>
        <div class="oq-ph-concept-zones">
          <span class="oq-ph-concept-zone-chip oq-ph-concept-zone-chip--below">
            <span class="oq-ph-concept-zone-chip-label">${escapeHtml(t("settingsHeating.zoneBelow"))}</span>
            <span class="oq-ph-concept-zone-chip-meta">${escapeHtml(t("settingsHeating.zoneBelowMeta", { value: formatNumericState(quietMin, 1, "°C") }))}</span>
          </span>
          <span class="oq-ph-concept-zone-chip oq-ph-concept-zone-chip--calm">
            <span class="oq-ph-concept-zone-chip-label">${escapeHtml(t("settingsHeating.zoneCalm"))}</span>
            <span class="oq-ph-concept-zone-chip-meta">${escapeHtml(formatNumericState(quietMin, 1, "°C"))} – ${escapeHtml(formatNumericState(quietMax, 1, "°C"))}</span>
          </span>
          <span class="oq-ph-concept-zone-chip oq-ph-concept-zone-chip--above">
            <span class="oq-ph-concept-zone-chip-label">${escapeHtml(t("settingsHeating.zoneAbove"))}</span>
            <span class="oq-ph-concept-zone-chip-meta">${escapeHtml(t("settingsHeating.zoneAboveMeta", { value: formatNumericState(quietMax, 1, "°C") }))}</span>
          </span>
        </div>
        <div class="oq-ph-concept-notes">
          <article class="oq-ph-concept-note">
            <span class="oq-ph-concept-note-title">${escapeHtml(t("settingsHeating.noteComfortBelowTitle"))}</span>
            <p>${escapeHtml(t("settingsHeating.noteComfortBelowCopy"))}</p>
          </article>
          <article class="oq-ph-concept-note">
            <span class="oq-ph-concept-note-title">${escapeHtml(t("settingsHeating.noteBandTitle"))}</span>
            <p>${escapeHtml(t("settingsHeating.noteBandCopy"))}</p>
          </article>
          <article class="oq-ph-concept-note">
            <span class="oq-ph-concept-note-title">${escapeHtml(t("settingsHeating.noteReactionTitle"))}</span>
            <p>${escapeHtml(t("settingsHeating.noteReactionCopy"))}</p>
          </article>
        </div>
      </div>
    `;
  }

  export function renderPowerHouseAdvancedField() {
    const fields = [
      renderSettingsNumberField("phKp", t("settingsHeating.phKpTitle"), t("settingsHeating.phKpCopy"), "", { unitOverride: "W/K" }),
      renderSettingsNumberField("phComfortBelow", t("settingsHeating.phComfortBelowTitle"), t("settingsHeating.phComfortBelowCopy")),
      renderSettingsNumberField("phComfortAbove", t("settingsHeating.phComfortAboveTitle"), t("settingsHeating.phComfortAboveCopy")),
    ].filter(Boolean);

    if (!fields.length) {
      return "";
    }

    return `
      <div class="oq-settings-subpanel oq-settings-subpanel--nested">
        <div class="oq-settings-subpanel-head">
          <p class="oq-helper-label">${escapeHtml(t("settingsHeating.tuningKicker"))}</p>
          <h4>${escapeHtml(t("settingsHeating.tuningTitle"))}</h4>
          <p>${escapeHtml(t("settingsHeating.tuningCopy"))}</p>
        </div>
        ${renderPowerHouseConceptGraphic()}
        <div class="oq-settings-grid">
          ${fields.join("")}
        </div>
      </div>
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
          <div class="oq-settings-curve-shell">
            ${renderCurveGraph()}
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
          ${renderPowerHouseAdvancedField()}
        </div>
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
      `,
    );
  }

  export function renderCurveGraph() {
    const width = 560;
    const height = 240;
    const margin = { top: 22, right: 18, bottom: 38, left: 34 };
    const plotWidth = width - margin.left - margin.right;
    const plotHeight = height - margin.top - margin.bottom;
    const xMin = CURVE_POINTS[0].outdoor;
    const xMax = CURVE_POINTS[CURVE_POINTS.length - 1].outdoor;

    const toX = (temp) => margin.left + ((temp - xMin) / (xMax - xMin)) * plotWidth;
    const toY = (value) => margin.top + ((70 - value) / 50) * plotHeight;

    const gridLines = [20, 30, 40, 50, 60, 70]
      .map((value) => {
        const y = toY(value);
        return `
          <line x1="${margin.left}" y1="${y}" x2="${width - margin.right}" y2="${y}" class="oq-helper-curve-grid" />
          <text x="8" y="${y + 4}" class="oq-helper-curve-axis-label">${value}°</text>
        `;
      })
      .join("");

    const xLabels = CURVE_POINTS
      .map((point) => `
        <text x="${toX(point.outdoor)}" y="${height - 12}" text-anchor="middle" class="oq-helper-curve-axis-label">${escapeHtml(point.label)}</text>
      `)
      .join("");

    const linePoints = CURVE_POINTS
      .map((point) => `${toX(point.outdoor)},${toY(normalizeNumber(point.key, getEntityValue(point.key)))}`)
      .join(" ");

    const circles = CURVE_POINTS
      .map((point) => {
        const value = normalizeNumber(point.key, getEntityValue(point.key));
        return `
          <g>
            <circle
              cx="${toX(point.outdoor)}"
              cy="${toY(value)}"
              r="7"
              class="oq-helper-curve-point ${state.draggingCurveKey === point.key ? "is-dragging" : ""}"
              data-curve-key="${escapeHtml(point.key)}"
            />
            <text x="${toX(point.outdoor)}" y="${toY(value) - 14}" text-anchor="middle" class="oq-helper-curve-point-label">${value.toFixed(1)}°</text>
          </g>
        `;
      })
      .join("");

    return `
      <div class="oq-helper-curve-shell">
        <div class="oq-helper-curve-copy">
          <h3>${escapeHtml(t("settingsHeating.curveEditorTitle"))}</h3>
          <p>${escapeHtml(t("settingsHeating.curveEditorCopy"))}</p>
        </div>
        <svg class="oq-helper-curve-svg" viewBox="0 0 ${width} ${height}" role="img" aria-label="${escapeHtml(t("settingsHeating.curveEditorAria"))}">
          ${gridLines}
          <polyline points="${linePoints}" class="oq-helper-curve-line" />
          ${circles}
          ${xLabels}
        </svg>
      </div>
    `;
  }
