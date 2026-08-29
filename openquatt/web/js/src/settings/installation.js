import { getEntityNumericValue, getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";
import { getOduRuntimeFrequencyControlKey, getOduRuntimeFrequencyValueKey, ODU_RUNTIME_FREQUENCY_HP_IDS, ODU_RUNTIME_FREQUENCY_LEVELS, ODU_RUNTIME_FREQUENCY_MODES } from "../core/config.js";
import { HP_GENERATION_IMAGE_V1, HP_GENERATION_IMAGE_V2 } from "../core/embedded-assets.js";
import { getInputDraftValue } from "../core/control-drafts.js";
import { isCurveMode } from "../core/domain-helpers.js";
import { getEntityValue, getNumberMeta, parseLooseNumber } from "../core/entity-store.js";
import { formatIncidentOccurrenceTime, getFallbackBlockReasonLabel, getHeatPumpStatusPresentation, getIncidentActionPresentation, getIncidentCategoryLabel, getIncidentDisplayLabel, getIncidentEffectLabels, getIncidentLifecyclePresentation, getIncidentRecoveryLabel, getIncidentTechnicalCode, getIncidentUserActionLabel, getPumpIncidentContextRows, getSystemActionPresentation } from "../core/incident-monitoring.js";
import { getInstallationMonitoringFailureText, getInstallationMonitoringModel, isInstallationMonitoringBinaryActive, isInstallationMonitoringFailureActive, isInstallationMonitoringIntegrationEnabled, syncInstallationMonitoringDetailsState } from "../core/installation-monitoring.js";
import { renderNumberInputControl } from "../core/number-controls.js";
import { state } from "../core/state.js";
import { getDebugRecordingStatusCopy, getDebugRecordingStatusLabel } from "../features/debug-recording.js";
import { formatDiagnosticsDateTime, formatUptimeFromMeta, getDeviceIpAddress, getInstallationLabel } from "../features/device-context.js";
import { getUpdateStatus } from "../features/firmware-update.js";
import { getConnectivityStatus, getEspTemperatureLabel } from "../features/header-status.js";
import { getOduGenerationChoiceMeta, getOduGenerationDetectionModel, renderOduGenerationDetectionStatus } from "../features/odu-generation-ui.js";
import { getWebServerLogStatusLabel } from "../features/webserver-logs.js";
import { BOILER_OPENTHERM_CAPABILITY, getBoilerOpenThermCapability, getSupportedBoilerConnectionOptions } from "./boiler.js";
import { getSelectEntityOptions, renderNamedActionButton, renderSettingsAdvancedDisclosure, renderSettingsChoiceOption, renderSettingsCompactSwitchControl, renderSettingsFieldCard, renderSettingsMiniNumberField, renderSettingsNumberField, renderSettingsSection, renderSettingsSelectField, renderSettingsSwitchField, renderSettingsSystemRow } from "./controls.js";
import { renderSettingsHeatPumpLimiterCard } from "./heating.js";
import { escapeHtml } from "../core/html.js";

const AUX_HEAT_ASSIST_TITLE = "Hybride verwarmen bij vermogenstekort";
const AUX_HEAT_BACKUP_TITLE = "Overnemen wanneer de warmtepomp niet beschikbaar is";
const AUX_HEAT_BACKUP_COPY = "Laat de warmtebron tijdelijk overnemen wanneer geen warmtepomp veilig beschikbaar is, ook bij een koude opstart onder 5 °C. Dit gebeurt pas na een veilige stop en geldige flow, temperatuur en aansturing. Een korte communicatiedip telt niet als uitval.";

  export function getOduRuntimeFrequencyHpIndexes() {
    return ODU_RUNTIME_FREQUENCY_HP_IDS.filter((hpIndex) => (
      hasEntity(getOduRuntimeFrequencyControlKey(hpIndex, "Status"))
      || hasEntity(getOduRuntimeFrequencyControlKey(hpIndex, "Load"))
      || hasEntity(getOduRuntimeFrequencyValueKey(hpIndex, "cooling", 0))
    ));
  }

  export function getOduRuntimeFrequencyNumberValue(key) {
    return parseLooseNumber(getInputDraftValue(key));
  }

  export function getOduRuntimeFrequencyLevels(hpIndex) {
    const variantKey = hpIndex === 2 ? "hp2GenerationVariant" : "hp1GenerationVariant";
    const extendedLayout = String(getEntityValue(variantKey) || "").trim() === "V2 new model";
    return ODU_RUNTIME_FREQUENCY_LEVELS.slice(0, extendedLayout ? 21 : 11);
  }

  export function getOduRuntimeFrequencyTableValidation(hpIndex) {
    const invalid = [];
    const levels = getOduRuntimeFrequencyLevels(hpIndex);
    ODU_RUNTIME_FREQUENCY_MODES.forEach((mode) => {
      let previous = -Infinity;
      levels.forEach((level) => {
        const key = getOduRuntimeFrequencyValueKey(hpIndex, mode, level);
        const value = getOduRuntimeFrequencyNumberValue(key);
        const invalidOffLevel = level === 0 ? value !== 0 : value <= 0;
        if (!Number.isFinite(value) || invalidOffLevel || value > 120 || value < previous) {
          invalid.push(`${mode === "cooling" ? "C" : "H"}F${level}`);
        }
        if (Number.isFinite(value)) {
          previous = value;
        }
      });
    });
    return {
      valid: invalid.length === 0,
      invalid,
    };
  }

  export function getOduRuntimeFrequencyOperationState(hpIndex) {
    const mode = String(getEntityValue(`hp${hpIndex}Mode`) || "").trim();
    const freq = parseLooseNumber(getEntityValue(`hp${hpIndex}Freq`));
    const modeKnown = mode && mode !== "Onbekend" && mode !== "Unknown";
    const freqKnown = Number.isFinite(freq);
    const standby = modeKnown && /standby|stand-by/i.test(mode);
    const stopped = freqKnown && freq <= 0.5;
    const reason = !modeKnown
      ? "ODU status is onbekend."
      : !standby
        ? `ODU staat in ${mode}.`
        : !freqKnown
          ? "Compressorfrequentie is onbekend."
          : !stopped
            ? `Compressor draait op ${freq.toFixed(0)} Hz.`
            : "Standby en compressor uit.";
    return {
      mode: modeKnown ? mode : "Onbekend",
      freq: Number.isFinite(freq) ? `${freq.toFixed(0)} Hz` : "Onbekend",
      safe: standby && stopped,
      reason,
    };
  }

  export function getOduRuntimeFrequencyStatusCopy(status) {
    const normalized = String(status || "").toUpperCase();
    if (!status || normalized === "UNKNOWN" || normalized === "UNAVAILABLE") {
      return "Nog geen readback of apply-status ontvangen.";
    }
    if (normalized.includes("APPLIED")) {
      return "Runtime registers zijn geschreven en via readback bevestigd. Een power-cycle / stroomloos maken van de buitenunit zet de originele tabel terug.";
    }
    if (normalized.includes("GUARD_READ_REQUESTED")) {
      return "Firmware leest actuele ODU mode en compressorfrequentie voordat er geschreven wordt.";
    }
    if (normalized.includes("WRITE_QUEUED") || normalized.includes("WRITE_CONFIRMED")) {
      return "Runtime write loopt; wacht op bevestigde readback voordat je de waarden vertrouwt.";
    }
    if (normalized.includes("FAILED")) {
      return "Firmware kon de runtime tabel niet volledig bevestigen. Laad opnieuw voordat je verder test.";
    }
    if (normalized.includes("LOADED")) {
      return "Readback is in de velden geladen. Controleer de waarden voordat je schrijft.";
    }
    if (normalized.includes("BLOCKED")) {
      return "Firmware heeft de actie geblokkeerd; controleer enable, standby en compressorstatus.";
    }
    if (normalized.includes("LOAD_REQUESTED")) {
      return "Readback is aangevraagd bij de ODU.";
    }
    return "Laatste status van de experimentele runtime tabel.";
  }

  export function renderOduRuntimeFrequencyNumberInput(key, tabIndex) {
    if (!hasEntity(key)) {
      return `<span class="oq-settings-odu-runtime-missing">-</span>`;
    }
    return renderNumberInputControl({
      key,
      value: getInputDraftValue(key),
      meta: getNumberMeta(key),
      controlClass: "oq-helper-control oq-helper-control--suffix oq-settings-odu-runtime-control",
      inputClass: "oq-helper-input oq-helper-input--compact-number oq-settings-odu-runtime-input",
      inputAttributes: `data-oq-odu-runtime-tab-index="${tabIndex}"`,
      unitMarkup: '<span class="oq-helper-unit-chip">Hz</span>',
    });
  }

  export function renderOduRuntimeFrequencyTable(hpIndex) {
    const levels = getOduRuntimeFrequencyLevels(hpIndex);
    const levelCount = levels.length;
    return `
      <div class="oq-settings-odu-runtime-table" role="table" aria-label="${escapeHtml(`HP${hpIndex} ODU runtime frequentietabel`)}">
        <div class="oq-settings-odu-runtime-row oq-settings-odu-runtime-row--head" role="row">
          <span role="columnheader">Level</span>
          <span role="columnheader">Cooling</span>
          <span role="columnheader">Heating</span>
        </div>
        ${levels.map((level) => `
          <div class="oq-settings-odu-runtime-row" role="row">
            <span class="oq-settings-odu-runtime-level" role="cell">F${level}</span>
            <div role="cell">${renderOduRuntimeFrequencyNumberInput(getOduRuntimeFrequencyValueKey(hpIndex, "cooling", level), level)}</div>
            <div role="cell">${renderOduRuntimeFrequencyNumberInput(getOduRuntimeFrequencyValueKey(hpIndex, "heating", level), levelCount + level)}</div>
          </div>
        `).join("")}
      </div>
    `;
  }

  export function handleOduRuntimeFrequencyInputKeyDown(event) {
    if (event.key !== "Tab" || event.altKey || event.ctrlKey || event.metaKey) {
      return;
    }

    const input = event.target && event.target.closest
      ? event.target.closest("input[data-oq-odu-runtime-tab-index]")
      : null;
    const table = input ? input.closest(".oq-settings-odu-runtime-table") : null;
    if (!input || !table) {
      return;
    }

    const inputs = Array.from(table.querySelectorAll("input[data-oq-odu-runtime-tab-index]:not(:disabled)"))
      .sort((left, right) => Number(left.dataset.oqOduRuntimeTabIndex || 0) - Number(right.dataset.oqOduRuntimeTabIndex || 0));
    const currentIndex = inputs.indexOf(input);
    const nextInput = inputs[currentIndex + (event.shiftKey ? -1 : 1)];
    if (currentIndex < 0 || !nextInput) {
      return;
    }

    event.preventDefault();
    nextInput.focus();
    if (typeof nextInput.select === "function") {
      nextInput.select();
    }
  }

  export function renderOduRuntimeFrequencyHpPanel(hpIndex) {
    const enableKey = getOduRuntimeFrequencyControlKey(hpIndex, "Enable");
    const loadKey = getOduRuntimeFrequencyControlKey(hpIndex, "Load");
    const applyKey = getOduRuntimeFrequencyControlKey(hpIndex, "Apply");
    const statusKey = getOduRuntimeFrequencyControlKey(hpIndex, "Status");
    const status = String(getEntityValue(statusKey) || "").trim() || "Nog niet geladen";
    const validation = getOduRuntimeFrequencyTableValidation(hpIndex);
    const operation = getOduRuntimeFrequencyOperationState(hpIndex);
    const enabled = Boolean(getEntityValue(enableKey));
    const busy = state.loadingEntities || state.busyAction === loadKey || state.busyAction === applyKey;
    const applyDisabled = busy || !enabled || !validation.valid || !operation.safe || !hasEntity(applyKey);
    const validationText = validation.valid
      ? "F0 is 0 Hz; F1 en hoger zijn 1-120 Hz en per tabel oplopend."
      : `Controleer ${validation.invalid.slice(0, 5).join(", ")}${validation.invalid.length > 5 ? "..." : ""}.`;

    return `
      <article class="oq-settings-odu-runtime-panel">
        <div class="oq-settings-odu-runtime-panel-head">
          <div>
            <p class="oq-helper-label">HP${hpIndex}</p>
            <h4>Runtime frequentietabel</h4>
            <p>${escapeHtml(operation.reason)} Laatste compressorfrequentie: ${escapeHtml(operation.freq)}.</p>
            <p>${getOduRuntimeFrequencyLevels(hpIndex).length === 21 ? "Volledig compressorbereik beschikbaar." : "Standaard compressorbereik beschikbaar."}</p>
          </div>
          <div class="oq-settings-odu-runtime-actions">
            ${hasEntity(loadKey) ? renderNamedActionButton(loadKey, state.busyAction === loadKey ? "Lezen..." : "Uit ODU laden", "oq-helper-button oq-helper-button--ghost", busy) : ""}
      ${hasEntity(enableKey) ? renderSettingsCompactSwitchControl(enableKey, `HP${hpIndex} writes vrijgeven`, enabled, busy, "Enable", "Locked") : ""}
            ${hasEntity(applyKey) ? renderNamedActionButton(applyKey, state.busyAction === applyKey ? "Schrijven..." : "Runtime toepassen", "oq-helper-button oq-helper-button--warning", applyDisabled) : ""}
          </div>
        </div>
        <div class="oq-settings-odu-runtime-status${status.toUpperCase().includes("BLOCKED") ? " is-warning" : status.toUpperCase().includes("APPLIED") || status.toUpperCase().includes("LOADED") ? " is-success" : ""}">
          <div>
            <span>Status</span>
            <strong>${escapeHtml(status)}</strong>
          </div>
          <p>${escapeHtml(getOduRuntimeFrequencyStatusCopy(status))}</p>
        </div>
        ${renderOduRuntimeFrequencyTable(hpIndex)}
        <p class="oq-settings-odu-runtime-validation${validation.valid && operation.safe ? " is-ok" : " is-warning"}">${escapeHtml(validationText)} ${escapeHtml(operation.safe ? "" : operation.reason)}</p>
      </article>
    `;
  }

  export function renderSettingsOduRuntimeFrequencySection() {
    const hpIndexes = getOduRuntimeFrequencyHpIndexes();
    if (!hpIndexes.length) {
      return "";
    }

    return `
      <details class="oq-settings-section oq-settings-section--collapsible oq-settings-odu-runtime-details"${state.oduRuntimeFrequencyDetailsOpen ? " open" : ""}>
        <summary class="oq-settings-section-summary" data-oq-action="toggle-odu-runtime-frequency-details">
          <div class="oq-settings-section-head">
            <div class="oq-settings-section-head-meta">
              <p class="oq-helper-label">Experimenteel</p>
              <div class="oq-settings-section-head-meta-badge">
                <span class="oq-settings-section-badge oq-settings-section-badge--experimental">Runtime only</span>
              </div>
            </div>
            <h3>ODU runtime frequentietabel</h3>
            <p>Lees en schrijf de ODU frequentietabel alleen runtime; waarden worden niet opgeslagen in EEPROM. Een power-cycle / stroomloos maken van de buitenunit reset de frequentietabel weer naar de originele tabel.</p>
          </div>
          <span class="oq-settings-section-summary-toggle" aria-hidden="true"></span>
        </summary>
        <div class="oq-settings-section-collapsible-body oq-settings-odu-runtime">
          <div class="oq-settings-odu-runtime-warning" role="alert">
            <strong>Schrijft direct naar ODU runtime registers.</strong>
            <p>Gebruik dit alleen voor gecontroleerde tests. Apply werkt alleen wanneer de HP in standby staat, de compressor uit is en de enable-schakelaar bewust aan staat.</p>
            <p>Verlaag koel-frequenties onder de OEM-ondergrens rond 30 Hz alleen met superheat-bewaking. Bij te lage suction superheat kan natte zuigretour richting compressor ontstaan.</p>
            <p>Een power-cycle / stroomloos maken van de buitenunit reset de frequentietabel weer naar de originele tabel.</p>
          </div>
          <div class="oq-settings-odu-runtime-panels">
            ${hpIndexes.map((hpIndex) => renderOduRuntimeFrequencyHpPanel(hpIndex)).join("")}
          </div>
        </div>
      </details>
    `;
  }

  export function renderInstallationMonitoringBadge(
    active,
    activeLabel = "Aandacht",
    clearLabel = "OK",
    activeTone = "warning",
  ) {
    const tone = active ? activeTone : "clear";
    return `<span class="oq-settings-monitoring-badge is-${escapeHtml(tone)}">${escapeHtml(active ? activeLabel : clearLabel)}</span>`;
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

  export function renderInstallationMonitoringHpIncident(incident, pumpContext = null) {
    const lifecycle = getIncidentLifecyclePresentation(incident);
    const effects = getIncidentEffectLabels(incident.effects);
    const firstSeen = formatIncidentOccurrenceTime(incident.firstSeenS, incident.firstSeenMs);
    const lastSeen = formatIncidentOccurrenceTime(incident.lastSeenS, incident.lastSeenMs);
    const technicalCode = getIncidentTechnicalCode(incident);
    const details = [
      technicalCode ? ["ODU-code", technicalCode] : null,
      incident.technicalDescription ? ["ODU-omschrijving", incident.technicalDescription] : null,
      effects.length ? ["Effect", effects.join(", ")] : null,
      firstSeen ? ["Eerste optreden", firstSeen] : null,
      lastSeen ? ["Laatste optreden", lastSeen] : null,
      incident.recoveryCondition ? ["Herstel", getIncidentRecoveryLabel(incident.recoveryCondition)] : null,
      getIncidentUserActionLabel(incident.userAction)
        ? ["Gebruikersactie", getIncidentUserActionLabel(incident.userAction)]
        : null,
      incident.occurrenceCount > 1 ? ["Bevestigd", `${incident.occurrenceCount} keer sinds controllerstart`] : null,
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
            <p>Warmtepomp ${escapeHtml(heatPump.index)}</p>
            <strong>${escapeHtml(presentation.label)}</strong>
            <span>${escapeHtml(presentation.note)}</span>
          </div>
          ${renderInstallationMonitoringBadge(
            presentation.tone !== "clear",
            presentation.tone === "fault" ? "Niet beschikbaar" : "Begrensd",
            presentation.label,
            presentation.tone,
          )}
        </div>
        ${incidents.map((incident) => renderInstallationMonitoringHpIncident(
          incident,
          heatPump.pumpContext,
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
                  ? "Retry wordt verwerkt..."
                  : `Startretry HP${heatPump.index}`}</button>
              <span>Alleen na een bevestigde veilige stop; actieve fouten, verbindingsherstel en andere startblokkades blijven gelden.</span>
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
                  ? "Bevestiging wordt verwerkt..."
                  : `ODU-powercycle HP${heatPump.index} bevestigen`}</button>
              <span>Alleen nadat buitenunit HP${escapeHtml(heatPump.index)} werkelijk spanningsloos is geweest; dit geeft uitsluitend de herstelde safety-latch van deze HP vrij.</span>
            </div>
          </div>
        ` : ""}
        ${incidentAction.visible ? `
          <div class="oq-settings-monitoring-incident">
            <div class="oq-settings-monitoring-incident-head">
              <div>
                <p>Herstelactie</p>
                <strong>${escapeHtml(incidentAction.label)}</strong>
              </div>
              ${renderInstallationMonitoringBadge(
                incidentAction.tone !== "clear",
                incidentAction.tone === "fault" ? "Geweigerd" : "In behandeling",
                "Uitgevoerd",
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
          <p>Warmtepompen</p>
          ${renderInstallationMonitoringBadge(
            visibleIncidents.some((incident) => incident.category !== "status"),
            "Incident",
            "OK",
            "warning",
          )}
        </header>
        <span>Bevestigde status per warmtepomp. Een korte communicatiehapering wordt eerst gecontroleerd voordat OpenQuatt ingrijpt.</span>
        <div class="oq-settings-monitoring-rows">
          ${heatPumps.map(renderInstallationMonitoringHeatPumpUnit).join("")}
        </div>
        ${acknowledgeableIncidents.length > 0 && hasEntity("acknowledgeHpIncidents") ? `
          <div class="oq-settings-monitoring-incident-action">
            ${renderNamedActionButton(
              "acknowledgeHpIncidents",
              "Herstelde meldingen bevestigen",
              "oq-helper-button oq-helper-button--ghost",
            )}
            <span>Alleen herstelde, vastgehouden meldingen verdwijnen; actieve incidenten blijven staan.</span>
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
          title: "CM3 · CV ondersteunt",
          copy: "De warmtepomp blijft de primaire warmtebron; de CV-ketel vult tijdelijk aan.",
          tone: "clear",
        }
        : {
          title: "CM3 · ondersteuningsrol niet actief",
          copy: "CM3 is de regelrol, maar de ketel wordt op dit moment niet aangestuurd.",
          tone: "warning",
        };
    }
    if (controlMode === 4) {
      return commandActive
        ? {
          title: "CM4 · ketelfallback aangestuurd",
          copy: "De warmtepompen zijn niet inzetbaar; de CV-ketel krijgt de verwarmingsopdracht.",
          tone: "fault",
        }
        : {
          title: "CM4 · fallback niet actief",
          copy: system.fallbackBlockReason
            ? `De fallbackrol is gekozen, maar de ketel wordt niet aangestuurd. Blokkade: ${getFallbackBlockReasonLabel(system.fallbackBlockReason)}.`
            : "De fallbackrol is gekozen, maar de ketel wordt niet aangestuurd; er is geen blokkadereden aangeleverd.",
          tone: "fault",
        };
    }
    if (action === "fallback_blocked") {
      return {
        title: actionPresentation.label,
        copy: `${actionPresentation.copy} Blokkade: ${getFallbackBlockReasonLabel(system.fallbackBlockReason)}.`,
        tone: "fault",
      };
    }
    return {
      title: controlMode >= 0 ? `CM${controlMode} · normale regeling` : "Normale regeling",
      copy: "Er is geen bijzondere ketelreactie voor een warmtepompincident actief.",
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
      ? "Overgang CM3 → CM4: de ketelopdracht bleef volgens de controller actief en kreeg geen uit/aan-puls."
      : "";
    return `
      <article class="oq-settings-monitoring-card oq-settings-monitoring-system">
        <header>
          <p>Systeemreactie</p>
          ${renderInstallationMonitoringBadge(
            reaction.tone !== "clear",
            reaction.tone === "fault" ? "Fallback" : "Inactief",
            "Normaal",
            reaction.tone === "fault" ? "fault" : "warning",
          )}
        </header>
        <strong class="oq-settings-monitoring-card-value">${escapeHtml(reaction.title)}</strong>
        <span>${escapeHtml(reaction.copy)}</span>
        ${continuityCopy ? renderInstallationMonitoringStatusRow({
          label: "Overgang CM3 → CM4",
          value: "Geen uit/aan-puls",
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
      return "Nog niet gemeten";
    }
    if (ageMinutes < 1) {
      return "Zojuist";
    }
    if (ageMinutes < 60) {
      return `${Math.round(ageMinutes)} min geleden`;
    }
    const hours = Math.floor(ageMinutes / 60);
    const minutes = Math.round(ageMinutes % 60);
    return `${hours}u ${minutes}m geleden`;
  }

  export function formatInstallationMonitoringEpoch(key) {
    const epoch = getEntityNumericValue(key);
    if (Number.isNaN(epoch) || epoch <= 0) {
      return "Tijdstip onbekend";
    }
    return new Intl.DateTimeFormat("nl-NL", {
      day: "2-digit",
      month: "short",
      hour: "2-digit",
      minute: "2-digit",
    }).format(new Date(epoch * 1000));
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
            <p>Pendelmelding</p>
            <strong>${monitoring.cyclingAlertActive ? "Pendelen is nu actief" : "Pendelen is niet meer actief"}</strong>
          </div>
          ${renderInstallationMonitoringBadge(monitoring.cyclingAlertActive, "Actief", "Hersteld")}
        </div>
        <span>${monitoring.cyclingAlertActive
          ? "De melding blijft staan nadat de starts weer rustig zijn geworden. Hier zie je de vastgelegde aantallen."
          : "OpenQuatt bewaart deze melding totdat je haar hieronder bevestigt."}</span>
        <dl>
          <div><dt>Eerste melding</dt><dd>${escapeHtml(formatInstallationMonitoringEpoch("compressorCyclingAlertFirstSeen"))}</dd></div>
          <div><dt>Laatste melding</dt><dd>${escapeHtml(formatInstallationMonitoringEpoch("compressorCyclingAlertLastSeen"))}</dd></div>
          <div><dt>HP1 2 uur</dt><dd>${escapeHtml(hp1Peak2h)} starts</dd></div>
          <div><dt>HP1 72 uur</dt><dd>${escapeHtml(hp1Peak72h)} starts</dd></div>
          ${hp2Peak2h ? `<div><dt>HP2 2 uur</dt><dd>${escapeHtml(hp2Peak2h)} starts</dd></div>` : ""}
          ${hp2Peak72h ? `<div><dt>HP2 72 uur</dt><dd>${escapeHtml(hp2Peak72h)} starts</dd></div>` : ""}
          ${alternating ? "<div><dt>Patroon</dt><dd>Opvallend vaak om en om</dd></div>" : ""}
        </dl>
        <div class="oq-settings-monitoring-incident-action">
          ${state.entities.acknowledgeCompressorCyclingAlert
            ? renderNamedActionButton(
              "acknowledgeCompressorCyclingAlert",
              "Melding bevestigen",
              "oq-helper-button oq-helper-button--ghost",
              monitoring.cyclingAlertActive,
            )
            : ""}
          <span>${monitoring.cyclingAlertActive
            ? "Bevestigen wordt beschikbaar zodra het pendelen is gestopt."
            : "Na bevestigen verdwijnt de herinnering uit het overzicht."}</span>
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
        label: "Flow",
        value: isInstallationMonitoringBinaryActive("lowflowFaultActive") ? "Te lage flow gemeld" : "Geen lage-flowmelding",
        active: isInstallationMonitoringBinaryActive("lowflowFaultActive"),
      }) : "",
      hasEntity("flowMismatch") ? renderInstallationMonitoringStatusRow({
        label: "Flowvergelijking duo",
        value: isInstallationMonitoringBinaryActive("flowMismatch") ? "Afwijking tussen warmtepompen" : "Geen afwijking gemeld",
        active: isInstallationMonitoringBinaryActive("flowMismatch"),
      }) : "",
    ].filter(Boolean).join("");
    const connectionRows = [
      hasEntity("cicDataStale") ? renderInstallationMonitoringStatusRow({
        label: "CIC-data",
        value: !cicPollingEnabled
          ? "Polling uitgeschakeld"
          : isInstallationMonitoringBinaryActive("cicDataStale") ? "Verouderd" : "Geen probleem gemeld",
        active: cicPollingEnabled && isInstallationMonitoringBinaryActive("cicDataStale"),
      }) : "",
      hasEntity("otLinkProblem") ? renderInstallationMonitoringStatusRow({
        label: "OpenTherm",
        value: !otEnabled
          ? "Uitgeschakeld"
          : isInstallationMonitoringBinaryActive("otLinkProblem") ? "Verbindingsprobleem" : "Geen probleem gemeld",
        active: otEnabled && isInstallationMonitoringBinaryActive("otLinkProblem"),
      }) : "",
    ].filter(Boolean).join("");
    const hpRows = structuredIncidentMonitoringAvailable ? "" : [
      hasEntity("hp1Failures") ? renderInstallationMonitoringStatusRow({
        label: "Warmtepomp 1",
        value: getInstallationMonitoringFailureText("hp1Failures"),
        active: isInstallationMonitoringFailureActive("hp1Failures"),
      }) : "",
      hasEntity("hp2Failures") ? renderInstallationMonitoringStatusRow({
        label: "Warmtepomp 2",
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
        <header><p>Hydrauliek</p></header>
        <div class="oq-settings-monitoring-rows">${hydraulicRows}</div>
      </article>
    ` : "";
    const hpPanel = hpRows ? `
      <article class="oq-settings-monitoring-card">
        <header><p>Warmtepompen</p></header>
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
        <header><p>Verbindingen</p></header>
        <div class="oq-settings-monitoring-rows">${connectionRows}</div>
      </article>
    ` : "";

    return renderSettingsSection(
      "Bewaking",
      "Installatiebewaking",
      "Lokale diagnose van warmtepompincidenten, systeemreactie, compressorstarts, hydrauliek en verbindingen. Hiervoor is geen Home Assistant nodig.",
      `
        <div class="oq-settings-monitoring-summary${monitoring.severity === "fault" ? " is-fault" : monitoring.active ? " is-warning" : " is-clear"}">
          <div>
            <p>Huidige status</p>
            <strong>${escapeHtml(monitoring.title)}</strong>
            <span>${escapeHtml(monitoring.copy)}</span>
          </div>
          ${renderInstallationMonitoringBadge(
            monitoring.active,
            monitoring.severity === "fault"
              ? "Storing"
              : monitoring.incidentMonitoringStale ? "Niet actueel" : "Aandacht nodig",
            "Alles rustig",
            monitoring.severity === "fault" ? "fault" : "warning",
          )}
        </div>
        <details class="oq-settings-monitoring-details"${state.installationMonitoringDetailsOpen ? " open" : ""}>
          <summary data-oq-action="toggle-installation-monitoring-details">
            <strong>Details en systeemreactie</strong>
          </summary>
        ${monitoring.active ? `
          <div class="oq-settings-monitoring-active-list">
            ${monitoring.problems.map((problem) => `<span>${escapeHtml(problem.label)}</span>`).join("")}
          </div>
        ` : ""}
        <div class="oq-settings-monitoring-grid">
          <div class="oq-settings-monitoring-column">
          ${systemPanel}
          ${structuredHpPanel}
          <article class="oq-settings-monitoring-card">
            <header>
              <p>Compressorstarts</p>
              ${renderInstallationMonitoringBadge(
                compressorWarningActive,
              )}
            </header>
            <span>Starts sinds de laatste controllerherstart.</span>
            ${renderInstallationMonitoringCyclingIncident(monitoring)}
            <div class="oq-starts-panel">
              <table class="oq-starts"><thead><tr><th scope="col">Warmtepomp</th><th scope="col">Laatste</th><th scope="col" class="is-alarm">2 uur</th><th scope="col">6 uur</th><th scope="col">24 uur</th><th scope="col" class="is-alarm">72 uur</th></tr></thead>
                <tbody>
                  ${renderInstallationMonitoringCompressorUnit("Warmtepomp 1", "hp1")}
                  ${renderInstallationMonitoringCompressorUnit("Warmtepomp 2", "hp2")}
                </tbody>
              </table>
              ${state.compressorLimitsOpen ? `
                <div class="oq-start-editor">
                  <strong>Alarmgrenzen</strong>
                  <div class="oq-start-fields" id="oq-start-fields">
                    ${renderSettingsMiniNumberField("compressorStarts2hWarningLimit", "2 uur", "", { compact: true })}
                    ${renderSettingsMiniNumberField("compressorStarts72hWarningLimit", "72 uur", "", { compact: true })}
                  </div>
                  <button type="button" class="oq-helper-button oq-helper-button--ghost oq-start-done" data-oq-action="toggle-compressor-limits" aria-expanded="true" aria-controls="oq-start-fields">Gereed</button>
                </div>
              ` : `
                <button type="button" class="oq-start-summary" data-oq-action="toggle-compressor-limits" aria-expanded="false">
                  <span><strong>Alarmgrenzen</strong><span>${Number.isNaN(compressorLimit2h) ? "—" : Math.round(compressorLimit2h)} / 2 uur · ${Number.isNaN(compressorLimit72h) ? "—" : Math.round(compressorLimit72h)} / 72 uur</span></span>
                  <strong>Aanpassen ›</strong>
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
        copy: "Voor Quatt V1 en Quatt V1 + V1.5 combinaties.",
        image: HP_GENERATION_IMAGE_V1,
        alt: "Quatt Hybrid V1 en V1.5",
        infoTitle: "V1",
        infoCopy: "Model: AMM4\nKenmerken: Flowmeter bij CV-ketel en vorstbeveiligingsklep buiten de buitenunit. Ook geschikt voor gemengde V1/V1.5 duo's.",
      },
      "V1.5": {
        copy: "Voor Quatt V1.5-installaties.",
        image: HP_GENERATION_IMAGE_V1,
        alt: "Quatt Hybrid V1 en V1.5",
        infoTitle: "V1.5",
        infoCopy: "Model: AMM4-V1.5\nKenmerken: Flowmeter in de buitenunit geïntegreerd. Onder CV-ketel enkel een kleine clip-on temperatuursensor.",
      },
      V2: {
        copy: "Voor Quatt V2.",
        image: HP_GENERATION_IMAGE_V2,
        alt: "Quatt Hybrid V2",
        infoTitle: "V2",
        infoCopy: "Model: AMH6 of AMH6-2\nKenmerken: Flowmeter in de buitenunit geïntegreerd. Onder CV-ketel enkel een kleine clip-on temperatuursensor.",
      },
    };

    const entity = state.entities.hpGeneration || {};
    const currentValue = String(getEntityValue("hpGeneration") || "");
    const options = getSelectEntityOptions(entity);
    const busy = state.loadingEntities || state.busyAction === "save-hpGeneration";

    return `
      ${detectionStatus}
      <div class="oq-settings-generation-field oq-settings-field--span-2">
        <div class="oq-settings-generation-grid">
          ${options.map((option) => {
            const description = descriptions[option] || {};
            return renderSettingsChoiceOption({
              key: "hpGeneration",
              option,
              currentValue,
              busy,
              copy: description.copy || "",
              meta: getOduGenerationChoiceMeta(option, currentValue, detectionModel.recommendation),
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
    const entity = state.entities.hpGeneration || {};
    const canEdit = hasEntity("hpGeneration") && getSelectEntityOptions(entity).length > 0;

    if (!currentLabel && !canEdit) {
      return "";
    }

    return renderSettingsSection(
      "Basis",
      "Quatt Hybrid-versie",
      "Kies hier welke Quatt Hybrid je hebt. Deze keuze bepaalt de basis van de regeling.",
      `
        <div class="oq-helper-surface oq-settings-field">
          <div class="oq-gen-current">
            <div>
              <p class="oq-settings-quickstart-status-label">Huidige versie</p>
              <strong class="oq-settings-quickstart-status-value">${escapeHtml(currentLabel || "Onbekend")}</strong>
            </div>
          <button
            class="oq-helper-button oq-helper-button--ghost"
            type="button"
            data-oq-action="open-generation-modal"
            ${!canEdit || state.loadingEntities || state.busyAction === "save-hpGeneration" ? "disabled" : ""}
          >
            Aanpassen
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
              ${escapeHtml(option === "OpenTherm" ? "OpenTherm (OTB)" : "Aan/uit (R1)")}
            </option>
          `).join("")}
        </select>
        <span class="oq-settings-select-caret" aria-hidden="true"></span>
      </label>
    ` : boilerConnectionAvailable ? `
      <div class="oq-settings-boiler-power-empty" role="status" aria-live="polite">
        <strong>Beschikbaarheid controleren…</strong>
        <p>De aansluitingskeuze is tijdelijk geblokkeerd.</p>
      </div>
    ` : "";
    const boilerPowerMissingHint = "Deze firmware levert nog geen bewerkbare vermogensinstelling voor de warmtebron.";
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
          <strong>Niet beschikbaar</strong>
          <p>${escapeHtml(boilerPowerMissingHint)}</p>
        </div>
      `;
    const boilerPowerFooter = sourcePresent && boilerPowerEntityAvailable
      ? `<p class="oq-settings-boiler-power-note">Je kunt deze waarde altijd handmatig aanpassen.</p>`
      : "";
    const boilerConnectionFooter = boilerConnectionAutoSelected
      ? `
        <div class="oq-settings-boiler-connection-note is-success" role="status" aria-live="polite">
          <strong>OpenTherm-ketel gedetecteerd</strong>
          <p>OpenTherm (OTB) is automatisch als ketelaansluiting geselecteerd.</p>
        </div>
      `
      : boilerConnection === "R1" && openthermBoilerSupported
        ? boilerConnectionMismatch
        ? `
          <div class="oq-settings-boiler-connection-note is-warning" role="alert">
            <strong>OpenTherm-ketel gevonden</strong>
            <p>Kies OpenTherm (OTB).</p>
          </div>
        `
        : `
          <p class="oq-settings-boiler-connection-note">OT-controle bij opstart actief.</p>
        `
        : "";
    const supportSwitchingFields = !isCurveMode() && sourcePresent && assistEnabled
      ? [
          renderSettingsNumberField(
            "boilerSupportStartThreshold",
            "Ondersteuning starten vanaf",
            "Standaard 1000 W. Power House moet eerst minimaal 2 minuten zonder aanvullende warmtebron draaien; daarna moet het warmtetekort 5 minuten onafgebroken boven deze grens blijven.",
          ),
          renderSettingsNumberField(
            "boilerSupportStopThreshold",
            "Ondersteuning stoppen onder",
            "Standaard 400 W. De aanvullende warmtebron blijft minimaal 5 minuten actief en stopt pas wanneer het warmtetekort daarna 2 minuten onder deze grens blijft.",
          ),
        ].filter(Boolean).join("")
      : "";
    const supportSwitchingMarkup = renderSettingsAdvancedDisclosure(
      "boiler-support",
      "Wanneer hybride ondersteuning start en stopt",
      "Alleen voor Power House. Het warmtetekort is het gevraagde woningvermogen min het maximaal beschikbare warmtepompvermogen, met minimaal 0 W. Tussen beide grenzen blijft de huidige toestand behouden. Deze waarden veranderen het beschikbare verwarmingsvermogen en de aansturing niet.",
      supportSwitchingFields ? `<div class="oq-settings-grid">${supportSwitchingFields}</div>` : "",
    );

    return `
        <div class="${escapeHtml(className)}">
          ${renderSettingsFieldCard(
            sourcePresenceKey,
            "Warmtebron aangesloten",
            "Zet dit aan als OpenQuatt een aanvullende warmtebron kan aansturen, zoals een cv-ketel, elektrische cv-ketel (e-cv) of doorstroomverwarmer.",
            `
              <div class="oq-settings-compact-switch-field">
                ${renderSettingsCompactSwitchControl(sourcePresenceKey, "Warmtebron aangesloten", sourcePresent, sourcePresentBusy)}
              </div>
            `,
            "oq-settings-field--compact",
          )}

          ${(sourcePresent || boilerConnectionMismatch || boilerConnectionAutoSelected) && boilerConnectionAvailable ? renderSettingsFieldCard(
            "boilerConnection",
            "Aansturing warmtebron",
            !openthermBoilerCapabilityKnown
              ? "OpenQuatt controleert welke aansturingen deze hardware ondersteunt."
              : openthermBoilerSupported
              ? "Kies de route waarmee de warmtebron fysiek is verbonden. OpenQuatt gebruikt nooit beide routes tegelijk."
              : "Deze hardware ondersteunt alleen de aan/uit-aansluiting via R1.",
            boilerConnectionControl,
            "oq-settings-field--compact",
            boilerConnectionFooter,
          ) : ""}

          ${sourcePresent ? renderSettingsFieldCard(
            "boilerRatedHeatPower",
            "Beschikbaar verwarmingsvermogen",
            "Vul hier het vermogen in dat OpenQuatt mag meerekenen.",
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
            AUX_HEAT_ASSIST_TITLE,
            "Laat de aanvullende warmtebron meeverwarmen wanneer het beschikbare warmtepompvermogen niet genoeg is voor de warmtevraag en tijdens een koude opstart van 5 tot 12 °C.",
            `
              <div class="oq-settings-compact-switch-field">
                ${renderSettingsCompactSwitchControl(
                  "boilerCvAssistEnabled",
                  AUX_HEAT_ASSIST_TITLE,
                  assistEnabled,
                  assistBusy,
                )}
              </div>
            `,
            "oq-settings-field--compact",
          ) : ""}
          ${sourcePresent && includeFaultFallback && fallbackSettingAvailable ? renderSettingsFieldCard(
            "boilerFaultFallbackEnabled",
            AUX_HEAT_BACKUP_TITLE,
            AUX_HEAT_BACKUP_COPY,
            `
              <div class="oq-settings-compact-switch-field">
                ${renderSettingsCompactSwitchControl(
                  "boilerFaultFallbackEnabled",
                  AUX_HEAT_BACKUP_TITLE,
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
      "Basis",
      "Aanvullende warmtebron",
      sourcePresent
        ? "Bijvoorbeeld een cv-ketel, elektrische cv-ketel (e-cv) of doorstroomverwarmer. Kies wanneer OpenQuatt deze mag gebruiken."
        : "Geef aan of OpenQuatt een aanvullende warmtebron kan aansturen, zoals een cv-ketel, elektrische cv-ketel (e-cv) of doorstroomverwarmer.",
      renderBoilerCvFields("oq-settings-grid oq-settings-boiler-simple-grid", true),
    );
  }

  export function formatAuxRelayStatus(status) {
    const value = String(status || "").trim();
    if (!value) {
      return "";
    }

    const labels = {
      Disabled: "Uitgeschakeld",
      "No thermal demand": "Geen warmte- of koelvraag",
      "No heating demand": "Geen warmtevraag",
      "No cooling demand": "Geen koelvraag",
      "Heating demand active": "Warmtevraag actief",
      "Cooling demand active": "Koelvraag actief",
      "External control": "Externe bediening",
      "Waiting for warm water": "Wacht op warm aanvoerwater",
      "Waiting for cold water": "Wacht op koud aanvoerwater",
      "Supply temperature unavailable": "Aanvoertemperatuur niet beschikbaar",
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
      "Huidige status",
      "Actuele toestand van het hulprelais.",
      `
        <div class="oq-settings-aux-relay-status">
          <strong>${escapeHtml(relayOn ? "Relais aan (COM–NO gesloten)" : "Relais uit (COM–NC gesloten)")}</strong>
          ${statusText ? `<p>${escapeHtml(statusText)}</p>` : ""}
        </div>
      `,
    ) : "";
    const fields = [
      renderSettingsSelectField(
        "auxRelayFunction",
        "Functie",
        "Kies wat relais R2 volgt. R2 volgt de effectieve warmte- of koelvraag van OpenQuatt, of kies Externe bediening om R2 via bijvoorbeeld Home Assistant of de REST-API te schakelen.",
      ),
      statusPanel,
      demandFunctionSelected ? renderSettingsSwitchField(
        "auxWaitForSupplyTemp",
        "Wacht op aanvoertemperatuur",
        "Aan: R2 schakelt bij vraag pas in zodra het aanvoerwater op temperatuur is (warm genoeg bij verwarmen, koud genoeg bij koelen).",
        "R2 wacht op de startdrempels hieronder.",
        "R2 schakelt direct bij vraag, ongeacht de watertemperatuur.",
        "oq-settings-field--span-2",
      ) : "",
      tempGateEnabled ? renderSettingsNumberField("auxHeatingStartTemp", "Startdrempel verwarmen", "Bij warmtevraag schakelt R2 pas in zodra het aanvoerwater minstens deze temperatuur heeft.") : "",
      tempGateEnabled ? renderSettingsNumberField("auxCoolingStartTemp", "Startdrempel koelen", "Bij koelvraag schakelt R2 pas in zodra het aanvoerwater maximaal deze temperatuur heeft.") : "",
      tempGateEnabled ? renderSettingsNumberField("auxTempHysteresis", "Hysterese aanvoertemperatuur", "Marge waarmee de startdrempel weer verlaten moet worden voordat R2 uitschakelt. Voorkomt snel aan/uit schakelen rond de grens.") : "",
    ].filter(Boolean);

    return renderSettingsSection(
      "Basis",
      "Hulprelais (R2)",
      "Gebruik het tweede potentiaalvrije relais van de controller als optionele hulpuitgang, bijvoorbeeld voor een fancoil, pomp of klep. Standaard staat deze functie uit.",
      `
        <div class="oq-settings-grid">
          ${fields.join("")}
        </div>
      `,
    );
  }

  export function renderSettingsQuickStartSection() {
    const statusLabel = state.complete === true ? "Afgerond" : state.complete === false ? "Open" : "Laden...";
    const statusCopy = state.complete === true
      ? "Quick Start is afgerond. Je kunt de status hier altijd weer openen met een reset."
      : state.complete === false
        ? "Quick Start staat nog open. Gebruik de resetknop om opnieuw te beginnen."
        : "De status van Quick Start wordt nog geladen.";

    return renderSettingsSection(
      "Setup",
      "Quick Start",
      "Bekijk of de Quick Start nog open staat of al is afgerond.",
      `
        <div class="oq-settings-quickstart-status">
          <div class="oq-settings-quickstart-status-row">
            <div>
              <p class="oq-settings-quickstart-status-label">Huidige status</p>
              <strong class="oq-settings-quickstart-status-value">${escapeHtml(statusLabel)}</strong>
            </div>
            <button
              class="oq-helper-button oq-helper-button--ghost"
              type="button"
              data-oq-action="reset"
              ${state.busyAction === "reset" ? "disabled" : ""}
            >
              Reset status
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
      Openen
    </button>`;
  }

  export function renderSettingsDiagnosticsSection() {
    const updateStatus = getUpdateStatus();
    const dateTime = formatDiagnosticsDateTime();
    const busyRestart = state.busyAction === "restartAction";
    const activeConnection = hasEntity("connectionText")
      ? getEntityStateText("connectionText", "Niet verbonden").replace("Not connected", "Niet verbonden")
      : getConnectivityStatus();
    const ipAddress = getDeviceIpAddress();

    return renderSettingsSection(
      "Diagnostiek",
      "Systeemstatus",
      "Snelle statusinformatie voor support, controle en onderhoud.",
      `
        <div class="oq-settings-system-summary">
          ${renderSettingsSystemRow({ dataValue: "uptime", label: "Uptime", value: formatUptimeFromMeta() })}
          ${renderSettingsSystemRow({
            dataValue: "connectivity",
            label: "Connectiviteit",
            value: activeConnection,
            note: ipAddress === "—" ? "" : `IP-adres ${ipAddress}`,
            action: renderSettingsSystemOpenAction("open-connectivity-modal"),
          })}
          ${renderSettingsSystemRow({
            dataValue: "updates",
            label: "Updates",
            value: updateStatus,
            action: renderSettingsSystemOpenAction("open-update-modal"),
          })}
          ${renderSettingsSystemRow({
            dataValue: "webserverLog",
            label: "Logboek",
            value: getWebServerLogStatusLabel(),
            action: renderSettingsSystemOpenAction("open-webserver-log-modal"),
          })}
          ${renderSettingsSystemRow({
            dataValue: "debugRecording",
            label: "Debugopname",
            value: getDebugRecordingStatusLabel(),
            note: getDebugRecordingStatusCopy(),
            action: renderSettingsSystemOpenAction("open-debug-recording-modal"),
          })}
          ${renderSettingsSystemRow({ dataValue: "datetime", label: "Datum/tijd", value: dateTime })}
          ${renderSettingsSystemRow({ dataValue: "espTemp", label: "ESP-temp", value: getEspTemperatureLabel() })}
          ${renderSettingsSystemRow({
            dataValue: "restart",
            label: "Herstart OpenQuatt",
            value: "Opnieuw opstarten",
            note: "Dit onderbreekt de webinterface kort.",
            action: `<button
              class="oq-helper-button oq-helper-button--warning"
              type="button"
              data-oq-action="open-restart-confirm"
              ${busyRestart ? "disabled" : ""}
            >
              ${busyRestart ? "Herstarten..." : "Herstarten"}
            </button>`,
          })}
          ${hasEntity("statusLedsEnabled") ? `
            ${renderSettingsSystemRow({
              dataValue: "statusLeds",
              label: "Status-LEDs",
              value: isEntityActive("statusLedsEnabled") ? "Aan" : "Uit",
              note: "Schakelt de gele netwerk-LED en rode storings-LED op de Q-edition controller.",
              action: renderSettingsCompactSwitchControl(
                "statusLedsEnabled",
                "Status-LEDs",
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
      renderSettingsHeatPumpLimiterCard("Warmtepomp 1", "hp1"),
      renderSettingsHeatPumpLimiterCard("Warmtepomp 2", "hp2"),
    ].filter(Boolean).join("");

    return renderSettingsSection(
      "Installatie",
      "Compressorinstellingen",
      "Stel hier de minimale draaitijd in en bepaal per warmtepomp welke compressorfrequenties je wilt overslaan.",
      `
        <div class="oq-settings-subpanel">
          <div class="oq-settings-subpanel-head">
            <p class="oq-helper-label">Draaitijd</p>
            <h4>Minimale draaitijd</h4>
            <p>Voorkomt dat de warmtepomp te kort achter elkaar start en stopt.</p>
          </div>
          <div class="oq-settings-grid">
            ${renderSettingsNumberField("minRuntime", "Minimale draaitijd", "Hoe lang een compressor minimaal moet blijven lopen voordat hij weer mag stoppen.")}
          </div>
        </div>
        <div class="oq-settings-subpanel oq-settings-subpanel--nested">
          <div class="oq-settings-subpanel-head">
            <p class="oq-helper-label">Uitsluitingen</p>
            <h4>Frequentiebereiken uitsluiten</h4>
            <p>Kies per warmtepomp één frequentiebereik dat OpenQuatt moet overslaan.</p>
          </div>
          <div class="oq-settings-hp-columns${hasEntity("hp2ExcludeMinHz") ? "" : " oq-settings-hp-columns--single"}">
            ${hpGroups}
          </div>
        </div>
      `,
    );
  }
