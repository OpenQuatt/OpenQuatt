import { hasEntity, isEntityActive } from "../core/app-shared.js";
import { STRATEGY_OPTION_CURVE, STRATEGY_OPTION_POWER_HOUSE } from "../core/config.js";
import { getInputDraftValue } from "../core/control-drafts.js";
import { formatValue, getEntityValue, getNumberMeta, normalizeNumber, parseLooseNumber, toTimeInputValue } from "../core/entity-store.js";
import { escapeHtml } from "../core/html.js";
import { renderNumberInputControl } from "../core/number-controls.js";
import { state } from "../core/state.js";

export function renderSettingsInfoToggle(infoId, title, copy, buttonLabel = "i", className = "") {
  if (!copy) {
    return "";
  }

  return `
    <div class="oq-settings-info${className ? ` ${escapeHtml(className)}` : ""}${state.settingsInfoOpen === infoId ? " is-open" : ""}" data-oq-settings-info="${escapeHtml(infoId)}">
      <button
        class="oq-settings-info-button"
        type="button"
        data-oq-action="toggle-settings-info"
        data-info-id="${escapeHtml(infoId)}"
        aria-label="${escapeHtml(`Uitleg bij ${title}`)}"
        aria-expanded="${state.settingsInfoOpen === infoId ? "true" : "false"}"
      >${escapeHtml(buttonLabel)}</button>
      <div class="oq-settings-info-popover" ${state.settingsInfoOpen === infoId ? "" : "hidden"}>
        <p>${escapeHtml(copy)}</p>
      </div>
    </div>
  `;
}

export function renderSettingsFieldCard(fieldKey, title, copy, controlMarkup, className = "", footerMarkup = "", headAction = "") {
  return `<article class="oq-helper-surface oq-settings-field${className ? ` ${className}` : ""}" data-oq-settings-field="${escapeHtml(fieldKey)}"><div class="oq-settings-field-head"><h3>${escapeHtml(title)}</h3>${headAction}${renderSettingsInfoToggle(fieldKey, title, copy)}</div><div class="oq-settings-field-control">${controlMarkup}</div>${footerMarkup}</article>`;
}

export function renderSettingsStaticField(fieldKey, title, copy, value, className = "") {
  return renderSettingsFieldCard(fieldKey, title, copy, `<div class="oq-settings-static-value">${escapeHtml(value)}</div>`, className);
}

export function renderSettingsSystemRow({
  label,
  value,
  note = "",
  action = "",
  className = "",
  dataAttribute = "data-oq-diagnostics-row",
  dataValue = "",
}) {
  const classes = `oq-settings-system-row${action ? " oq-settings-system-row--with-action" : ""}${className ? ` ${escapeHtml(className)}` : ""}`;
  const attribute = dataAttribute && dataValue ? ` ${dataAttribute}="${escapeHtml(dataValue)}"` : "";
  if (!action && !note) {
    return `<div class="${classes}"${attribute}><span class="oq-settings-system-row-label">${escapeHtml(label)}</span><strong class="oq-settings-system-row-value">${escapeHtml(value)}</strong></div>`;
  }
  return `<div class="${classes}"${attribute}><div class="oq-settings-system-row-copy"><p class="oq-settings-system-row-label">${escapeHtml(label)}</p><strong class="oq-settings-system-row-value">${escapeHtml(value)}</strong>${note ? `<p class="oq-settings-system-row-note">${escapeHtml(note)}</p>` : ""}</div>${action}</div>`;
}

export function getSettingsStatValue(key, options = {}) {
  const config = typeof options === "number"
    ? { decimals: options }
    : (options || {});
  const entity = state.entities[key];
  if (!entity) {
    return "—";
  }

  const numeric = parseLooseNumber(entity.value ?? entity.state);
  if (Number.isFinite(numeric)) {
    const decimals = Number.isInteger(numeric)
      ? 0
      : Number.isFinite(config.decimals) ? config.decimals : 1;
    let formatted = numeric.toFixed(Math.max(0, decimals));
    if (config.trimTrailingZeros && formatted.includes(".")) {
      formatted = formatted.replace(/\.?0+$/, "");
    }
    return `${formatted}${entity.uom ? ` ${entity.uom}` : ""}`;
  }

  const text = String(entity.state ?? entity.value ?? "").trim();
  const normalizedText = text.toLowerCase();
  return !text || normalizedText === "nan" || normalizedText === "unknown" || normalizedText === "unavailable" ? "—" : text;
}

export function getSettingsTextStatValue(key, fallback = "—") {
  const entity = state.entities[key];
  if (!entity) {
    return fallback;
  }

  const text = String(entity.state ?? entity.value ?? "").trim();
  if (!text || text === "0" || text === "—") {
    return fallback;
  }

  return text;
}

export function formatSettingsNumberValue(value, unit = "", decimals = 2) {
  const numeric = Number(value);
  if (!Number.isFinite(numeric)) {
    return "—";
  }
  return `${numeric.toFixed(Math.max(0, decimals))}${unit ? ` ${unit}` : ""}`;
}

export function getSettingsTemperatureValue(key, decimals = 2) {
  return getSettingsStatValue(key, { decimals });
}

export function getStatusTextValue(key, fallback = "IDLE") {
  const rawValue = getSettingsTextStatValue(key, fallback);
  const normalized = String(rawValue ?? "").trim();
  if (!normalized || normalized === "0" || normalized === "UNKNOWN" || normalized === "UNAVAILABLE" || normalized === "NAN") {
    return fallback;
  }
  return normalized;
}

export function getCommissioningStatusValue() {
  const rawStatus = getSettingsTextStatValue("commissioningStatus", "");
  const cm100Active = isEntityActive("cm100Active");
  const normalizedRawStatus = String(rawStatus || "").trim().toUpperCase();
  if (
    cm100Active
    || normalizedRawStatus === "CM100 READY"
    || normalizedRawStatus === "CM100 STOPPED"
    || normalizedRawStatus.includes("DONE")
    || normalizedRawStatus.includes("FAILED")
    || normalizedRawStatus.includes("ABORT")
    || normalizedRawStatus.includes("APPLIED")
    || normalizedRawStatus.includes("REFUSED")
  ) {
    state.pendingCommissioningCm100Start = false;
  }
  if (normalizedRawStatus && normalizedRawStatus !== "0") {
    if (normalizedRawStatus === "IDLE" && state.pendingCommissioningCm100Start) {
      return "CM100 REQUESTED";
    }
    return normalizedRawStatus;
  }
  if (state.pendingCommissioningCm100Start) {
    return "CM100 REQUESTED";
  }
  return cm100Active ? "CM100 READY" : "IDLE";
}

export function formatSettingsOptionLabel(option) {
  const value = String(option || "").trim();
  if (!value) {
    return "";
  }

  const labels = {
    Automatic: "Automatisch",
    None: "Geen",
    Manual: "Handmatig",
    Schedule: "Dagelijks tijdvenster",
    Disabled: "Niet gebruiken",
    "HA input + Manual": "HA-invoer + handmatig",
    "MQTT + Manual": "MQTT + handmatig",
    "OT thermostat + Manual": "OT-thermostaat + handmatig",
    "Schedule + Manual": "Dagelijks tijdvenster + handmatig",
    "CIC + Manual": "CIC + handmatig",
    "CIC + HA input + Manual": "CIC + HA-invoer + handmatig",
    Balanced: "Gebalanceerd",
    Stable: "Stabiel",
    Responsive: "Direct",
    Calm: "Rustig",
    Custom: "Aangepast",
    [STRATEGY_OPTION_CURVE]: "Stooklijn",
    [STRATEGY_OPTION_POWER_HOUSE]: "Power House",
    "Heating demand": "Warmtevraag",
    "Cooling demand": "Koelvraag",
    "Water temperature": "Watertemperatuur",
    "Minimum off time": "Minimale uit-tijd",
    "Heating or cooling demand": "Warmte- of koelvraag",
    "External control": "Externe bediening",
    "Dew point required": "Dauwpuntmeting vereist",
    "Dew point": "Dauwpunt",
    "Dew point (MQTT)": "Dauwpunt (MQTT)",
    "Dew point (HA)": "Dauwpunt (HA)",
    "Allow without dew point": "Dauwpuntsbenadering",
    "Allow without dew point, use fallback": "Dauwpuntsbenadering",
    "Allow without dew point, use dew point approximation": "Dauwpuntsbenadering",
    "Allow without dew point, user responsibility": "Expliciet toestaan",
    Fallback: "Dauwpuntsbenadering",
    "Fallback blocked": "Dauwpuntsbenadering geblokkeerd",
    "User responsibility": "Expliciet toegestaan",
    Local: "Lokaal",
    CIC: "CIC",
    "HA input": "HA-invoer",
    "API input": "API-invoer",
    "API Input": "API-invoer",
    "CIC + HA input": "CIC + HA-invoer",
    "OT thermostat": "OT-thermostaat",
    "Outdoor unit": "Buitenunit",
    "Local - PT1000": "Lokaal - PT1000",
    "Local - DS18B20": "Lokaal - DS18B20",
    "HP1 water out (fallback)": "HP1 uitgaand water (fallback)",
    "HP2 water out (fallback)": "HP2 uitgaand water (fallback)",
    Unavailable: "Niet beschikbaar",
    Auto: "Auto",
    "CIC or HA input": "CIC of HA-invoer",
    "Flowmeter HP1": "Flowmeter HP1",
    "Flowmeter HP2": "Flowmeter HP2",
    "Local aggregate HP1/HP2": "Gecombineerde flow HP1/HP2",
  };

  return labels[value] || value;
}

export function renderSettingsChoiceOption({ key, option, currentValue, busy, copy = "", meta = "", image = "", imageAlt = "", infoTitle = "", infoCopy = "", infoId = "" }) {
  const active = option === currentValue;
  const cardBody = `
    <button
      class="oq-helper-surface oq-settings-choice-card${active ? " is-active" : ""}${image ? " oq-settings-choice-card--with-image" : ""}${infoCopy ? " oq-settings-choice-card--has-info" : ""}"
      type="button"
      data-oq-action="select-settings-option"
      data-select-key="${escapeHtml(key)}"
      data-select-option="${escapeHtml(option)}"
      aria-pressed="${active ? "true" : "false"}"
      ${busy ? "disabled" : ""}
    >
      <span class="oq-settings-choice-head">
        <span class="oq-settings-choice-title">${escapeHtml(formatSettingsOptionLabel(option))}</span>
        ${meta ? `<span class="oq-settings-choice-meta"><span class="oq-settings-choice-meta-text">${escapeHtml(meta)}</span></span>` : ""}
      </span>
      ${image ? `<span class="oq-settings-choice-media"><img src="${escapeHtml(image)}" alt="${escapeHtml(imageAlt || formatSettingsOptionLabel(option))}" loading="lazy" decoding="async"></span>` : ""}
      ${copy ? `<span class="oq-settings-choice-copy">${escapeHtml(copy)}</span>` : ""}
    </button>
  `;
  if (!infoCopy) {
    return cardBody;
  }

  const toggleTitle = infoTitle || formatSettingsOptionLabel(option);
  const toggleId = infoId || `${key}-${option}`;
  return `
    <article class="oq-settings-choice-card-shell${active ? " is-active" : ""}${image ? " oq-settings-choice-card-shell--with-image" : ""}">
      ${cardBody}
      ${renderSettingsInfoToggle(toggleId, toggleTitle, infoCopy)}
    </article>
  `;
}

export function getSelectEntityOptions(entity = {}) {
  if (Array.isArray(entity.option)) {
    return entity.option;
  }
  if (Array.isArray(entity.options)) {
    return entity.options;
  }
  return [];
}

export function renderSettingsSelectField(key, title, copy, className = "") {
  if (!hasEntity(key)) {
    return "";
  }
  const entity = state.entities[key] || {};
  const value = String(getEntityValue(key) || "");
  const options = getSelectEntityOptions(entity);
  const busy = state.loadingEntities
    || state.busyAction === `save-${key}`
    || (key === "strategy" && state.busyAction === "save-heatingEnableSource");
  return renderSettingsFieldCard(key, title, copy, `<label class="oq-settings-control oq-settings-control--select"><select class="oq-helper-select" data-oq-field="${escapeHtml(key)}" ${busy ? "disabled" : ""}>${options.map((option) => `<option value="${escapeHtml(option)}" ${option === value ? "selected" : ""}>${escapeHtml(formatSettingsOptionLabel(option))}</option>`).join("")}</select><span class="oq-settings-select-caret" aria-hidden="true"></span></label>`, className);
}

export function renderSettingsAdvancedDisclosure(id, title, copy, bodyMarkup) {
  const body = String(bodyMarkup || "").trim();
  if (!body) {
    return "";
  }
  const open = Boolean(state.settingsAdvancedOpen?.[id]);
  return `
    <details class="oq-settings-advanced" data-oq-settings-advanced="${escapeHtml(id)}"${open ? " open" : ""}>
      <summary data-oq-action="toggle-settings-advanced" data-settings-advanced="${escapeHtml(id)}">${escapeHtml(title)}</summary>
      <div class="oq-settings-advanced-body">
        ${copy ? `<p class="oq-settings-advanced-copy">${escapeHtml(copy)}</p>` : ""}
        ${body}
      </div>
    </details>
  `;
}

export function renderSettingsSwitchPill(key, enabled, onLabel = "Aan", offLabel = "Uit") {
  return `<span class="oq-settings-toggle-state${enabled ? " is-on" : ""}" data-oq-switch-pill="${escapeHtml(key)}" data-on-label="${escapeHtml(onLabel)}" data-off-label="${escapeHtml(offLabel)}">${escapeHtml(enabled ? onLabel : offLabel)}</span>`;
}

export function renderSettingsCompactSwitchControl(key, title, enabled, busy, onLabel = "Aan", offLabel = "Uit", showStatus = true) {
  const stateLabel = enabled ? onLabel : offLabel;
  const nextState = enabled ? "off" : "on";
  return `
    <div class="oq-settings-compact-switch-row">
      ${showStatus ? renderSettingsSwitchPill(key, enabled, onLabel, offLabel) : ""}
      <button
        class="oq-settings-toggle-switch${enabled ? " is-on" : ""}"
        type="button"
        role="switch"
        data-oq-action="toggle-overview-control"
        data-control-key="${escapeHtml(key)}"
        data-control-state="${escapeHtml(nextState)}"
        data-switch-title="${escapeHtml(title)}"
        data-on-label="${escapeHtml(onLabel)}"
        data-off-label="${escapeHtml(offLabel)}"
        aria-checked="${enabled ? "true" : "false"}"
        aria-label="${escapeHtml(`${title}: ${stateLabel}`)}"
        ${busy ? "disabled" : ""}
      >
        <span class="oq-settings-toggle-switch-track" aria-hidden="true">
          <span class="oq-settings-toggle-switch-knob"></span>
        </span>
      </button>
    </div>
  `;
}

export function renderSettingsSwitchCopy(key, enabled, enabledCopy = "", disabledCopy = "") {
  const copy = enabled ? enabledCopy : disabledCopy;
  if (!copy) {
    return "";
  }
  return `<p data-oq-switch-copy="${escapeHtml(key)}" data-on-copy="${escapeHtml(enabledCopy)}" data-off-copy="${escapeHtml(disabledCopy)}">${escapeHtml(copy)}</p>`;
}

export function renderSettingsSwitchField(key, title, copy, enabledCopy = "", disabledCopy = "", className = "") {
  if (!hasEntity(key)) {
    return "";
  }

  const enabled = Boolean(getEntityValue(key));
  const busy = state.loadingEntities || state.busyAction === `switch-${key}`;
  return renderSettingsFieldCard(
    key,
    title,
    copy,
    `
      <div class="oq-settings-compact-switch-field">
        ${renderSettingsCompactSwitchControl(key, title, enabled, busy)}
        ${renderSettingsSwitchCopy(key, enabled, enabledCopy, disabledCopy)}
      </div>
    `,
    className,
  );
}

export function renderSettingsCheckboxSwitchField(key, title, copy, label, className = "") {
  if (!hasEntity(key)) {
    return "";
  }

  const enabled = Boolean(getEntityValue(key));
  const busy = state.loadingEntities || state.busyAction === `switch-${key}`;
  return renderSettingsFieldCard(
    key,
    title,
    copy,
    `
      <div class="oq-settings-compact-switch-field">
        ${renderSettingsCompactSwitchControl(key, title, enabled, busy)}
        ${label ? `<p>${escapeHtml(label)}</p>` : ""}
      </div>
    `,
    className,
  );
}

export function renderSettingsIntegrationSwitchCard(key, title, copy) {
  if (!hasEntity(key)) {
    return "";
  }

  const enabled = Boolean(getEntityValue(key));
  const busy = state.loadingEntities || state.busyAction === `switch-${key}`;
  return `
    <article class="oq-settings-integration-card" data-oq-settings-field="${escapeHtml(key)}">
      <div class="oq-settings-integration-card-head">
        <h4>${escapeHtml(title)}</h4>
      </div>
      <p>${escapeHtml(copy)}</p>
      ${renderSettingsCompactSwitchControl(key, title, enabled, busy)}
    </article>
  `;
}

export function renderNamedActionButton(buttonKey, label, buttonClass = "oq-helper-button oq-helper-button--ghost", disabled = false) {
  return `
    <button
      class="${buttonClass}"
      type="button"
      data-oq-action="press-named-button"
      data-oq-button-key="${escapeHtml(buttonKey)}"
      ${disabled ? "disabled" : ""}
    >
      ${escapeHtml(label)}
    </button>
  `;
}

export function renderNamedToggleActionButton({
  active,
  startKey,
  stopKey,
  startLabel,
  stopLabel,
  startClass = "oq-helper-button oq-helper-button--primary",
  stopClass = "oq-helper-button oq-helper-button--ghost",
  startDisabled = false,
  stopDisabled = false,
}) {
  const key = active ? stopKey : startKey;
  const label = active ? stopLabel : startLabel;
  const buttonClass = active ? stopClass : startClass;
  const disabled = active ? stopDisabled : startDisabled;
  return renderNamedActionButton(key, label, buttonClass, disabled);
}

export function renderSettingsOptionCardsField(key, title, copy, descriptions, className = "") {
  if (!hasEntity(key)) {
    return "";
  }

  const entity = state.entities[key] || {};
  const currentValue = String(getEntityValue(key) || "");
  const options = getSelectEntityOptions(entity);
  const busy = state.loadingEntities || state.busyAction === `save-${key}`;
  const controlMarkup = `
    <div class="oq-settings-choice-grid">
      ${options.map((option) => {
        const description = descriptions[option] || "";
        const optionCopy = typeof description === "string" ? description : (description.copy || "");
        const optionImage = typeof description === "string" ? "" : (description.image || "");
        const optionImageAlt = typeof description === "string" ? "" : (description.alt || "");
        return renderSettingsChoiceOption({ key, option, currentValue, busy, copy: optionCopy, image: optionImage, imageAlt: optionImageAlt });
      }).join("")}
    </div>
  `;

  return renderSettingsFieldCard(key, title, copy, controlMarkup, className);
}

export function renderSettingsNumberField(key, title, copy, className = "", options = {}) {
  if (!hasEntity(key)) {
    return "";
  }

  const meta = getNumberMeta(key);
  const value = getInputDraftValue(key);
  const unit = options.unitOverride || meta.uom || "";
  const showUnit = options.showUnit !== false && Boolean(unit);
  const useInlineUnit = showUnit && options.unitMode !== "outside";
  const controlMarkup = renderNumberInputControl({
    key,
    value,
    meta,
    controlClass: `oq-helper-control${showUnit && !useInlineUnit ? " oq-helper-control--split" : ""}${useInlineUnit ? " oq-helper-control--suffix" : ""}`,
    unitMarkup: showUnit
      ? useInlineUnit
        ? `<span class="oq-helper-unit-chip">${escapeHtml(unit)}</span>`
        : `<span class="oq-helper-unit">${escapeHtml(unit)}</span>`
      : "",
  });

  return renderSettingsFieldCard(key, title, copy, controlMarkup, className, options.footerMarkup || "");
}

export function renderSettingsSliderField(key, title, copy, className = "", options = {}) {
  if (!hasEntity(key)) {
    return "";
  }
  const meta = getNumberMeta(key);
  const configuredMin = Number(options.minValue);
  const configuredMax = Number(options.maxValue);
  const min = Number.isFinite(configuredMin) ? configuredMin : meta.min;
  const max = Number.isFinite(configuredMax) ? configuredMax : meta.max;
  const value = Math.max(min, Math.min(max, normalizeNumber(key, getEntityValue(key))));
  const minLabel = options.minLabel || `${min}${meta.uom || ""}`;
  const maxLabel = options.maxLabel || `${max}${meta.uom || ""}`;
  const valueLabel = options.valueLabel || formatValue(key, value);
  return renderSettingsFieldCard(key, title, copy, `<label class="oq-helper-slider-field"><div class="oq-helper-slider-meta"><span>${escapeHtml(minLabel)}</span><strong>${escapeHtml(valueLabel)}</strong><span>${escapeHtml(maxLabel)}</span></div><input class="oq-helper-range" type="range" data-oq-field="${escapeHtml(key)}" min="${min}" max="${max}" step="${meta.step}" value="${value}" ${state.loadingEntities ? "disabled" : ""}></label>`, className, options.footerMarkup || "");
}

export function renderSettingsFrequencyRangeField(minKey, maxKey, title, copy) {
  if (!hasEntity(minKey) || !hasEntity(maxKey)) {
    return "";
  }
  const meta = getNumberMeta(minKey);
  const min = meta.min;
  const max = Math.min(meta.max, 110);
  let minValue = Math.min(max, normalizeNumber(minKey, getInputDraftValue(minKey)));
  let maxValue = Math.min(max, normalizeNumber(maxKey, getInputDraftValue(maxKey)));
  const disabled = minValue === 0 || maxValue === 0;
  if (disabled) {
    minValue = maxValue = 0;
  }
  const invalid = !disabled && minValue > maxValue;
  const valueLabel = disabled
    ? "Geen uitsluiting"
    : invalid
      ? "Ongeldig bereik"
      : `${minValue}–${maxValue} ${meta.uom || "Hz"}`;
  const span = Math.max(1, max - min);
  const start = ((minValue - min) / span) * 100;
  const end = ((maxValue - min) / span) * 100;
  const markup = `
    <div
      class="oq-helper-dual-range${disabled ? " is-disabled" : ""}${invalid ? " is-invalid" : ""}"
      data-oq-dual-range="true"
      style="--oq-range-start:${start}%;--oq-range-end:${end}%"
    >
      <div class="oq-helper-slider-meta" style="position:relative">
        <span>Uit</span>
        <span style="position:absolute;left:18.18%;transform:translateX(-50%)">20Hz</span>
        <strong data-oq-range-value>${escapeHtml(valueLabel)}</strong>
        <span>${escapeHtml(`${max}${meta.uom || ""}`)}</span>
      </div>
      <div class="oq-helper-dual-range-track">
        <input
          class="oq-helper-dual-range-input oq-helper-dual-range-input--min"
          type="range"
          data-oq-field="${escapeHtml(minKey)}"
          data-oq-range-role="min"
          aria-label="Ondergrens bereik"
          min="${min}"
          max="${max}"
          step="${meta.step}"
          value="${minValue}"
          ${state.loadingEntities ? "disabled" : ""}
        >
        <input
          class="oq-helper-dual-range-input oq-helper-dual-range-input--max"
          type="range"
          data-oq-field="${escapeHtml(maxKey)}"
          data-oq-range-role="max"
          aria-label="Bovengrens bereik"
          min="${min}"
          max="${max}"
          step="${meta.step}"
          value="${maxValue}"
          ${state.loadingEntities ? "disabled" : ""}
        >
      </div>
    </div>
  `;
  const disableButton = `<button class="oq-helper-button oq-helper-button--ghost oq-range-disable" type="button" data-oq-action="disable-range" data-oq-range-key="${escapeHtml(minKey)}" ${disabled || state.loadingEntities ? "disabled" : ""}>Uitschakelen</button>`;
  return renderSettingsFieldCard(minKey, title, copy, markup, "oq-settings-field--frequency-range", "", disableButton);
}

export function renderSettingsMiniNumberField(key, title, copy, options = {}) {
  if (!hasEntity(key)) {
    return "";
  }

  const meta = getNumberMeta(key);
  const value = getInputDraftValue(key);
  const compact = options.compact === true;
  const embedded = options.embedded === true;
  const infoId = options.infoId || key;
  const showCopy = options.showCopy !== false;
  return `
    <article class="oq-settings-mini-field${compact ? " oq-settings-mini-field--compact" : ""}${embedded ? " oq-settings-mini-field--embedded" : ""}">
      <div class="oq-settings-mini-copy">
        <div class="oq-settings-mini-copy-head">
          <h5>${escapeHtml(title)}</h5>
          ${copy ? renderSettingsInfoToggle(infoId, title, copy) : ""}
        </div>
        ${copy && showCopy ? `<p>${escapeHtml(copy)}</p>` : ""}
      </div>
      ${renderNumberInputControl({
        key,
        value,
        meta,
        controlClass: "oq-helper-control oq-helper-control--suffix",
        inputClass: "oq-helper-input oq-helper-input--compact-number",
        unitMarkup: meta.uom ? `<span class="oq-helper-unit-chip">${escapeHtml(meta.uom)}</span>` : "",
      })}
    </article>
  `;
}

export function renderSettingsTimeField(key, title, copy, className = "") {
  if (!hasEntity(key)) {
    return "";
  }
  const value = toTimeInputValue(getInputDraftValue(key));
  return renderSettingsFieldCard(key, title, copy, `<label class="oq-settings-control oq-settings-control--time"><input class="oq-helper-input oq-helper-input--time" type="time" step="60" lang="nl-NL" inputmode="numeric" aria-label="${escapeHtml(title)}" data-oq-field="${escapeHtml(key)}" value="${escapeHtml(value)}" ${state.loadingEntities || state.savingTimeFields.has(key) ? "disabled" : ""}><span class="oq-settings-time-icon" aria-hidden="true"><svg viewBox="0 0 20 20" focusable="false"><circle cx="10" cy="10" r="6.5" fill="none" stroke="currentColor" stroke-width="1.6" /><path d="M10 6.2 V10 L12.9 11.8" fill="none" stroke="currentColor" stroke-width="1.6" stroke-linecap="round" stroke-linejoin="round" /></svg></span></label>`, className || "oq-settings-field--time");
}

export function renderSettingsSection(kicker, title, copy, body, badgeMarkup = "", className = "", headerActions = "") {
  return `<section class="oq-settings-section${className ? ` ${escapeHtml(className)}` : ""}"><div class="oq-settings-section-head"><div class="oq-settings-section-head-meta"><p class="oq-helper-label">${escapeHtml(kicker)}</p>${badgeMarkup ? `<div class="oq-settings-section-head-meta-badge">${badgeMarkup}</div>` : ""}${headerActions ? `<div class="oq-settings-section-head-actions">${headerActions}</div>` : ""}</div><h3>${escapeHtml(title)}</h3><p>${escapeHtml(copy)}</p></div>${body}</section>`;
}
