import { getEntityNumericValue, getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";
import { HP_GENERATION_IMAGE_V1, HP_GENERATION_IMAGE_V2 } from "../core/embedded-assets.js";
import { getInputDraftValue } from "../core/control-drafts.js";
import { isCurveMode } from "../core/domain-helpers.js";
import { getEntityValue, getNumberMeta } from "../core/entity-store.js";
import { formatIncidentOccurrenceTime, getFallbackBlockReasonLabel, getHeatPumpStatusPresentation, getIncidentActionPresentation, getIncidentCategoryLabel, getIncidentDisplayLabel, getIncidentEffectLabels, getIncidentLifecyclePresentation, getIncidentRecoveryLabel, getIncidentTechnicalCode, getIncidentUserActionLabel, getLinkLossConsequenceForHeatPump, getPumpIncidentContextRows, getSystemActionPresentation } from "../core/incident-monitoring.js";
import { getInstallationMonitoringFailureText, getInstallationMonitoringModel, isInstallationMonitoringBinaryActive, isInstallationMonitoringFailureActive, isInstallationMonitoringIntegrationEnabled, syncInstallationMonitoringDetailsState } from "../core/installation-monitoring.js";
import { renderLowFlowDiagnosis } from "../core/lowflow-diagnosis.js";
import { renderNumberInputControl } from "../core/number-controls.js";
import { state } from "../core/state.js";
import { getDebugRecordingStatusCopy, getDebugRecordingStatusLabel } from "../features/debug-recording.js";
import { formatDiagnosticsDateTime, formatUptimeFromMeta, getDeviceIpAddress, getInstallationLabel } from "../features/device-context.js";
import { getUpdateStatus } from "../features/firmware-update.js";
import { getConnectivityStatus, getEspTemperatureLabel } from "../features/header-status.js";
import { getOduGenerationChoiceMeta, getOduGenerationDetectionModel, renderOduGenerationDetectionStatus } from "../features/odu-generation-ui.js";
import { getOduRuntimeFrequencyHpIndexes } from "../features/odu-runtime-frequency.js";
import { getWebServerLogStatusLabel } from "../features/webserver-logs.js";
import { BOILER_OPENTHERM_CAPABILITY, getBoilerOpenThermCapability, getSupportedBoilerConnectionOptions } from "./boiler.js";
import { getSelectEntityOptions, renderNamedActionButton, renderSettingsAdvancedDisclosure, renderSettingsChoiceOption, renderSettingsCompactSwitchControl, renderSettingsFieldCard, renderSettingsMiniNumberField, renderSettingsNumberField, renderSettingsSection, renderSettingsSelectField, renderSettingsSwitchField, renderSettingsSystemRow } from "./controls.js";
import { getSettingsSelectModel } from "./field-models.js";
import { renderSettingsHeatPumpLimiterCard } from "./heating.js";
import { escapeHtml } from "../core/html.js";
import { formatDateTime, formatNumber, t } from "../i18n/index.js";

  export function renderSettingsOduRuntimeFrequencySection() {
    const hpIndexes = getOduRuntimeFrequencyHpIndexes();
    if (!hpIndexes.length) {
      return "";
    }

    return `
      <section class="oq-settings-section oq-settings-odu-launchers">
        <div class="oq-settings-section-head">
          <div class="oq-settings-section-head-meta"><p class="oq-helper-label">${escapeHtml(t("settingsInstallation.oduKicker"))}</p><span class="oq-settings-section-badge oq-settings-section-badge--experimental">${escapeHtml(t("settingsInstallation.oduExperimental"))}</span></div>
          <h3>${escapeHtml(t("settingsInstallation.oduTitle"))}</h3>
          <p>${escapeHtml(t("settingsInstallation.oduCopy"))}</p>
        </div>
        <div class="oq-settings-section-body oq-settings-odu-launcher-list">
          ${renderSettingsSystemRow({
            label: t("settingsInstallation.oduBottomTitle"),
            value: t("settingsInstallation.oduBottomValue"),
            note: t("settingsInstallation.oduBottomNote"),
            action: `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="open-odu-bottom-plate-settings">${escapeHtml(t("settingsInstallation.oduBottomAction"))}</button>`,
          })}
          ${renderSettingsSystemRow({
            label: t("settingsInstallation.oduFreqTitle"),
            value: hpIndexes.length === 2 ? t("settingsInstallation.oduFreqDual") : t("settingsInstallation.oduFreqSingle"),
            note: t("settingsInstallation.oduFreqNote"),
            action: `<button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="open-odu-frequency-settings">${escapeHtml(t("settingsInstallation.oduFreqAction"))}</button>`,
          })}
        </div>
      </section>`;
  }

  export function renderInstallationMonitoringBadge(
    active,
    activeLabel = null,
    clearLabel = null,
    activeTone = "warning",
  ) {
    const tone = active ? activeTone : "clear";
    return `<span class="oq-settings-monitoring-badge is-${escapeHtml(tone)}">${escapeHtml(active ? (activeLabel ?? t("settingsInstallation.badgeAttention")) : (clearLabel ?? t("settingsInstallation.badgeOk")))}</span>`;
  }

  export function renderInstallationMonitoringStatusRow({ label, value, note = "", active = false }) {
    return `
      <div class="oq-settings-monitoring-row${active ? " is-warning" : ""}">
        <div>
          <p>${escapeHtml(label)}</p>
          <strong>${escapeHtml(value)}</strong>
          ${note ? `<span>${escapeHtml(note)}</span>` : ""}
        </div>
        ${renderInstallationMonitoringBadge(active)}
      </div>
    `;
  }

  function getVisibleHpIncidents(heatPump) {
    return (heatPump?.incidents || []).filter((incident) => (
      incident.active
      || incident.recovering
      || (incident.latched && !incident.acknowledged)
    ));
  }

  export function renderInstallationMonitoringHpIncident(incident, pumpContext = null, consequenceNote = "") {
    const lifecycle = getIncidentLifecyclePresentation(incident);
    const effects = getIncidentEffectLabels(incident.effects);
    const firstSeen = formatIncidentOccurrenceTime(incident.firstSeenS, incident.firstSeenMs);
    const lastSeen = formatIncidentOccurrenceTime(incident.lastSeenS, incident.lastSeenMs);
    const technicalCode = getIncidentTechnicalCode(incident);
    const details = [
      technicalCode ? [t("settingsInstallation.dtOduCode"), technicalCode] : null,
      incident.technicalDescription ? [t("settingsInstallation.dtOduDesc"), incident.technicalDescription] : null,
      effects.length ? [t("settingsInstallation.dtEffect"), effects.join(", ")] : null,
      consequenceNote ? [t("settingsInstallation.dtConsequence"), consequenceNote] : null,
      firstSeen ? [t("settingsInstallation.dtFirstSeen"), firstSeen] : null,
      lastSeen ? [t("settingsInstallation.dtLastSeen"), lastSeen] : null,
      incident.recoveryCondition ? [t("settingsInstallation.dtRecovery"), getIncidentRecoveryLabel(incident.recoveryCondition)] : null,
      getIncidentUserActionLabel(incident.userAction)
        ? [t("settingsInstallation.dtUserAction"), getIncidentUserActionLabel(incident.userAction)]
        : null,
      incident.occurrenceCount > 1 ? [t("settingsInstallation.dtConfirmed"), t("settingsInstallation.dtConfirmedValue", { count: formatNumber(incident.occurrenceCount, { maximumFractionDigits: 0 }) })] : null,
      ...getPumpIncidentContextRows(incident, pumpContext),
    ].filter(Boolean);
    return `
      <div class="oq-settings-monitoring-incident">
        <div class="oq-settings-monitoring-incident-head">
          <div>
            <p>${escapeHtml(getIncidentCategoryLabel(incident.category))}</p>
            <strong>${escapeHtml(getIncidentDisplayLabel(incident))}</strong>
          </div>
          ${renderInstallationMonitoringBadge(
            lifecycle.tone !== "clear",
            lifecycle.label,
            lifecycle.label,
            lifecycle.tone,
          )}
        </div>
        ${details.length ? `<dl>${details.map(([label, value]) => (
          `<div><dt>${escapeHtml(label)}</dt><dd>${escapeHtml(value)}</dd></div>`
        )).join("")}</dl>` : ""}
      </div>
    `;
  }

  function renderInstallationMonitoringHeatPumpUnit(heatPump) {
    const presentation = getHeatPumpStatusPresentation(heatPump);
    const incidents = getVisibleHpIncidents(heatPump);
    // Issue #706: a stop-unconfirmed caused by a live link loss is rendered
    // nested inside the outage card, never as a second standalone card.
    const linkLossConsequence = getLinkLossConsequenceForHeatPump(heatPump);
    const shownIncidents = linkLossConsequence
      ? incidents.filter((incident) => incident.id !== linkLossConsequence.consequence.id)
      : incidents;
    const retryStartRequired = incidents.some((incident) => (
      incident.id === "1002" && incident.active
    ));
    const powerCycleRequired = incidents.some((incident) => (
      !incident.active
      && !incident.recovering
      && incident.latched
      && !incident.acknowledged
      && incident.recoveryCondition === "confirmed_odu_power_cycle"
    ));
    const incidentAction = getIncidentActionPresentation(state.incidentAction, heatPump.index);
    const actionPending = state.incidentAction?.pending === true;
    return `
      <div class="oq-settings-monitoring-rows">
        <div class="oq-settings-monitoring-row${presentation.tone === "clear" ? "" : " is-warning"}">
          <div>
            <p>${escapeHtml(t("settingsInstallation.hpUnitTitle", { index: heatPump.index }))}</p>
            <strong>${escapeHtml(presentation.label)}</strong>
            <span>${escapeHtml(presentation.note)}</span>
          </div>
          ${renderInstallationMonitoringBadge(
            presentation.tone !== "clear",
            presentation.tone === "fault" ? t("settingsInstallation.badgeUnavailable") : t("settingsInstallation.badgeLimited"),
            presentation.label,
            presentation.tone,
          )}
        </div>
        ${shownIncidents.map((incident) => renderInstallationMonitoringHpIncident(
          incident,
          heatPump.pumpContext,
          linkLossConsequence && incident.id === linkLossConsequence.linkLoss.id
            ? linkLossConsequence.copy
            : "",
        )).join("")}
        ${retryStartRequired ? `
          <div class="oq-settings-monitoring-incident">
            <div class="oq-settings-monitoring-incident-action">
              <button
                class="oq-helper-button oq-helper-button--warning"
                type="button"
                data-oq-action="retry-hp-start"
                data-oq-hp-index="${heatPump.index}"
                ${actionPending ? "disabled" : ""}
              >${state.incidentAction?.pending && state.incidentAction.hp === heatPump.index
                && state.incidentAction.kind === "start_failure_retry"
                  ? escapeHtml(t("settingsInstallation.retryBusy"))
                  : escapeHtml(t("settingsInstallation.retryLabel", { index: heatPump.index }))}</button>
              <span>${escapeHtml(t("settingsInstallation.retryNote"))}</span>
            </div>
          </div>
        ` : ""}
        ${powerCycleRequired ? `
          <div class="oq-settings-monitoring-incident">
            <div class="oq-settings-monitoring-incident-action">
              <button
                class="oq-helper-button oq-helper-button--warning"
                type="button"
                data-oq-action="confirm-hp-power-cycle"
                data-oq-hp-index="${heatPump.index}"
                ${actionPending ? "disabled" : ""}
              >${state.incidentAction?.pending && state.incidentAction.hp === heatPump.index
                && state.incidentAction.kind === "confirm_odu_power_cycle"
                  ? escapeHtml(t("settingsInstallation.powerBusy"))
                  : escapeHtml(t("settingsInstallation.powerLabel", { index: heatPump.index }))}</button>
              <span>${escapeHtml(t("settingsInstallation.powerNote", { index: heatPump.index }))}</span>
            </div>
          </div>
        ` : ""}
        ${incidentAction.visible ? `
          <div class="oq-settings-monitoring-incident">
            <div class="oq-settings-monitoring-incident-head">
              <div>
                <p>${escapeHtml(t("settingsInstallation.recoveryTitle"))}</p>
                <strong>${escapeHtml(incidentAction.label)}</strong>
              </div>
              ${renderInstallationMonitoringBadge(
                incidentAction.tone !== "clear",
                incidentAction.tone === "fault" ? t("settingsInstallation.badgeRefused") : t("settingsInstallation.badgePending"),
                t("settingsInstallation.badgeDone"),
                incidentAction.tone,
              )}
            </div>
            <span>${escapeHtml(incidentAction.copy)}</span>
          </div>
        ` : ""}
      </div>
    `;
  }

  function renderInstallationMonitoringStructuredHpPanel(monitoring) {
    const heatPumps = monitoring.incidentMonitoring?.snapshot?.heatPumps || [];
    if (!heatPumps.length) {
      return "";
    }
    const visibleIncidents = heatPumps.flatMap(getVisibleHpIncidents);
    const recoveredIncidents = visibleIncidents.filter((incident) => (
      incident.latched && !incident.acknowledged && !incident.active && !incident.recovering
    ));
    const acknowledgeableIncidents = recoveredIncidents.filter((incident) => (
      incident.recoveryCondition !== "confirmed_odu_power_cycle"
    ));
    return `
      <article class="oq-settings-monitoring-card">
        <header>
          <p>${escapeHtml(t("settingsInstallation.hpPanelTitle"))}</p>
          ${renderInstallationMonitoringBadge(
            visibleIncidents.some((incident) => incident.category !== "status"),
            t("settingsInstallation.hpPanelBadgeIncident"),
            t("settingsInstallation.badgeOk"),
            "warning",
          )}
        </header>
        <span>${escapeHtml(t("settingsInstallation.hpPanelCopy"))}</span>
        <div class="oq-settings-monitoring-rows">
          ${heatPumps.map(renderInstallationMonitoringHeatPumpUnit).join("")}
        </div>
        ${acknowledgeableIncidents.length > 0 && hasEntity("acknowledgeHpIncidents") ? `
          <div class="oq-settings-monitoring-incident-action">
            ${renderNamedActionButton(
              "acknowledgeHpIncidents",
              t("settingsInstallation.ackButton"),
              "oq-helper-button oq-helper-button--ghost",
            )}
            <span>${escapeHtml(t("settingsInstallation.ackNote"))}</span>
          </div>
        ` : ""}
      </article>
    `;
  }

  function getInstallationMonitoringSystemReaction(system) {
    const controlMode = Number(system?.controlMode) || 0;
    const commandActive = Boolean(system?.boilerCommandActive);
    const action = String(system?.action || "none");
    const actionPresentation = getSystemActionPresentation(action);
    if (controlMode === 3) {
      return commandActive
        ? {
          title: t("settingsInstallation.cm3Title"),
          copy: t("settingsInstallation.cm3Copy"),
          tone: "clear",
        }
        : {
          title: t("settingsInstallation.cm3InactiveTitle"),
          copy: t("settingsInstallation.cm3InactiveCopy"),
          tone: "warning",
        };
    }
    if (controlMode === 4) {
      return commandActive
        ? {
          title: t("settingsInstallation.cm4Title"),
          copy: t("settingsInstallation.cm4Copy"),
          tone: "fault",
        }
        : {
          title: t("settingsInstallation.cm4InactiveTitle"),
          copy: system.fallbackBlockReason
            ? t("settingsInstallation.cm4InactiveBlocked", { reason: getFallbackBlockReasonLabel(system.fallbackBlockReason) })
            : t("settingsInstallation.cm4InactiveNoReason"),
          tone: "fault",
        };
    }
    if (action === "fallback_blocked") {
      return {
        title: actionPresentation.label,
        copy: t("settingsInstallation.fallbackBlockedCopy", { copy: actionPresentation.copy, reason: getFallbackBlockReasonLabel(system.fallbackBlockReason) }),
        tone: "fault",
      };
    }
    return {
      title: controlMode >= 0 ? t("settingsInstallation.normalTitleMode", { mode: controlMode }) : t("settingsInstallation.normalTitle"),
      copy: t("settingsInstallation.normalCopy"),
      tone: "clear",
    };
  }

  function renderInstallationMonitoringSystemPanel(monitoring) {
    const system = monitoring.incidentMonitoring?.snapshot?.system;
    if (!system) {
      return "";
    }
    const reaction = getInstallationMonitoringSystemReaction(system);
    const continuityCopy = system.boilerCommandActive
      && system.boilerTransition === "assist_to_fallback_continuous"
      && system.boilerOutputContinuous === true
      ? t("settingsInstallation.continuityCopy")
      : "";
    return `
      <article class="oq-settings-monitoring-card oq-settings-monitoring-system">
        <header>
          <p>${escapeHtml(t("settingsInstallation.systemPanelTitle"))}</p>
          ${renderInstallationMonitoringBadge(
            reaction.tone !== "clear",
            reaction.tone === "fault" ? t("settingsInstallation.badgeFallback") : t("settingsInstallation.badgeInactive"),
            t("settingsInstallation.badgeNormal"),
            reaction.tone === "fault" ? "fault" : "warning",
          )}
        </header>
        <strong class="oq-settings-monitoring-card-value">${escapeHtml(reaction.title)}</strong>
        <span>${escapeHtml(reaction.copy)}</span>
        ${continuityCopy ? renderInstallationMonitoringStatusRow({
          label: t("settingsInstallation.transitionLabel"),
          value: t("settingsInstallation.transitionValue"),
          note: continuityCopy,
        }) : ""}
      </article>
    `;
  }

  export function getInstallationMonitoringCount(key) {
    const value = getEntityNumericValue(key);
    return Number.isNaN(value) ? "—" : String(Math.max(0, Math.round(value)));
  }

  export function formatInstallationMonitoringLastStart(key) {
    const ageMinutes = getEntityNumericValue(key);
    if (Number.isNaN(ageMinutes)) {
      return t("settingsInstallation.lastStartNever");
    }
    if (ageMinutes < 1) {
      return t("settingsInstallation.lastStartJust");
    }
    if (ageMinutes < 60) {
      return t("settingsInstallation.lastStartMinutes", { minutes: formatNumber(Math.round(ageMinutes), { maximumFractionDigits: 0 }) });
    }
    const hours = Math.floor(ageMinutes / 60);
    const minutes = Math.round(ageMinutes % 60);
    return t("settingsInstallation.lastStartHours", { hours: formatNumber(hours, { maximumFractionDigits: 0 }), minutes: formatNumber(minutes, { maximumFractionDigits: 0 }) });
  }

  export function formatInstallationMonitoringEpoch(key) {
    const epoch = getEntityNumericValue(key);
    if (Number.isNaN(epoch) || epoch <= 0) {
      return t("settingsInstallation.epochUnknown");
    }
    return formatDateTime(epoch * 1000, {
      day: "2-digit",
      month: "short",
      hour: "2-digit",
      minute: "2-digit",
    });
  }

  export function renderInstallationMonitoringCyclingIncident(monitoring) {
    if (!monitoring.cyclingAlertLatched) {
      return "";
    }
    const alternating = isInstallationMonitoringBinaryActive("compressorCyclingAlertAlternating");
    const hp1Peak2h = getInstallationMonitoringCount("compressorCyclingAlertHp1Peak2h");
    const hp1Peak72h = getInstallationMonitoringCount("compressorCyclingAlertHp1Peak72h");
    const hp2Peak2h = hasEntity("compressorCyclingAlertHp2Peak2h")
      ? getInstallationMonitoringCount("compressorCyclingAlertHp2Peak2h")
      : "";
    const hp2Peak72h = hasEntity("compressorCyclingAlertHp2Peak72h")
      ? getInstallationMonitoringCount("compressorCyclingAlertHp2Peak72h")
      : "";
    return `
      <div class="oq-settings-monitoring-incident${monitoring.cyclingAlertActive ? " is-active" : " is-recovered"}">
        <div class="oq-settings-monitoring-incident-head">
          <div>
            <p>${escapeHtml(t("settingsInstallation.cycleTitle"))}</p>
            <strong>${escapeHtml(monitoring.cyclingAlertActive ? t("settingsInstallation.cycleActive") : t("settingsInstallation.cycleRecovered"))}</strong>
          </div>
          ${renderInstallationMonitoringBadge(monitoring.cyclingAlertActive, t("settingsInstallation.cycleBadgeActive"), t("settingsInstallation.cycleBadgeRecovered"))}
        </div>
        <span>${escapeHtml(monitoring.cyclingAlertActive
          ? t("settingsInstallation.cycleActiveCopy")
          : t("settingsInstallation.cycleRecoveredCopy"))}</span>
        <dl>
          <div><dt>${escapeHtml(t("settingsInstallation.cycleFirst"))}</dt><dd>${escapeHtml(formatInstallationMonitoringEpoch("compressorCyclingAlertFirstSeen"))}</dd></div>
          <div><dt>${escapeHtml(t("settingsInstallation.cycleLast"))}</dt><dd>${escapeHtml(formatInstallationMonitoringEpoch("compressorCyclingAlertLastSeen"))}</dd></div>
          <div><dt>${escapeHtml(t("settingsInstallation.cycleHp1_2h"))}</dt><dd>${escapeHtml(t("settingsInstallation.cycleStartsSuffix", { value: hp1Peak2h }))}</dd></div>
          <div><dt>${escapeHtml(t("settingsInstallation.cycleHp1_72h"))}</dt><dd>${escapeHtml(t("settingsInstallation.cycleStartsSuffix", { value: hp1Peak72h }))}</dd></div>
          ${hp2Peak2h ? `<div><dt>${escapeHtml(t("settingsInstallation.cycleHp2_2h"))}</dt><dd>${escapeHtml(t("settingsInstallation.cycleStartsSuffix", { value: hp2Peak2h }))}</dd></div>` : ""}
          ${hp2Peak72h ? `<div><dt>${escapeHtml(t("settingsInstallation.cycleHp2_72h"))}</dt><dd>${escapeHtml(t("settingsInstallation.cycleStartsSuffix", { value: hp2Peak72h }))}</dd></div>` : ""}
          ${alternating ? `<div><dt>${escapeHtml(t("settingsInstallation.cyclePattern"))}</dt><dd>${escapeHtml(t("settingsInstallation.cyclePatternValue"))}</dd></div>` : ""}
        </dl>
        <div class="oq-settings-monitoring-incident-action">
          ${state.entities.acknowledgeCompressorCyclingAlert
            ? renderNamedActionButton(
              "acknowledgeCompressorCyclingAlert",
              t("settingsInstallation.cycleAck"),
              "oq-helper-button oq-helper-button--ghost",
              monitoring.cyclingAlertActive,
            )
            : ""}
          <span>${escapeHtml(monitoring.cyclingAlertActive
            ? t("settingsInstallation.cycleAckActive")
            : t("settingsInstallation.cycleAckRecovered"))}</span>
        </div>
      </div>
    `;
  }

  export function renderInstallationMonitoringCompressorUnit(title, prefix) {
    if (!hasEntity(`${prefix}CompressorStarts2h`)) {
      return "";
    }
    return `<tr><th scope="row">${escapeHtml(title)}</th>
      <td>${escapeHtml(formatInstallationMonitoringLastStart(`${prefix}CompressorLastStartAge`))}</td><td class="is-alarm">${escapeHtml(getInstallationMonitoringCount(`${prefix}CompressorStarts2h`))}</td>
      <td>${escapeHtml(getInstallationMonitoringCount(`${prefix}CompressorStarts6h`))}</td><td>${escapeHtml(getInstallationMonitoringCount(`${prefix}CompressorStarts24h`))}</td>
      <td class="is-alarm">${escapeHtml(getInstallationMonitoringCount(`${prefix}CompressorStarts72h`))}</td></tr>`;
  }

  export function renderSettingsInstallationMonitoringSection() {
    const monitoring = getInstallationMonitoringModel();
    syncInstallationMonitoringDetailsState(monitoring);
    const structuredIncidentMonitoringAvailable = Boolean(monitoring.incidentMonitoring?.available);
    const cicPollingEnabled = isInstallationMonitoringIntegrationEnabled("cicPollingEnabled");
    const otEnabled = isInstallationMonitoringIntegrationEnabled("otEnabled");
    const hydraulicRows = [
      hasEntity("lowflowFaultActive") ? renderInstallationMonitoringStatusRow({
        label: t("settingsInstallation.flowLabel"),
        value: isInstallationMonitoringBinaryActive("lowflowFaultActive") ? t("settingsInstallation.flowFault") : t("settingsInstallation.flowOk"),
        active: isInstallationMonitoringBinaryActive("lowflowFaultActive"),
      }) : "",
      hasEntity("flowMismatch") ? renderInstallationMonitoringStatusRow({
        label: t("settingsInstallation.flowDuoLabel"),
        value: isInstallationMonitoringBinaryActive("flowMismatch") ? t("settingsInstallation.flowDuoFault") : t("settingsInstallation.flowDuoOk"),
        active: isInstallationMonitoringBinaryActive("flowMismatch"),
      }) : "",
    ].filter(Boolean).join("");
    const connectionRows = [
      hasEntity("cicDataStale") ? renderInstallationMonitoringStatusRow({
        label: t("settingsInstallation.cicLabel"),
        value: !cicPollingEnabled
          ? t("settingsInstallation.cicOff")
          : isInstallationMonitoringBinaryActive("cicDataStale") ? t("settingsInstallation.cicStale") : t("settingsInstallation.cicOk"),
        active: cicPollingEnabled && isInstallationMonitoringBinaryActive("cicDataStale"),
      }) : "",
      hasEntity("otLinkProblem") ? renderInstallationMonitoringStatusRow({
        label: t("settingsInstallation.otLabel"),
        value: !otEnabled
          ? t("settingsInstallation.otOff")
          : isInstallationMonitoringBinaryActive("otLinkProblem") ? t("settingsInstallation.otProblem") : t("settingsInstallation.otOk"),
        active: otEnabled && isInstallationMonitoringBinaryActive("otLinkProblem"),
      }) : "",
    ].filter(Boolean).join("");
    const hpRows = structuredIncidentMonitoringAvailable ? "" : [
      hasEntity("hp1Failures") ? renderInstallationMonitoringStatusRow({
        label: t("incidents.hp1Label"),
        value: getInstallationMonitoringFailureText("hp1Failures"),
        active: isInstallationMonitoringFailureActive("hp1Failures"),
      }) : "",
      hasEntity("hp2Failures") ? renderInstallationMonitoringStatusRow({
        label: t("incidents.hp2Label"),
        value: getInstallationMonitoringFailureText("hp2Failures"),
        active: isInstallationMonitoringFailureActive("hp2Failures"),
      }) : "",
    ].filter(Boolean).join("");
    const compressorLimit2h = getEntityNumericValue("compressorStarts2hWarningLimit");
    const compressorLimit72h = getEntityNumericValue("compressorStarts72hWarningLimit");
    const compressorWarningActive = isInstallationMonitoringBinaryActive("compressorCyclingWarning2h")
      || isInstallationMonitoringBinaryActive("compressorCyclingWarning72h")
      || isInstallationMonitoringBinaryActive("alternatingCompressorStartsWarning")
      || monitoring.cyclingAlertLatched;
    const hydraulicPanel = hydraulicRows ? `
      <article class="oq-settings-monitoring-card">
        <header><p>${escapeHtml(t("settingsInstallation.hydraulicsTitle"))}</p></header>
        <div class="oq-settings-monitoring-rows">${hydraulicRows}</div>
        ${renderLowFlowDiagnosis()}
      </article>
    ` : "";
    const hpPanel = hpRows ? `
      <article class="oq-settings-monitoring-card">
        <header><p>${escapeHtml(t("settingsInstallation.hpPanelTitle"))}</p></header>
        <div class="oq-settings-monitoring-rows">${hpRows}</div>
      </article>
    ` : "";
    const structuredHpPanel = structuredIncidentMonitoringAvailable
      ? renderInstallationMonitoringStructuredHpPanel(monitoring)
      : "";
    const systemPanel = structuredIncidentMonitoringAvailable
      ? renderInstallationMonitoringSystemPanel(monitoring)
      : "";
    const connectionPanel = connectionRows ? `
      <article class="oq-settings-monitoring-card">
        <header><p>${escapeHtml(t("settingsInstallation.connectionsTitle"))}</p></header>
        <div class="oq-settings-monitoring-rows">${connectionRows}</div>
      </article>
    ` : "";

    return renderSettingsSection(
      t("settingsInstallation.sectionGroup"),
      t("settingsInstallation.sectionTitle"),
      t("settingsInstallation.sectionCopy"),
      `
        <div class="oq-settings-monitoring-summary${monitoring.severity === "fault" ? " is-fault" : monitoring.active ? " is-warning" : " is-clear"}">
          <div>
            <p>${escapeHtml(t("settingsInstallation.summaryKicker"))}</p>
            <strong>${escapeHtml(monitoring.title)}</strong>
            <span>${escapeHtml(monitoring.copy)}</span>
          </div>
          ${renderInstallationMonitoringBadge(
            monitoring.active,
            monitoring.severity === "fault"
              ? t("settingsInstallation.badgeFault")
              : monitoring.incidentMonitoringStale ? t("settingsInstallation.badgeStale") : t("settingsInstallation.badgeAttentionNeeded"),
            t("settingsInstallation.badgeCalm"),
            monitoring.severity === "fault" ? "fault" : "warning",
          )}
        </div>
        <details class="oq-settings-monitoring-details"${state.installationMonitoringDetailsOpen ? " open" : ""}>
          <summary data-oq-action="toggle-installation-monitoring-details">
            <strong>${escapeHtml(t("settingsInstallation.detailsTitle"))}</strong>
          </summary>
        ${monitoring.active ? `
          <div class="oq-settings-monitoring-active-list">
            ${monitoring.problems.map((problem) => `<span>${escapeHtml(problem.label)}${problem.copy ? ` — ${escapeHtml(problem.copy)}` : ""}</span>`).join("")}
          </div>
        ` : ""}
        <div class="oq-settings-monitoring-grid">
          <div class="oq-settings-monitoring-column">
          ${systemPanel}
          ${structuredHpPanel}
          <article class="oq-settings-monitoring-card">
            <header>
              <p>${escapeHtml(t("settingsInstallation.startsTitle"))}</p>
              ${renderInstallationMonitoringBadge(
                compressorWarningActive,
              )}
            </header>
            <span>${escapeHtml(t("settingsInstallation.startsCopy"))}</span>
            ${renderInstallationMonitoringCyclingIncident(monitoring)}
            <div class="oq-starts-panel">
              <table class="oq-starts"><thead><tr><th scope="col">${escapeHtml(t("settingsInstallation.startsColHp"))}</th><th scope="col">${escapeHtml(t("settingsInstallation.startsColLast"))}</th><th scope="col" class="is-alarm">${escapeHtml(t("settingsInstallation.startsCol2h"))}</th><th scope="col">${escapeHtml(t("settingsInstallation.startsCol6h"))}</th><th scope="col">${escapeHtml(t("settingsInstallation.startsCol24h"))}</th><th scope="col" class="is-alarm">${escapeHtml(t("settingsInstallation.startsCol72h"))}</th></tr></thead>
                <tbody>
                  ${renderInstallationMonitoringCompressorUnit(t("incidents.hp1Label"), "hp1")}
                  ${renderInstallationMonitoringCompressorUnit(t("incidents.hp2Label"), "hp2")}
                </tbody>
              </table>
              ${state.compressorLimitsOpen ? `
                <div class="oq-start-editor">
                  <strong>${escapeHtml(t("settingsInstallation.limitsTitle"))}</strong>
                  <div class="oq-start-fields" id="oq-start-fields">
                    ${renderSettingsMiniNumberField("compressorStarts2hWarningLimit", t("settingsInstallation.startsCol2h"), "", { compact: true })}
                    ${renderSettingsMiniNumberField("compressorStarts72hWarningLimit", t("settingsInstallation.startsCol72h"), "", { compact: true })}
                  </div>
                  <button type="button" class="oq-helper-button oq-helper-button--ghost oq-start-done" data-oq-action="toggle-compressor-limits" aria-expanded="true" aria-controls="oq-start-fields">${escapeHtml(t("settingsInstallation.limitsDone"))}</button>
                </div>
              ` : `
                <button type="button" class="oq-start-summary" data-oq-action="toggle-compressor-limits" aria-expanded="false">
                  <span><strong>${escapeHtml(t("settingsInstallation.limitsTitle"))}</strong><span>${escapeHtml(t("settingsInstallation.limitsSummary", { min: Number.isNaN(compressorLimit2h) ? "—" : Math.round(compressorLimit2h), max: Number.isNaN(compressorLimit72h) ? "—" : Math.round(compressorLimit72h) }))}</span></span>
                  <strong>${escapeHtml(t("settingsInstallation.limitsAdjust"))}</strong>
                </button>
              `}
            </div>
          </article>
          ${hpPanel}
          </div>
          <div class="oq-settings-monitoring-column">
            ${hydraulicPanel}
            ${connectionPanel}
          </div>
        </div>
        </details>
      `,
    );
  }

  export function renderHpGenerationField() {
    const detectionModel = getOduGenerationDetectionModel();
    const detectionStatus = renderOduGenerationDetectionStatus();
    if (!hasEntity("hpGeneration")) {
      return detectionStatus;
    }

    const descriptions = {
      V1: {
        copy: t("settingsInstallation.genV1Copy"),
        image: HP_GENERATION_IMAGE_V1,
        alt: t("settingsInstallation.genV1Alt"),
        infoTitle: "V1",
        infoCopy: t("settingsInstallation.genV1Info"),
      },
      "V1.5": {
        copy: t("settingsInstallation.genV15Copy"),
        image: HP_GENERATION_IMAGE_V1,
        alt: t("settingsInstallation.genV1Alt"),
        infoTitle: "V1.5",
        infoCopy: t("settingsInstallation.genV15Info"),
      },
      V2: {
        copy: t("settingsInstallation.genV2Copy"),
        image: HP_GENERATION_IMAGE_V2,
        alt: t("settingsInstallation.genV2Alt"),
        infoTitle: "V2",
        infoCopy: t("settingsInstallation.genV2Info"),
      },
    };

    const model = getSettingsSelectModel("hpGeneration");

    return `
      ${detectionStatus}
      <div class="oq-settings-generation-field oq-settings-field--span-2">
        <div class="oq-settings-generation-grid">
          ${model.options.map((option) => {
            const description = descriptions[option] || {};
            return renderSettingsChoiceOption({
              key: "hpGeneration",
              option,
              model,
              copy: description.copy || "",
              meta: getOduGenerationChoiceMeta(option, model.value, detectionModel.recommendation),
              image: description.image || "",
              imageAlt: description.alt || "",
              infoTitle: description.infoTitle || "",
              infoCopy: description.infoCopy || "",
              infoId: `hp-generation-${String(option).toLowerCase().replace(/[^a-z0-9]+/g, "-")}`,
            });
          }).join("")}
        </div>
      </div>
    `;
  }

  export function renderSettingsGenerationSection() {
    const currentLabel = getInstallationLabel();
    const model = getSettingsSelectModel("hpGeneration");
    const canEdit = model.available && model.options.length > 0;

    if (!currentLabel && !canEdit) {
      return "";
    }

    return renderSettingsSection(
      t("settingsInstallation.genSectionGroup"),
      t("settingsInstallation.genSectionTitle"),
      t("settingsInstallation.genSectionCopy"),
      `
        <div class="oq-helper-surface oq-settings-field">
          <div class="oq-gen-current">
            <div>
              <p class="oq-settings-quickstart-status-label">${escapeHtml(t("settingsInstallation.genCurrent"))}</p>
              <strong class="oq-settings-quickstart-status-value">${escapeHtml(currentLabel || t("settingsInstallation.genUnknown"))}</strong>
            </div>
          <button
            class="oq-helper-button oq-helper-button--ghost"
            type="button"
            data-oq-action="open-generation-modal"
            ${!canEdit || model.busy ? "disabled" : ""}
          >
            ${escapeHtml(t("settingsInstallation.genAdjust"))}
          </button>
          </div>
        </div>
      `,
    );
  }

  export function renderBoilerCvFields(
    className = "oq-settings-grid oq-settings-boiler-simple-grid",
    includeFaultFallback = false,
  ) {
    if (!hasEntity("auxHeatSourcePresent") && !hasEntity("boilerCvAssistEnabled")) {
      return "";
    }

    const separateSourcePolicyAvailable = hasEntity("auxHeatSourcePresent");
    const sourcePresenceKey = separateSourcePolicyAvailable
      ? "auxHeatSourcePresent"
      : "boilerCvAssistEnabled";
    const sourcePresent = separateSourcePolicyAvailable
      ? isEntityActive("auxHeatSourcePresent")
      : isEntityActive("boilerCvAssistEnabled");
    const sourcePresentBusy = state.loadingEntities || state.busyAction === `switch-${sourcePresenceKey}`;
    const assistSettingAvailable = hasEntity("boilerCvAssistEnabled");
    const assistEnabled = assistSettingAvailable && isEntityActive("boilerCvAssistEnabled");
    const assistBusy = state.loadingEntities || state.busyAction === "switch-boilerCvAssistEnabled";
    const boilerPowerEntityAvailable = hasEntity("boilerRatedHeatPower");
    const boilerMeta = getNumberMeta("boilerRatedHeatPower");
    const boilerValue = getInputDraftValue("boilerRatedHeatPower");
    const fallbackSettingAvailable = hasEntity("boilerFaultFallbackEnabled");
    const fallbackEnabled = fallbackSettingAvailable && isEntityActive("boilerFaultFallbackEnabled");
    const fallbackBusy = state.loadingEntities || state.busyAction === "switch-boilerFaultFallbackEnabled";
    const boilerConnectionAvailable = hasEntity("boilerConnection");
    const boilerConnection = boilerConnectionAvailable
      ? String(getEntityValue("boilerConnection") || "R1")
      : "R1";
    const openthermBoilerCapability = getBoilerOpenThermCapability({
      linkEntityPresent: hasEntity("otbLinkAvailable"),
      linkEntityConfirmedMissing: Boolean(state.optionalMissingEntities?.otbLinkAvailable),
    });
    const openthermBoilerSupported = openthermBoilerCapability === BOILER_OPENTHERM_CAPABILITY.SUPPORTED;
    const openthermBoilerCapabilityKnown = openthermBoilerCapability !== BOILER_OPENTHERM_CAPABILITY.UNKNOWN;
    const boilerConnectionMismatch =
      boilerConnection === "R1" &&
      isEntityActive("otbConnectionMismatch");
    const boilerConnectionAutoSelected =
      boilerConnection === "OpenTherm" &&
      isEntityActive("otbConnectionAutoSelected");
    const otbConnectionStateAvailable = hasEntity("otbConnectionState");
    const otbConnectionState = otbConnectionStateAvailable
      ? String(getEntityValue("otbConnectionState") || "")
      : "";
    const boilerConnectionOptions = boilerConnectionAvailable
      ? getSupportedBoilerConnectionOptions(
          getSelectEntityOptions(state.entities.boilerConnection || {}),
          openthermBoilerCapability,
        )
      : [];
    const boilerConnectionControl = boilerConnectionAvailable && openthermBoilerCapabilityKnown ? `
      <label class="oq-settings-control oq-settings-control--select">
        <select class="oq-helper-select" data-oq-field="boilerConnection" ${state.loadingEntities ? "disabled" : ""}>
          ${boilerConnectionOptions.map((option) => `
            <option value="${escapeHtml(option)}" ${option === boilerConnection ? "selected" : ""}>
              ${escapeHtml(option === "OpenTherm" ? t("settingsInstallation.boilerConnectionOt") : t("settingsInstallation.boilerConnectionR1"))}
            </option>
          `).join("")}
        </select>
        <span class="oq-settings-select-caret" aria-hidden="true"></span>
      </label>
    ` : boilerConnectionAvailable ? `
      <div class="oq-settings-boiler-power-empty" role="status" aria-live="polite">
        <strong>${escapeHtml(t("settingsInstallation.boilerCheckingTitle"))}</strong>
        <p>${escapeHtml(t("settingsInstallation.boilerCheckingCopy"))}</p>
      </div>
    ` : "";
    const boilerPowerMissingHint = t("settingsInstallation.boilerPowerMissingHint");
    const boilerPowerControl = boilerPowerEntityAvailable
      ? renderNumberInputControl({
          key: "boilerRatedHeatPower",
          value: boilerValue,
          meta: boilerMeta,
          controlClass: "oq-helper-control oq-helper-control--suffix oq-settings-boiler-power-control",
          unitMarkup: `<span class="oq-helper-unit-chip">W</span>`,
        })
      : `
        <div class="oq-settings-boiler-power-empty">
          <strong>${escapeHtml(t("settingsInstallation.boilerPowerMissingTitle"))}</strong>
          <p>${escapeHtml(boilerPowerMissingHint)}</p>
        </div>
      `;
    const boilerPowerFooter = sourcePresent && boilerPowerEntityAvailable
      ? `<p class="oq-settings-boiler-power-note">${escapeHtml(t("settingsInstallation.boilerPowerNote"))}</p>`
      : "";
    const boilerConnectionFooter = boilerConnection === "OpenTherm" && otbConnectionStateAvailable
      ? otbConnectionState === "ot_verified"
        ? `
          <div class="oq-settings-boiler-connection-note is-success" role="status" aria-live="polite">
            <strong>${escapeHtml(t("settingsInstallation.otVerifiedTitle"))}</strong>
            <p>${escapeHtml(boilerConnectionAutoSelected ? t("settingsInstallation.otVerifiedAuto") : t("settingsInstallation.otVerifiedManual"))}</p>
          </div>
        `
        : otbConnectionState === "ot_no_response"
          ? `
            <div class="oq-settings-boiler-connection-note is-warning" role="alert">
              <strong>${escapeHtml(t("settingsInstallation.otNoResponseTitle"))}</strong>
              <p>${escapeHtml(t("settingsInstallation.otNoResponseCopy"))}</p>
            </div>
          `
          : otbConnectionState === "ot_link_lost"
            ? `
              <div class="oq-settings-boiler-connection-note is-warning" role="alert">
                <strong>${escapeHtml(t("settingsInstallation.otLostTitle"))}</strong>
                <p>${escapeHtml(t("settingsInstallation.otLostCopy"))}</p>
              </div>
            `
            : `
              <div class="oq-settings-boiler-connection-note" role="status" aria-live="polite">
                <strong>${escapeHtml(t("settingsInstallation.otCheckingTitle"))}</strong>
                <p>${escapeHtml(t("settingsInstallation.otCheckingCopy"))}</p>
              </div>
            `
      : boilerConnectionAutoSelected
        ? `
          <div class="oq-settings-boiler-connection-note is-success" role="status" aria-live="polite">
            <strong>${escapeHtml(t("settingsInstallation.otVerifiedTitle"))}</strong>
            <p>${escapeHtml(t("settingsInstallation.otVerifiedAuto"))}</p>
          </div>
        `
        : boilerConnection === "R1" && openthermBoilerSupported
          ? boilerConnectionMismatch
            ? `
              <div class="oq-settings-boiler-connection-note is-warning" role="alert">
                <strong>${escapeHtml(t("settingsInstallation.otFoundTitle"))}</strong>
                <p>${escapeHtml(t("settingsInstallation.otFoundCopy"))}</p>
              </div>
            `
            : `<p class="oq-settings-boiler-connection-note">${escapeHtml(t("settingsInstallation.otCheckActive"))}</p>`
          : "";
    const supportSwitchingFields = !isCurveMode() && sourcePresent && assistEnabled
      ? [
          renderSettingsNumberField(
            "boilerSupportStartThreshold",
            t("settingsInstallation.supportStartTitle"),
            t("settingsInstallation.supportStartCopy"),
          ),
          renderSettingsNumberField(
            "boilerSupportStopThreshold",
            t("settingsInstallation.supportStopTitle"),
            t("settingsInstallation.supportStopCopy"),
          ),
        ].filter(Boolean).join("")
      : "";
    const supportSwitchingMarkup = renderSettingsAdvancedDisclosure(
      "boiler-support",
      t("settingsInstallation.supportAdvancedTitle"),
      t("settingsInstallation.supportAdvancedCopy"),
      supportSwitchingFields ? `<div class="oq-settings-grid">${supportSwitchingFields}</div>` : "",
    );

    return `
        <div class="${escapeHtml(className)}">
          ${renderSettingsFieldCard(
            sourcePresenceKey,
            t("settingsInstallation.sourceTitle"),
            t("settingsInstallation.sourceCopy"),
            `
              <div class="oq-settings-compact-switch-field">
                ${renderSettingsCompactSwitchControl(sourcePresenceKey, t("settingsInstallation.sourceTitle"), sourcePresent, sourcePresentBusy)}
              </div>
            `,
            "oq-settings-field--compact",
          )}

          ${(sourcePresent || boilerConnectionMismatch || boilerConnectionAutoSelected) && boilerConnectionAvailable ? renderSettingsFieldCard(
            "boilerConnection",
            t("settingsInstallation.connectionTitle"),
            !openthermBoilerCapabilityKnown
              ? t("settingsInstallation.connectionChecking")
              : openthermBoilerSupported
              ? t("settingsInstallation.connectionBoth")
              : t("settingsInstallation.connectionR1Only"),
            boilerConnectionControl,
            "oq-settings-field--compact",
            boilerConnectionFooter,
          ) : ""}

          ${sourcePresent ? renderSettingsFieldCard(
            "boilerRatedHeatPower",
            t("settingsInstallation.powerTitle"),
            t("settingsInstallation.powerCopy"),
            `
              <div class="oq-settings-boiler-power-inline">
                ${boilerPowerControl}
              </div>
            `,
            sourcePresent && boilerPowerEntityAvailable ? "oq-settings-field--compact" : "oq-settings-field--compact is-disabled",
            boilerPowerFooter,
          ) : ""}
          ${sourcePresent && separateSourcePolicyAvailable && assistSettingAvailable ? renderSettingsFieldCard(
            "boilerCvAssistEnabled",
            t("settingsInstallation.assistTitle"),
            t("settingsInstallation.assistCopy"),
            `
              <div class="oq-settings-compact-switch-field">
                ${renderSettingsCompactSwitchControl(
                  "boilerCvAssistEnabled",
                  t("settingsInstallation.assistTitle"),
                  assistEnabled,
                  assistBusy,
                )}
              </div>
            `,
            "oq-settings-field--compact",
          ) : ""}
          ${sourcePresent && includeFaultFallback && fallbackSettingAvailable ? renderSettingsFieldCard(
            "boilerFaultFallbackEnabled",
            t("settingsInstallation.backupTitle"),
            t("settingsInstallation.backupCopy"),
            `
              <div class="oq-settings-compact-switch-field">
                ${renderSettingsCompactSwitchControl(
                  "boilerFaultFallbackEnabled",
                  t("settingsInstallation.backupTitle"),
                  fallbackEnabled,
                  fallbackBusy,
                )}
              </div>
            `,
            "oq-settings-field--compact",
          ) : ""}

          ${supportSwitchingMarkup}
        </div>
      `;
  }

  export function renderSettingsBoilerCvSection() {
    if (!hasEntity("auxHeatSourcePresent") && !hasEntity("boilerCvAssistEnabled")) {
      return "";
    }

    const sourcePresent = hasEntity("auxHeatSourcePresent")
      ? isEntityActive("auxHeatSourcePresent")
      : isEntityActive("boilerCvAssistEnabled");
    return renderSettingsSection(
      t("settingsInstallation.boilerSectionGroup"),
      t("settingsInstallation.boilerSectionTitle"),
      sourcePresent
        ? t("settingsInstallation.boilerSectionCopyOn")
        : t("settingsInstallation.boilerSectionCopyOff"),
      renderBoilerCvFields("oq-settings-grid oq-settings-boiler-simple-grid", true),
    );
  }

  export function formatAuxRelayStatus(status) {
    const value = String(status || "").trim();
    if (!value) {
      return "";
    }

    const labels = {
      Disabled: t("settingsInstallation.auxDisabled"),
      "No thermal demand": t("settingsInstallation.auxNoThermal"),
      "No heating demand": t("settingsInstallation.auxNoHeating"),
      "No cooling demand": t("settingsInstallation.auxNoCooling"),
      "Heating demand active": t("settingsInstallation.auxHeatingActive"),
      "Cooling demand active": t("settingsInstallation.auxCoolingActive"),
      "External control": t("settingsInstallation.auxExternal"),
      "Waiting for warm water": t("settingsInstallation.auxWaitWarm"),
      "Waiting for cold water": t("settingsInstallation.auxWaitCold"),
      "Supply temperature unavailable": t("settingsInstallation.auxSupplyUnavailable"),
    };

    return labels[value] || value;
  }

  export function renderSettingsAuxRelaySection() {
    if (!hasEntity("auxRelayFunction")) {
      return "";
    }

    const functionValue = String(getEntityValue("auxRelayFunction") || "Disabled");
    const demandFunctionSelected = functionValue !== "Disabled" && functionValue !== "External control";
    const tempGateEnabled = demandFunctionSelected && hasEntity("auxWaitForSupplyTemp") && isEntityActive("auxWaitForSupplyTemp");
    const relayOn = hasEntity("auxRelayActive") && isEntityActive("auxRelayActive");
    const statusText = hasEntity("auxRelayStatus") ? formatAuxRelayStatus(getEntityStateText("auxRelayStatus", "")) : "";
    const statusPanel = hasEntity("auxRelayActive") || statusText ? renderSettingsFieldCard(
      "auxRelayStatus",
      t("settingsInstallation.auxStatusTitle"),
      t("settingsInstallation.auxStatusCopy"),
      `
        <div class="oq-settings-aux-relay-status">
          <strong>${escapeHtml(relayOn ? t("settingsInstallation.auxRelayOn") : t("settingsInstallation.auxRelayOff"))}</strong>
          ${statusText ? `<p>${escapeHtml(statusText)}</p>` : ""}
        </div>
      `,
    ) : "";
    const fields = [
      renderSettingsSelectField(
        "auxRelayFunction",
        t("settingsInstallation.auxFunctionTitle"),
        t("settingsInstallation.auxFunctionCopy"),
      ),
      statusPanel,
      demandFunctionSelected ? renderSettingsSwitchField(
        "auxWaitForSupplyTemp",
        t("settingsInstallation.auxWaitTitle"),
        t("settingsInstallation.auxWaitOn"),
        t("settingsInstallation.auxWaitOffOn"),
        t("settingsInstallation.auxWaitOffOff"),
        "oq-settings-field--span-2",
      ) : "",
      tempGateEnabled ? renderSettingsNumberField("auxHeatingStartTemp", t("settingsInstallation.auxHeatStartTitle"), t("settingsInstallation.auxHeatStartCopy")) : "",
      tempGateEnabled ? renderSettingsNumberField("auxCoolingStartTemp", t("settingsInstallation.auxCoolStartTitle"), t("settingsInstallation.auxCoolStartCopy")) : "",
      tempGateEnabled ? renderSettingsNumberField("auxTempHysteresis", t("settingsInstallation.auxHysteresisTitle"), t("settingsInstallation.auxHysteresisCopy")) : "",
    ].filter(Boolean);

    return renderSettingsSection(
      t("settingsInstallation.auxSectionGroup"),
      t("settingsInstallation.auxSectionTitle"),
      t("settingsInstallation.auxSectionCopy"),
      `
        <div class="oq-settings-grid">
          ${fields.join("")}
        </div>
      `,
    );
  }

  export function renderSettingsQuickStartSection() {
    const statusLabel = state.complete === true ? t("settings.qsDone") : state.complete === false ? t("settings.qsOpen") : t("settings.qsLoading");
    const statusCopy = state.complete === true
      ? t("settings.qsDoneCopy")
      : state.complete === false
        ? t("settings.qsOpenCopy")
        : t("settings.qsLoadingCopy");

    return renderSettingsSection(
      t("settingsInstallation.qsSectionGroup"),
      t("settingsInstallation.qsSectionTitle"),
      t("settingsInstallation.qsSectionCopy"),
      `
        <div class="oq-settings-quickstart-status">
          <div class="oq-settings-quickstart-status-row">
            <div>
              <p class="oq-settings-quickstart-status-label">${escapeHtml(t("settings.qsCurrentStatus"))}</p>
              <strong class="oq-settings-quickstart-status-value">${escapeHtml(statusLabel)}</strong>
            </div>
            <button
              class="oq-helper-button oq-helper-button--ghost"
              type="button"
              data-oq-action="reset"
              ${state.busyAction === "reset" ? "disabled" : ""}
            >
              ${escapeHtml(t("settings.qsReset"))}
            </button>
          </div>
          <p class="oq-settings-quickstart-status-copy">${escapeHtml(statusCopy)}</p>
        </div>
      `,
    );
  }

  function renderSettingsSystemOpenAction(action) {
    return `<button
      class="oq-helper-button oq-helper-button--ghost"
      type="button"
      data-oq-action="${escapeHtml(action)}"
    >
      ${escapeHtml(t("settingsInstallation.openAction"))}
    </button>`;
  }

  export function renderSettingsDiagnosticsSection() {
    const updateStatus = getUpdateStatus();
    const dateTime = formatDiagnosticsDateTime();
    const busyRestart = state.busyAction === "restartAction";
    const busyFactoryReset = state.busyAction === "factoryResetButton";
    const activeConnection = hasEntity("connectionText")
      ? getEntityStateText("connectionText", t("header.connNotConnected")).replace("Not connected", t("header.connNotConnected"))
      : getConnectivityStatus();
    const ipAddress = getDeviceIpAddress();

    return renderSettingsSection(
      t("settingsInstallation.diagSectionGroup"),
      t("settingsInstallation.diagSectionTitle"),
      t("settingsInstallation.diagSectionCopy"),
      `
        <div class="oq-settings-system-summary">
          ${renderSettingsSystemRow({ dataValue: "uptime", label: t("settingsInstallation.diagUptime"), value: formatUptimeFromMeta() })}
          ${renderSettingsSystemRow({
            dataValue: "connectivity",
            label: t("settingsInstallation.diagConnectivity"),
            value: activeConnection,
            note: ipAddress === "—" ? "" : t("settingsInstallation.diagIpPrefix", { ip: ipAddress }),
            action: renderSettingsSystemOpenAction("open-connectivity-modal"),
          })}
          ${renderSettingsSystemRow({
            dataValue: "updates",
            label: t("settingsInstallation.diagUpdates"),
            value: updateStatus,
            action: renderSettingsSystemOpenAction("open-update-modal"),
          })}
          ${renderSettingsSystemRow({
            dataValue: "webserverLog",
            label: t("settingsInstallation.diagLog"),
            value: getWebServerLogStatusLabel(),
            action: renderSettingsSystemOpenAction("open-webserver-log-modal"),
          })}
          ${renderSettingsSystemRow({
            dataValue: "debugRecording",
            label: t("settingsInstallation.diagRecorder"),
            value: getDebugRecordingStatusLabel(),
            note: getDebugRecordingStatusCopy(),
            action: renderSettingsSystemOpenAction("open-debug-recording-modal"),
          })}
          ${renderSettingsSystemRow({ dataValue: "datetime", label: t("settingsInstallation.diagDatetime"), value: dateTime })}
          ${renderSettingsSystemRow({ dataValue: "espTemp", label: t("settingsInstallation.diagEspTemp"), value: getEspTemperatureLabel() })}
          ${renderSettingsSystemRow({
            dataValue: "restart",
            label: t("settingsInstallation.diagRestart"),
            value: t("settingsInstallation.diagRestartValue"),
            note: t("settingsInstallation.diagRestartNote"),
            action: `<button
              class="oq-helper-button oq-helper-button--warning"
              type="button"
              data-oq-action="open-restart-confirm"
              ${busyRestart ? "disabled" : ""}
            >
              ${busyRestart ? escapeHtml(t("settingsInstallation.diagRestartBusy")) : escapeHtml(t("settingsInstallation.diagRestartNow"))}
            </button>`,
          })}
          ${hasEntity("factoryResetButton") ? renderSettingsSystemRow({
            dataValue: "factory-reset",
            label: t("settingsInstallation.diagFactory"),
            value: t("settingsInstallation.diagFactoryValue"),
            note: t("settingsInstallation.diagFactoryNote"),
            action: `<button
              class="oq-helper-button oq-helper-button--warning"
              type="button"
              data-oq-action="open-factory-reset-confirm"
              ${busyFactoryReset ? "disabled" : ""}
            >
              ${busyFactoryReset ? escapeHtml(t("settingsInstallation.diagFactoryBusy")) : escapeHtml(t("settingsInstallation.diagFactory"))}
            </button>`,
          }) : ""}
          ${hasEntity("statusLedsEnabled") ? `
            ${renderSettingsSystemRow({
              dataValue: "statusLeds",
              label: t("settingsInstallation.diagLeds"),
              value: isEntityActive("statusLedsEnabled") ? t("common.on") : t("common.off"),
              note: t("settingsInstallation.diagLedsNote"),
              action: renderSettingsCompactSwitchControl(
                "statusLedsEnabled",
                t("settingsInstallation.diagLeds"),
                isEntityActive("statusLedsEnabled"),
                state.loadingEntities || state.busyAction === "switch-statusLedsEnabled",
              ),
            })}
          ` : ""}
        </div>
      `,
    );
  }

  export function renderSettingsCompressorSection() {
    const hpGroups = [
      renderSettingsHeatPumpLimiterCard(t("incidents.hp1Label"), "hp1"),
      renderSettingsHeatPumpLimiterCard(t("incidents.hp2Label"), "hp2"),
    ].filter(Boolean).join("");

    return renderSettingsSection(
      t("settingsInstallation.compressorSectionGroup"),
      t("settingsInstallation.compressorTitle"),
      t("settingsInstallation.compressorCopy"),
      `
        <div class="oq-settings-subpanel">
          <div class="oq-settings-subpanel-head">
            <p class="oq-helper-label">${escapeHtml(t("settingsInstallation.compressorRuntimeKicker"))}</p>
            <h4>${escapeHtml(t("settingsInstallation.compressorRuntimeTitle"))}</h4>
            <p>${escapeHtml(t("settingsInstallation.compressorRuntimeCopy"))}</p>
          </div>
          <div class="oq-settings-grid">
            ${renderSettingsNumberField("minRuntime", t("settingsInstallation.compressorRuntimeTitle"), t("settingsInstallation.compressorRuntimeFieldCopy"))}
          </div>
        </div>
        <div class="oq-settings-subpanel oq-settings-subpanel--nested">
          <div class="oq-settings-subpanel-head">
            <p class="oq-helper-label">${escapeHtml(t("settingsInstallation.compressorExclKicker"))}</p>
            <h4>${escapeHtml(t("settingsInstallation.compressorExclTitle"))}</h4>
            <p>${escapeHtml(t("settingsInstallation.compressorExclCopy"))}</p>
          </div>
          <div class="oq-settings-hp-columns${hasEntity("hp2ExcludeMinHz") ? "" : " oq-settings-hp-columns--single"}">
            ${hpGroups}
          </div>
        </div>
      `,
    );
  }
