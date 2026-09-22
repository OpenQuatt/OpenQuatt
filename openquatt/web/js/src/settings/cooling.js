import { getEntityNumericValue, getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";
import { COOLING_SCHEDULE_EFFECTIVE_SOURCE_KEY, COOLING_SCHEDULE_SOURCE_KEY, COOLING_SCHEDULE_TIME_KEYS, COOLING_SCHEDULE_VALID_KEY } from "../core/config.js";
import { formatValue, toTimeInputValue } from "../core/entity-store.js";
import { formatSettingsOptionLabel, renderSettingsAdvancedDisclosure, renderSettingsFieldCard, renderSettingsNumberField, renderSettingsOptionCardsField, renderSettingsSection, renderSettingsSelectField, renderSettingsSliderField, renderSettingsSwitchField, renderSettingsTimeField } from "./controls.js";
import { escapeHtml } from "../core/html.js";
import { state } from "../core/state.js";
import { t } from "../i18n/index.js";

  export function renderSettingsCoolingFact(label, value) {
    return `
      <div class="oq-settings-cooling-fact">
        <span>${escapeHtml(label)}</span>
        <strong>${escapeHtml(value)}</strong>
      </div>
    `;
  }

  export function formatCoolingBlockReason(reason) {
    const value = String(reason || "").trim();
    if (!value) {
      return "";
    }

    const labels = {
      Ready: t("settingsCooling.blockReady"),
      "Waiting for room request": t("settingsCooling.blockWaitingRoom"),
      "Cooling enabled, waiting for room temperature above cooling setpoint": t("settingsCooling.blockWaitingRoom"),
      "No dew point source": t("settingsCooling.blockNoDewPoint"),
      "OpenQuatt paused": t("settingsCooling.blockPaused"),
      "Cooling disabled": t("settingsCooling.blockDisabled"),
      "Cooling minimum unavailable": t("settingsCooling.blockMinUnavailable"),
      "Flow too low": t("settingsCooling.blockFlowLow"),
      "Fallback active": t("settingsCooling.blockFallback"),
      "Fallback active (+0.5°C warm night)": t("settingsCooling.blockFallbackWarmNight"),
      "Fallback active (+1.0°C very warm night)": t("settingsCooling.blockFallbackVeryWarmNight"),
      "Fallback active (+1.5°C tropical night)": t("settingsCooling.blockFallbackTropicalNight"),
      "User responsibility (no dew point or fallback)": t("settingsCooling.blockUserResponsibility"),
      "Fallback cooling active": t("settingsCooling.blockFallback"),
      "Fallback corrected by warm night": t("settingsCooling.blockFallbackCorrected"),
      "Fallback blocked by tropical night": t("settingsCooling.blockFallbackBlocked"),
      ...getCoolingStartBlockLabels(),
    };

    return labels[value] || value;
  }

  export const COOLING_START_BLOCK_REASON_READY = "Ready";
  export function getCoolingStartBlockLabels() {
    return {
      Ready: t("settingsCooling.startReady"),
      "Cooling minimum off-time": t("settingsCooling.startMinOffTime"),
      "Waiting for confirmed cooling stop": t("settingsCooling.startConfirmedStop"),
      "Compressor restart protection": t("settingsCooling.startRestartProtection"),
      "Startup inhibit after reboot": t("settingsCooling.startInhibitReboot"),
      "Compressor start limit (6/hour)": t("settingsCooling.startLimit"),
      "Compressor start blocked": t("settingsCooling.startBlocked"),
    };
  }

  export function formatCoolingStartBlockCountdown(seconds) {
    const total = Math.max(0, Math.ceil(Number(seconds) || 0));
    const minutes = Math.floor(total / 60);
    const rest = total % 60;
    return `${minutes}:${String(rest).padStart(2, "0")}`;
  }

  // Redenen die een afteltijd mogen tonen. De firmware garandeert al dat
  // alleen tijdgebonden redenen remaining_s > 0 dragen, maar reason en timer
  // zijn losse entities met eigen poll-moment. Deze tabel voorkomt dat een
  // oude timer bij een niet-tijdgebonden reden belandt; tussen twee
  // tijdgebonden redenen kan bij een overgang kort een oude timer staan.
  const COOLING_START_BLOCK_COUNTDOWN_REASONS = new Set([
    "Cooling minimum off-time",
    "Compressor restart protection",
    "Startup inhibit after reboot",
    "Compressor start limit (6/hour)",
  ]);

  export function formatCoolingStartBlockReason(reason, remainingS) {
    const value = String(reason || "").trim();
    if (!value) {
      return "";
    }
    const label = getCoolingStartBlockLabels()[value] || value;
    // Firmwarecontract: tijdgebonden redenen dragen altijd remaining_s > 0,
    // de overige altijd 0. De tabel hierboven houdt bovendien een oude timer
    // weg bij niet-tijdgebonden redenen.
    const remaining = Math.ceil(Number(remainingS) || 0);
    if (remaining > 0 && COOLING_START_BLOCK_COUNTDOWN_REASONS.has(value)) {
      return t("settingsCooling.startCountdown", { label, time: formatCoolingStartBlockCountdown(remaining) });
    }
    return label;
  }

  export function getCoolingCompressorRunning() {
    // De toegepaste compressorstand is leidend: niveau > 0 betekent draaien.
    for (const key of ["hp1Compressor", "hp2Compressor"]) {
      const level = getEntityNumericValue(key);
      if (!Number.isNaN(level) && level > 0) {
        return true;
      }
    }
    return false;
  }

  export function getCoolingStartBlockModel() {
    if (!hasEntity("coolingStartBlockReason")) {
      return { available: false, blocked: false, reasonRaw: "", remainingS: 0, hasCountdown: false, display: "" };
    }
    const reasonRaw = String(getEntityStateText("coolingStartBlockReason", "") || "").trim();
    if (!reasonRaw) {
      return { available: true, blocked: false, reasonRaw: "", remainingS: 0, hasCountdown: false, display: "" };
    }
    const remainingRaw = hasEntity("coolingStartBlockRemaining")
      ? getEntityNumericValue("coolingStartBlockRemaining")
      : Number.NaN;
    const remainingS = Number.isFinite(remainingRaw) && remainingRaw > 0 ? Math.ceil(remainingRaw) : 0;
    const blocked = reasonRaw !== COOLING_START_BLOCK_REASON_READY;
    const hasCountdown = blocked && remainingS > 0 &&
      COOLING_START_BLOCK_COUNTDOWN_REASONS.has(reasonRaw);
    return {
      available: true,
      blocked,
      reasonRaw,
      remainingS: hasCountdown ? remainingS : 0,
      hasCountdown,
      display: blocked ? formatCoolingStartBlockReason(reasonRaw, remainingS) : formatCoolingBlockReason(reasonRaw),
    };
  }

  export function getCoolingScheduleStatus() {
    const start = toTimeInputValue(getEntityStateText(COOLING_SCHEDULE_TIME_KEYS[0], ""));
    const end = toTimeInputValue(getEntityStateText(COOLING_SCHEDULE_TIME_KEYS[1], ""));
    const effective = getEntityStateText(COOLING_SCHEDULE_EFFECTIVE_SOURCE_KEY, "");
    return !start || !end ? t("settingsCooling.statusUnavailable")
      : start === end ? t("settingsCooling.statusDisabled")
      : getEntityStateText(COOLING_SCHEDULE_SOURCE_KEY, "") !== "Schedule" ? t("settingsCooling.statusNotSelected")
      : !isEntityActive(COOLING_SCHEDULE_VALID_KEY) ? t("settingsCooling.statusInvalidTime")
      : !effective || /unknown|unavailable/i.test(effective) ? t("settingsCooling.statusUnavailable")
      : effective.includes("Schedule") ? t("settingsCooling.statusOpen") : t("settingsCooling.statusClosed");
  }

  function getCoolingScheduleStatusCopy(status, start, end) {
    if (status === t("settingsCooling.statusOpen")) {
      return t("settingsCooling.statusCopyOpen", { start, end });
    }
    if (status === t("settingsCooling.statusClosed")) {
      return t("settingsCooling.statusCopyClosed", { start, end });
    }
    if (status === t("settingsCooling.statusDisabled")) {
      return t("settingsCooling.statusCopyDisabled");
    }
    return status === t("settingsCooling.statusInvalidTime")
      ? t("settingsCooling.statusCopyInvalidTime")
      : t("settingsCooling.statusCopyUnavailable");
  }

  export function renderCoolingScheduleSettingsFields(gridClass = "oq-settings-grid") {
    if (!hasEntity(COOLING_SCHEDULE_SOURCE_KEY) || !COOLING_SCHEDULE_TIME_KEYS.every((key) => hasEntity(key))) {
      return "";
    }
    const source = getEntityStateText(COOLING_SCHEDULE_SOURCE_KEY, "Disabled");
    const enabled = source === "Schedule";
    const start = toTimeInputValue(getEntityStateText(COOLING_SCHEDULE_TIME_KEYS[0], ""));
    const end = toTimeInputValue(getEntityStateText(COOLING_SCHEDULE_TIME_KEYS[1], ""));
    const status = getCoolingScheduleStatus();
    const busy = state.loadingEntities || state.busyAction === `save-${COOLING_SCHEDULE_SOURCE_KEY}`;
    const stateLabel = enabled ? t("overview.coolingOn") : t("overview.coolingOff");
    return `
      <section class="oq-settings-cooling-schedule${enabled ? " is-enabled" : ""}">
        <div class="oq-settings-subpanel-head oq-settings-cooling-schedule-head">
          <div>
            <p class="oq-helper-label">${escapeHtml(t("settingsCooling.scheduleKicker"))}</p>
            <h4>${escapeHtml(t("settingsCooling.scheduleTitle"))}</h4>
            <p>${escapeHtml(t("settingsCooling.scheduleCopy"))}</p>
          </div>
          <div class="oq-settings-compact-switch-row">
            <span class="oq-settings-toggle-state${enabled ? " is-on" : ""}">${stateLabel}</span>
            <button
              class="oq-settings-toggle-switch${enabled ? " is-on" : ""}"
              type="button"
              role="switch"
              data-oq-action="select-overview-control-option"
              data-control-key="${escapeHtml(COOLING_SCHEDULE_SOURCE_KEY)}"
              data-control-option="${enabled ? "Disabled" : "Schedule"}"
              aria-checked="${enabled ? "true" : "false"}"
              aria-label="${escapeHtml(t("settingsCooling.scheduleAria", { state: stateLabel }))}"
              ${busy ? "disabled" : ""}
            >
              <span class="oq-settings-toggle-switch-track" aria-hidden="true"><span class="oq-settings-toggle-switch-knob"></span></span>
            </button>
          </div>
        </div>
        ${enabled ? `
          <div class="${escapeHtml(gridClass)}">
            ${renderSettingsTimeField(COOLING_SCHEDULE_TIME_KEYS[0], t("settingsCooling.scheduleStartTitle"), t("settingsCooling.scheduleStartCopy"))}
            ${renderSettingsTimeField(COOLING_SCHEDULE_TIME_KEYS[1], t("settingsCooling.scheduleEndTitle"), t("settingsCooling.scheduleEndCopy"))}
          </div>
          <p class="oq-settings-cooling-schedule-status"><strong>${escapeHtml(status)}</strong><span>${escapeHtml(getCoolingScheduleStatusCopy(status, start, end))}</span></p>
        ` : ""}
      </section>
    `;
  }

  function renderCoolingSilentLimitWarning() {
    const silentModeOverride = getEntityStateText("silentModeOverride", "").trim().toLowerCase();
    if (silentModeOverride === "off") {
      return "";
    }

    const silentMaxHz = getEntityNumericValue("silentMaxHz");
    if (!hasEntity("silentMaxHz") || !Number.isFinite(silentMaxHz) || silentMaxHz >= 120) {
      return "";
    }
    const prefix = isEntityActive("silentActive")
      ? t("settingsCooling.silentLimitNow")
      : t("settingsCooling.silentLimitDuring");
    return `<p class="oq-settings-cooling-limit-warning"><span class="oq-settings-cooling-limit-warning-icon" aria-hidden="true">!</span><span>${escapeHtml(prefix)} ${escapeHtml(t("settingsCooling.silentLimitSuffix", { value: formatValue("silentMaxHz") }))}</span></p>`;
  }

  export function renderSettingsCoolingSection() {
    const roomRequestRequired = !hasEntity("coolingRoomRequestRequired") || isEntityActive("coolingRoomRequestRequired");
    const restartByMinimumOffTime = hasEntity("coolingRestartMode") &&
      getEntityStateText("coolingRestartMode", "Water temperature") === "Minimum off time";
    const scheduleFields = renderCoolingScheduleSettingsFields();
    const tuningFields = [
      renderSettingsNumberField("coolingMinimumSupplyTemp", t("settingsCooling.minSupplyTitle"), t("settingsCooling.minSupplyCopy")),
      renderSettingsSliderField("coolingDemandMax", t("settingsCooling.demandMaxTitle"), t("settingsCooling.demandMaxCopy"), "", {
        minLabel: t("settingsCooling.demandMinLabel"),
        maxLabel: t("settingsCooling.demandMaxLabel"),
        valueLabel: t("settingsCooling.demandValueSuffix", { value: formatValue("coolingDemandMax") }),
        footerMarkup: renderCoolingSilentLimitWarning(),
      }),
      hasEntity("coolingRestartMode") ? renderSettingsSelectField("coolingRestartMode", t("settingsCooling.restartModeTitle"), t("settingsCooling.restartModeCopy")) : "",
      restartByMinimumOffTime
        ? renderSettingsNumberField("coolingMinimumOffTime", t("settingsCooling.minOffTimeTitle"), t("settingsCooling.minOffTimeCopy"))
        : renderSettingsNumberField("coolingRestartDelta", t("settingsCooling.restartDeltaTitle"), t("settingsCooling.restartDeltaCopy")),
      renderSettingsNumberField("coolingSafetyMargin", t("settingsCooling.safetyMarginTitle"), t("settingsCooling.safetyMarginCopy")),
    ].filter(Boolean);
    const roomRequestFields = [
      hasEntity("coolingRoomRequestRequired") ? renderSettingsSwitchField(
        "coolingRoomRequestRequired",
        t("settingsCooling.roomRequestTitle"),
        t("settingsCooling.roomRequestOn"),
        t("settingsCooling.roomRequestOffOn"),
        t("settingsCooling.roomRequestOffOff"),
        "oq-settings-field--span-2",
      ) : "",
      roomRequestRequired ? renderSettingsNumberField("coolingRequestOnDelta", t("settingsCooling.requestOnDeltaTitle"), t("settingsCooling.requestOnDeltaCopy")) : "",
      roomRequestRequired ? renderSettingsNumberField("coolingRequestOffDelta", t("settingsCooling.requestOffDeltaTitle"), t("settingsCooling.requestOffDeltaCopy")) : "",
    ].filter(Boolean);
    const hasRoomRequestSettings = roomRequestFields.length > 0;
    const hasFallbackSettings = hasEntity("coolingWithoutDewPointMode");
    const guardStatusFacts = [
      hasEntity("coolingGuardMode") ? renderSettingsCoolingFact(t("settingsCooling.guardRoute"), formatSettingsOptionLabel(getEntityStateText("coolingGuardMode", t("overview.statusUnknown")))) : "",
      hasEntity("coolingEffectiveMinSupplyTemp") ? renderSettingsCoolingFact(t("settingsCooling.guardFloor"), getEntityStateText("coolingEffectiveMinSupplyTemp", "—")) : "",
    ].filter(Boolean);
    const guardStatusPanel = guardStatusFacts.length ? renderSettingsFieldCard(
      "coolingGuardStatus",
      t("settingsCooling.guardStatusTitle"),
      t("settingsCooling.guardStatusCopy"),
      `<div class="oq-settings-cooling-facts">${guardStatusFacts.join("")}</div>`,
      "oq-settings-field--span-2 oq-settings-field--cooling-status",
    ) : "";
    const fallbackMetricFacts = [
      hasEntity("outsideTempSelected") ? renderSettingsCoolingFact(t("settingsCooling.metricOutside"), getEntityStateText("outsideTempSelected", "—")) : "",
      hasEntity("coolingFallbackNightMinOutdoorTemp") ? renderSettingsCoolingFact(t("settingsCooling.metricNightMin"), getEntityStateText("coolingFallbackNightMinOutdoorTemp", "—")) : "",
      hasEntity("coolingFallbackMinSupplyTemp") ? renderSettingsCoolingFact(t("settingsCooling.metricMinSupply"), getEntityStateText("coolingFallbackMinSupplyTemp", "—")) : "",
    ].filter(Boolean);
    const fallbackMetricsMarkup = fallbackMetricFacts.length ? `<div class="oq-settings-cooling-fallback-metrics">${fallbackMetricFacts.join("")}</div>` : "";
    const hasFallbackDetails = hasFallbackSettings || fallbackMetricFacts.length > 0;
    const activeCoolingGuardMode = getEntityStateText("coolingGuardMode", "");
    const openFallbackDetails = activeCoolingGuardMode.toLowerCase().includes("fallback");
    const pidFields = [
      renderSettingsNumberField("coolingPidKp", t("settingsCooling.pidKpTitle"), t("settingsCooling.pidKpCopy")),
      renderSettingsNumberField("coolingPidKi", t("settingsCooling.pidKiTitle"), t("settingsCooling.pidKiCopy")),
      renderSettingsNumberField("coolingPidKd", t("settingsCooling.pidKdTitle"), t("settingsCooling.pidKdCopy")),
    ].filter(Boolean).join("");
    const advancedPidMarkup = renderSettingsAdvancedDisclosure(
      "cooling",
      t("settingsCooling.advancedTitle"),
      t("settingsCooling.advancedCopy"),
      pidFields ? `<div class="oq-settings-grid oq-settings-grid--pid">${pidFields}</div>` : "",
    );

    if (!scheduleFields && !tuningFields.length && !hasRoomRequestSettings && !hasFallbackSettings && !guardStatusPanel && !hasFallbackDetails && !advancedPidMarkup) {
      return "";
    }

    const fallbackModeCopy = {
      "Dew point required": t("settingsCooling.fallbackDewRequired"),
      "Allow without dew point": t("settingsCooling.fallbackAllow"),
      "Allow without dew point, use fallback": t("settingsCooling.fallbackAllow"),
      "Allow without dew point, use dew point approximation": t("settingsCooling.fallbackAllow"),
      "Allow without dew point, user responsibility": t("settingsCooling.fallbackUserResponsibility"),
    };

    return renderSettingsSection(
      t("settingsCooling.sectionGroup"),
      t("settingsCooling.sectionTitle"),
      t("settingsCooling.sectionCopy"),
      `
        ${scheduleFields}
        ${tuningFields.length ? `
          <div class="oq-settings-grid">
            ${tuningFields.join("")}
          </div>
        ` : ""}
        ${hasRoomRequestSettings ? `
          <div class="oq-settings-subpanel oq-settings-subpanel--nested">
            <div class="oq-settings-subpanel-head">
              <p class="oq-helper-label">${escapeHtml(t("settingsCooling.roomRequestKicker"))}</p>
              <h4>${escapeHtml(t("settingsCooling.roomRequestPanelTitle"))}</h4>
              <p>${escapeHtml(t("settingsCooling.roomRequestPanelCopy"))}</p>
            </div>
            <div class="oq-settings-grid">
              ${roomRequestFields.join("")}
            </div>
          </div>
        ` : ""}
        ${(hasFallbackSettings || guardStatusPanel || hasFallbackDetails) ? `
          <div class="oq-settings-grid">
            ${hasFallbackSettings ? renderSettingsOptionCardsField("coolingWithoutDewPointMode", t("settingsCooling.guardChoiceTitle"), t("settingsCooling.guardChoiceCopy"), fallbackModeCopy, "oq-settings-field--span-2 oq-settings-field--cooling-guard-choice") : ""}
            ${guardStatusPanel}
            ${hasFallbackDetails ? `
              <details class="oq-settings-callout oq-settings-callout--cooling oq-settings-callout--inline"${openFallbackDetails ? " open" : ""}>
              <summary>${escapeHtml(t("settingsCooling.fallbackDetails"))}</summary>
              <div class="oq-settings-callout-body">
                ${fallbackMetricsMarkup}
                <p>${escapeHtml(t("settingsCooling.fallbackP1"))}</p>
                <p>${escapeHtml(t("settingsCooling.fallbackP2"))}</p>
                <p>${escapeHtml(t("settingsCooling.fallbackP3"))}</p>
                <p>${escapeHtml(t("settingsCooling.fallbackP4"))}</p>
                <div class="oq-settings-rule-groups">
                  <section class="oq-settings-rule-group">
                    <h4>${escapeHtml(t("settingsCooling.ruleOutsideTitle"))}</h4>
                    <div class="oq-settings-rule-table">
                      <div class="oq-settings-rule-row">
                        <span class="oq-settings-rule-key">${escapeHtml(t("settingsCooling.ruleBelow20"))}</span>
                        <span class="oq-settings-rule-value">${escapeHtml(t("settingsCooling.ruleOff"))}</span>
                      </div>
                      <div class="oq-settings-rule-row">
                        <span class="oq-settings-rule-key">${escapeHtml(t("settingsCooling.ruleRange20to32"))}</span>
                        <span class="oq-settings-rule-value">${escapeHtml(t("settingsCooling.ruleRangeValue"))}</span>
                      </div>
                      <div class="oq-settings-rule-row">
                        <span class="oq-settings-rule-key">${escapeHtml(t("settingsCooling.ruleFrom32"))}</span>
                        <span class="oq-settings-rule-value">${escapeHtml(t("settingsCooling.ruleMinWater22"))}</span>
                      </div>
                    </div>
                  </section>
                  <section class="oq-settings-rule-group">
                    <h4>${escapeHtml(t("settingsCooling.ruleNightTitle"))}</h4>
                    <div class="oq-settings-rule-table">
                      <div class="oq-settings-rule-row">
                        <span class="oq-settings-rule-key">${escapeHtml(t("settingsCooling.ruleBelow18"))}</span>
                        <span class="oq-settings-rule-value">${escapeHtml(t("settingsCooling.rulePlus0"))}</span>
                      </div>
                      <div class="oq-settings-rule-row">
                        <span class="oq-settings-rule-key">${escapeHtml(t("settingsCooling.rule18to19"))}</span>
                        <span class="oq-settings-rule-value">${escapeHtml(t("settingsCooling.rulePlus05"))}</span>
                      </div>
                      <div class="oq-settings-rule-row">
                        <span class="oq-settings-rule-key">${escapeHtml(t("settingsCooling.rule19to20"))}</span>
                        <span class="oq-settings-rule-value">${escapeHtml(t("settingsCooling.rulePlus10"))}</span>
                      </div>
                      <div class="oq-settings-rule-row">
                        <span class="oq-settings-rule-key">${escapeHtml(t("settingsCooling.ruleFrom20"))}</span>
                        <span class="oq-settings-rule-value">${escapeHtml(t("settingsCooling.rulePlus15"))}</span>
                      </div>
                    </div>
                  </section>
                </div>
              </div>
            </details>
            ` : ""}
          </div>
        ` : ""}
        ${advancedPidMarkup}
      `,
    );
  }
