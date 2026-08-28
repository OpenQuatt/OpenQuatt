import { getEntityNumericValue, hasEntity } from "../core/app-shared.js";
import { CURVE_POINTS, STRATEGY_OPTION_CURVE, STRATEGY_OPTION_POWER_HOUSE } from "../core/config.js";
import { isCurveMode, isManualFlowMode } from "../core/domain-helpers.js";
import { getCurveFallbackSuggestion, getEntityValue, normalizeNumber } from "../core/entity-store.js";
import { getHeatingEnableAdvice, getHeatingEnableCurrent, getHeatingEnableRecommendation } from "../core/heating-strategy-matrix.js";
import { renderNumberInputField } from "../core/number-controls.js";
import { state } from "../core/state.js";
import { renderSettingsAdvancedDisclosure, renderSettingsChoiceOption, renderSettingsFieldCard, renderSettingsFrequencyRangeField, renderSettingsMiniNumberField, renderSettingsNumberField, renderSettingsSection, renderSettingsSelectField } from "./controls.js";
import { formatNumericState } from "../core/formatting.js";
import { escapeHtml } from "../core/html.js";

  export function renderCurveFallbackSuggestionMarkup(helper = false) {
    const suggestion = getCurveFallbackSuggestion();
    if (!suggestion) {
      return "";
    }
    return `
      <div class="oq-curve-fallback-suggest oq-curve-fallback-suggest--inside${helper ? " oq-curve-fallback-suggest--helper" : ""}">
        <div class="oq-curve-fallback-suggest-copy">
          <strong>Suggestie: ${escapeHtml(suggestion.label)}</strong>
          <span>${escapeHtml(suggestion.basis)}</span>
        </div>
        <button
          class="oq-helper-button oq-helper-button--ghost"
          type="button"
          data-oq-action="suggest-curve-fallback"
          ${state.loadingEntities || state.busyAction === "save-curveFallbackSupply" || suggestion.isCurrent ? "disabled" : ""}
        >
          ${suggestion.isCurrent ? "Actief" : "Gebruik suggestie"}
        </button>
      </div>
    `;
  }

  export function renderSettingsCurveInputs() {
    return `
      <div class="oq-settings-curve-grid">
        ${CURVE_POINTS.map((point) => renderSettingsNumberField(point.key, `Aanvoertemp. bij ${point.label}`, `Doelaanvoertemperatuur bij ${point.label} buitentemperatuur.`)).join("")}
        ${renderSettingsNumberField("curveFallbackSupply", "Fallback-aanvoertemperatuur zonder buitentemperatuur", "Aanvoertemperatuur die gebruikt wordt als de buitentemperatuursensor niet beschikbaar is.", "oq-settings-field--curve-fallback-card", { footerMarkup: renderCurveFallbackSuggestionMarkup() })}
      </div>
    `;
  }

  export function renderHeatingCurveAdvancedFields() {
    const fields = [
      renderSettingsNumberField("heatingCurvePidKp", "Proportionele reactie (Kp)", "Bepaalt hoe sterk de regeling direct reageert op het verschil tussen gewenste en gemeten aanvoertemperatuur."),
      renderSettingsNumberField("heatingCurvePidKi", "Langdurige correctie (Ki)", "Corrigeert een klein temperatuurverschil dat langere tijd blijft bestaan. Verhoog alleen in kleine stappen."),
      renderSettingsNumberField("heatingCurvePidKd", "Demping (Kd)", "Remt snelle veranderingen af. Een te hoge waarde kan de regeling onnodig traag of onrustig maken."),
    ].filter(Boolean).join("");

    return renderSettingsAdvancedDisclosure(
      "heating-curve",
      "Geavanceerde stooklijnafstelling",
      "Deze PID-waarden verfijnen de temperatuurcorrectie boven op de stooklijn. Laat ze op de standaardwaarden staan zolang de regeling stabiel reageert.",
      fields ? `<div class="oq-settings-grid oq-settings-grid--pid">${fields}</div>` : "",
    );
  }

  export function renderStrategySelectionFields(className = "oq-settings-grid") {
    return `
      <div class="${escapeHtml(className)}">
        ${renderSettingsSelectField("strategy", "Verwarmingsstrategie", "Kies tussen automatisch regelen met Power House of regelen met een stooklijn.")}
      </div>
    `;
  }

  export function renderFlowSettingsFields(className = "oq-settings-grid") {
    const autoFields = [
      renderSettingsNumberField("flowSetpoint", "Gewenste flow verwarmen", "De flow die OpenQuatt zoveel mogelijk probeert vast te houden buiten koeling."),
      renderSettingsNumberField("coolingFlowSetpoint", "Gewenste flow koelen", "De flow die OpenQuatt gebruikt tijdens actieve koeling."),
    ].filter(Boolean).join("");
    return `
      <div class="${escapeHtml(className)}">
        ${renderSettingsSelectField("flowControlMode", "Regelmodus", "Kies tussen automatische flowregeling en een vaste pompstand.")}
        ${isManualFlowMode()
          ? renderSettingsNumberField("manualIpwm", "Vaste pompstand", "Deze pompstand wordt gebruikt zolang de regeling op handmatig staat.")
          : autoFields}
      </div>
    `;
  }

  export function renderFlowTuningFields(className = "oq-settings-grid") {
    const fields = [
      renderSettingsNumberField("flowKp", "Flow PI Kp", "Hoe sterk de regeling direct reageert op een afwijking."),
      renderSettingsNumberField("flowKi", "Flow PI Ki", "Hoe snel de regeling kleine restfouten wegwerkt."),
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
        ${renderSettingsNumberField("houseColdTemp", "Koude referentietemperatuur", "Bij Quatt is -10 °C de standaard. Samen met het nominale woningvermogen bepaalt deze temperatuur hoe de warmtevraag bij koud weer wordt geschaald.")}
        ${renderSettingsNumberField("houseOutdoorMax", "Maximum heating outdoor temperature", "Bij deze buitentemperatuur is verwarmen meestal niet meer nodig.")}
        ${renderSettingsNumberField("housePower", "Nominaal woningvermogen", "Hoeveel warmte je woning ongeveer nodig heeft bij de koude referentietemperatuur hierboven.")}
        ${renderPowerHouseResponseProfilesField()}
      </div>
    `;
  }

  export function renderHeatingStrategyExplainCards() {
    const curveActive = isCurveMode();
    return `
      <div class="oq-settings-strategy-grid">
        <button
          class="oq-helper-surface oq-settings-strategy-card${curveActive ? "" : " is-active"}"
          type="button"
          data-oq-action="select-settings-option"
          data-select-key="strategy"
          data-select-option="${escapeHtml(STRATEGY_OPTION_POWER_HOUSE)}"
          aria-pressed="${curveActive ? "false" : "true"}"
          ${state.loadingEntities || state.busyAction === "save-strategy" || state.busyAction === "save-heatingEnableSource" ? "disabled" : ""}
        >
          <p class="oq-helper-label">Power House</p>
          <h4>Automatisch op basis van je woning</h4>
          <p>Power House schat hoeveel warmte je woning nodig heeft. Dit is meestal de beste keuze als je zonder veel finetuning wilt starten.</p>
          <ul class="oq-settings-strategy-points">
            <li>Gebruikt vooral het geschatte warmteverlies van je woning en de buitentemperatuur waarbij verwarmen meestal niet meer nodig is.</li>
            <li>Reageert meer op het gedrag van je woning dan op een vaste temperatuurcurve.</li>
            <li>Handig als je vooral comfort wilt en zo min mogelijk handmatig wilt instellen.</li>
          </ul>
        </button>
        <button
          class="oq-helper-surface oq-settings-strategy-card${curveActive ? " is-active" : ""}"
          type="button"
          data-oq-action="select-settings-option"
          data-select-key="strategy"
          data-select-option="${escapeHtml(STRATEGY_OPTION_CURVE)}"
          aria-pressed="${curveActive ? "true" : "false"}"
          ${state.loadingEntities || state.busyAction === "save-strategy" || state.busyAction === "save-heatingEnableSource" ? "disabled" : ""}
        >
          <p class="oq-helper-label">Stooklijn</p>
          <h4>Regelen met een stooklijn</h4>
          <p>Met een stooklijn kies je per buitentemperatuur welke aanvoertemperatuur nodig is. Handig als je dit bewust zelf wilt instellen.</p>
          <ul class="oq-settings-strategy-points">
            <li>Gebruikt de curvepunten van <strong>-20°C t/m 15°C</strong> als basis.</li>
            <li>Voelt herkenbaar voor wie gewend is aan een klassieke stooklijn.</li>
            <li>Handig als je de aanvoertemperatuur per buitentemperatuur zelf wilt finetunen.</li>
          </ul>
        </button>
      </div>
    `;
  }

  export function renderPowerHouseResponseProfilesField() {
    if (!hasEntity("phResponseProfile")) {
      return "";
    }

    const currentValue = String(getEntityValue("phResponseProfile") || "");
    const busy = state.loadingEntities || state.busyAction === "save-phResponseProfile";
    const options = [
      {
        value: "Calm",
        label: "Rustig",
        rise: "12 min",
        fall: "5 min",
        meta: "Opbouw 12 min · Afbouw 5 min",
        copy: "Reageert minder snel op schommelingen. Fijn voor vloerverwarming of een woning die traag opwarmt en afkoelt.",
      },
      {
        value: "Balanced",
        label: "Gebalanceerd",
        rise: "8 min",
        fall: "3 min",
        meta: "Opbouw 8 min · Afbouw 3 min",
        copy: "Goede middenweg tussen comfort en rust. Meestal het beste startpunt voor dagelijks gebruik.",
      },
      {
        value: "Responsive",
        label: "Direct",
        rise: "5 min",
        fall: "2 min",
        meta: "Opbouw 5 min · Afbouw 2 min",
        copy: "Reageert sneller op veranderende warmtevraag. Handig als je woning snel afkoelt of je sneller effect wilt zien.",
      },
      {
        value: "Custom",
        label: "Aangepast",
        rise: "Vrij",
        fall: "Instelbaar",
        meta: "Opbouw en afbouw instelbaar",
        copy: "Stel zelf in hoe snel de regeling op- en afbouwt. Handig als de standaardprofielen net niet goed passen.",
      },
    ];
    const controlMarkup = `
      <div class="oq-settings-choice-grid oq-settings-choice-grid--response">
        ${options.map((option) => {
          const isActive = option.value === currentValue;
          if (option.value === "Custom" && isActive) {
            return `
              <div class="oq-helper-surface oq-settings-choice-card oq-settings-choice-card--static oq-settings-choice-card--custom is-active">
                <span class="oq-settings-choice-title">${escapeHtml(option.label)}</span>
                <div class="oq-settings-choice-meta">
                  <span class="oq-settings-choice-meta-text">${escapeHtml(option.meta)}</span>
                </div>
                <span class="oq-settings-choice-copy">${escapeHtml(option.copy)}</span>
                <div class="oq-settings-choice-inline-grid oq-settings-choice-inline-grid--inside-card">
                  ${renderSettingsMiniNumberField("phDemandRiseTime", "Opbouwtijd", "Tijd waarmee de warmtevraag bij oplopende vraag naar het nieuwe niveau toeloopt.", { compact: true, showCopy: false, infoId: "phDemandRiseTime-inline", embedded: true })}
                  ${renderSettingsMiniNumberField("phDemandFallTime", "Afbouwtijd", "Tijd waarmee de warmtevraag bij afnemende vraag weer terugzakt.", { compact: true, showCopy: false, infoId: "phDemandFallTime-inline", embedded: true })}
                </div>
              </div>
            `;
          }
          return renderSettingsChoiceOption({ key: "phResponseProfile", option: option.value, currentValue, busy, copy: option.copy, meta: option.meta });
        }).join("")}
      </div>
    `;

    return renderSettingsFieldCard(
      "phResponseProfile",
      "Power House responsprofiel",
      "Kies hoe rustig of direct Power House mag reageren op veranderingen in je woning.",
      controlMarkup,
      "oq-settings-field--span-2",
    );
  }

  export function renderHeatingCurveProfileField() {
    if (!hasEntity("curveControlProfile")) {
      return "";
    }

    const currentValue = String(getEntityValue("curveControlProfile") || "");
    const busy = state.loadingEntities || state.busyAction === "save-curveControlProfile";
    const options = [
      {
        value: "Comfort",
        label: "Comfort",
        meta: "Eerder starten · Fijner trimmen",
        copy: "Reageert wat actiever en laat de aanvoertemperatuur eerder oplopen. Fijn als je vooral comfort wilt.",
      },
      {
        value: "Balanced",
        label: "Gebalanceerd",
        meta: "Middenweg · Voorspelbaar gedrag",
        copy: "De standaard middenweg voor dagelijks gebruik. Voorspelbaar en tegelijk vlot genoeg.",
      },
      {
        value: "Stable",
        label: "Stabiel",
        meta: "Meer filtering · Rustigere stappen",
        copy: "Reageert rustiger en stuurt minder snel bij. Fijn als je zo min mogelijk schommelingen wilt.",
      },
    ];

    const controlMarkup = `
      <div class="oq-settings-choice-grid oq-settings-choice-grid--curve">
        ${options.map((option) => renderSettingsChoiceOption({ key: "curveControlProfile", option: option.value, currentValue, busy, copy: option.copy, meta: option.meta })).join("")}
      </div>
    `;

    return renderSettingsFieldCard(
      "curveControlProfile",
      "Regelprofiel",
      "Kies of de stooklijn vooral comfortabel, gebalanceerd of rustig moet reageren.",
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
          <p class="oq-ph-concept-kicker">Kamercorrectie op Power House-huisvraag</p>
          <div class="oq-ph-concept-caption">
            Conceptueel: deze grafiek toont de kamercorrectie boven op de berekende Power House-huisvraag. Onder de comfortgrens loopt die correctie op, binnen de comfortband blijft de directe reactie vlak terwijl opgebouwde comfort memory nog kan doorwerken, en boven de bovengrens start warme tegensturing.
          </div>
          <div class="oq-ph-concept-meta">
            <span class="oq-ph-concept-meta-pill">Setpoint <strong>${escapeHtml(formatNumericState(exampleSetpoint, 1, "°C"))}</strong></span>
            <span class="oq-ph-concept-meta-pill">Comfortband <strong>${escapeHtml(formatNumericState(quietMin, 1, "°C"))} – ${escapeHtml(formatNumericState(quietMax, 1, "°C"))}</strong></span>
            <span class="oq-ph-concept-meta-pill">Temperatuurreactie <strong>${escapeHtml(formatNumericState(temperatureReaction, 0, " W/K"))}</strong></span>
          </div>
          <svg class="oq-ph-concept-svg" viewBox="0 0 ${width} ${height}" role="img" aria-label="Grafiek voor Power House tuning">
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
            ${showQuietMinTick ? renderConceptTooltip(quietMinX, "Comfort onder setpoint", formatNumericState(quietMin, 1, "°C"), "below") : ""}
            ${renderConceptTooltip(setpointX, "Setpoint", formatNumericState(exampleSetpoint, 1, "°C"), "setpoint")}
            ${showQuietMaxTick ? renderConceptTooltip(quietMaxX, "Comfort boven setpoint", formatNumericState(quietMax, 1, "°C"), "above") : ""}

            <text x="${leftX + 8}" y="${top + 18}" text-anchor="start" class="oq-ph-concept-label oq-ph-concept-label--heat">meer warmte</text>
            <text x="${leftX + 8}" y="${height - bottom - 8}" text-anchor="start" class="oq-ph-concept-label">minder warmte</text>
            <text x="${leftX}" y="${height - 26}" text-anchor="start" class="oq-ph-concept-label">kouder</text>
            <text x="${rightX}" y="${height - 26}" text-anchor="end" class="oq-ph-concept-label">warmer</text>

            ${showQuietMinTick ? `<text x="${quietMinX - 5}" y="${height - 14}" text-anchor="end" class="oq-ph-concept-tick-value">${escapeHtml(formatNumericState(quietMin, 1, "°C"))}</text>` : ""}
            <text x="${setpointX}" y="${height - 14}" text-anchor="middle" class="oq-ph-concept-tick-value oq-ph-concept-tick-value--setpoint">${escapeHtml(formatNumericState(exampleSetpoint, 1, "°C"))}</text>
            ${showQuietMaxTick ? `<text x="${quietMaxX + 5}" y="${height - 14}" text-anchor="start" class="oq-ph-concept-tick-value">${escapeHtml(formatNumericState(quietMax, 1, "°C"))}</text>` : ""}
          </svg>
        </div>
        <div class="oq-ph-concept-zones">
          <span class="oq-ph-concept-zone-chip oq-ph-concept-zone-chip--below">
            <span class="oq-ph-concept-zone-chip-label">extra opwarming</span>
            <span class="oq-ph-concept-zone-chip-meta">onder ${escapeHtml(formatNumericState(quietMin, 1, "°C"))}</span>
          </span>
          <span class="oq-ph-concept-zone-chip oq-ph-concept-zone-chip--calm">
            <span class="oq-ph-concept-zone-chip-label">comfortband</span>
            <span class="oq-ph-concept-zone-chip-meta">${escapeHtml(formatNumericState(quietMin, 1, "°C"))} – ${escapeHtml(formatNumericState(quietMax, 1, "°C"))}</span>
          </span>
          <span class="oq-ph-concept-zone-chip oq-ph-concept-zone-chip--above">
            <span class="oq-ph-concept-zone-chip-label">warme tegensturing</span>
            <span class="oq-ph-concept-zone-chip-meta">boven ${escapeHtml(formatNumericState(quietMax, 1, "°C"))}</span>
          </span>
        </div>
        <div class="oq-ph-concept-notes">
          <article class="oq-ph-concept-note">
            <span class="oq-ph-concept-note-title">Comfort onder</span>
            <p>Bepaalt wanneer extra opwarming begint onder het setpoint.</p>
          </article>
          <article class="oq-ph-concept-note">
            <span class="oq-ph-concept-note-title">Comfortband</span>
            <p>Binnen deze band blijft de directe temperatuurreactie vlak. Een opgebouwde comfort memory kan hier nog wel even doorwerken en loopt daarna rustig af.</p>
          </article>
          <article class="oq-ph-concept-note">
            <span class="oq-ph-concept-note-title">Temperatuurreactie</span>
            <p>Bepaalt hoe sterk Power House buiten de comfortband extra of minder warmtevraag als kamercorrectie toevoegt boven op de berekende huisvraag.</p>
          </article>
        </div>
      </div>
    `;
  }

  export function renderPowerHouseAdvancedField() {
    const fields = [
      renderSettingsNumberField("phKp", "Temperatuurreactie", "Bepaalt hoe sterk Power House kamertemperatuurafwijking vertaalt naar extra of minder warmtevraag in W/K. Hogere waarden reageren steviger, lagere waarden rustiger.", "", { unitOverride: "W/K" }),
      renderSettingsNumberField("phComfortBelow", "Comfort onder setpoint", "Extra comfortmarge onder het setpoint. Hiermee kan Power House iets sneller warmte vragen als de kamertemperatuur merkbaar onder het doel zakt."),
      renderSettingsNumberField("phComfortAbove", "Comfort boven setpoint", "Bovenmarge rond het setpoint. Hiermee bepaal je hoeveel ruimte er boven het setpoint mag ontstaan voordat warme tegensturing begint."),
    ].filter(Boolean);

    if (!fields.length) {
      return "";
    }

    return `
      <div class="oq-settings-subpanel oq-settings-subpanel--nested">
        <div class="oq-settings-subpanel-head">
          <p class="oq-helper-label">Power House tuning</p>
          <h4>Geavanceerde Power House tuning</h4>
          <p>Met deze instellingen verfijn je hoe Power House reageert rond het kamersetpoint. De grafiek hierboven laat meteen zien wat dat betekent.</p>
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
      "Uitgesloten frequentiebereik",
      "OpenQuatt slaat alle compressorfrequenties binnen dit bereik over, bij verwarmen en koelen.",
    );

    if (!fields) {
      return "";
    }

    return `
      <article class="oq-settings-hp-group">
        <header>
          <p class="oq-helper-label">Warmtepomp</p>
          <h4>${escapeHtml(title)}</h4>
          <p>Kies één frequentiebereik dat OpenQuatt bij verwarmen en koelen moet overslaan.</p>
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
      "Installatie",
      "Flowregeling",
      "Kies hoe de pomp wordt geregeld en stel de flow-instellingen direct als installatieparameter in. De autotune vind je later bij Service & commissioning.",
      `
        ${renderFlowSettingsFields()}
        ${flowTuning ? `
          ${renderSettingsAdvancedDisclosure(
            "flow",
            "Geavanceerde flow-afstelling",
            "Kp en Ki bepalen hoe stevig de flowregeling corrigeert. Gebruik bij voorkeur eerst de autotune onder Service & commissioning en wijzig daarna alleen in kleine stappen.",
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
          <p class="oq-helper-label">Warmtetoestemming</p>
          <h4>Welke warmtetoestemming past bij je strategie?</h4>
          <p>Power House bepaalt zelf de vraag; bij stooklijn bepaalt de thermostaat of er verwarmd wordt. Open de overwegingen en aanbevelingen per strategie.</p>
        </div>
        <div class="oq-helper-actions">
          <button class="oq-helper-button ${deviant ? "oq-helper-button--warning-soft" : "oq-helper-button--ghost"}" type="button" data-oq-action="open-heating-strategy-advice-modal">${deviant ? '<span class="oq-advice-warn-icon"><svg viewBox="0 0 20 18" aria-hidden="true"><path d="M10 1.6 L18.2 16.4 H1.8 Z"/><rect x="9.1" y="5.4" width="1.8" height="5.8" rx="0.9"/><circle cx="10" cy="13.6" r="1.1"/></svg></span> Advies per strategie bekijken' : "Advies per strategie bekijken"}</button>
        </div>
      </div>
    `;
  }

  export function renderSettingsHeatingSection() {
    const strategyContent = isCurveMode()
      ? `
        <div class="oq-settings-subpanel">
          <div class="oq-settings-subpanel-head">
            <p class="oq-helper-label">Stooklijn</p>
            <h4>Stooklijn</h4>
            <p>Stel hier je stooklijn in en kies wat OpenQuatt moet doen als er geen buitentemperatuur beschikbaar is.</p>
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
            <p>Met deze waarden schat OpenQuatt hoeveel warmte je woning nodig heeft. Heb je deze gegevens van Quatt, dan kun je ze hier als startpunt gebruiken.</p>
          </div>
          ${renderPowerHouseBaseFields()}
          ${renderPowerHouseAdvancedField()}
        </div>
      `;

    return renderSettingsSection(
      "Regeling",
      "Verwarmingsstrategie",
      "Kies hier hoe OpenQuatt je verwarming regelt. De instellingen hieronder passen zich automatisch aan.",
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
          <h3>Stooklijn-editor</h3>
          <p>Stel de verwarmingscurve in door de punten te verslepen en zo de zes vereiste aanvoertemperaturen te bepalen.</p>
        </div>
        <svg class="oq-helper-curve-svg" viewBox="0 0 ${width} ${height}" role="img" aria-label="Stooklijn-editor">
          ${gridLines}
          <polyline points="${linePoints}" class="oq-helper-curve-line" />
          ${circles}
          ${xLabels}
        </svg>
      </div>
    `;
  }
