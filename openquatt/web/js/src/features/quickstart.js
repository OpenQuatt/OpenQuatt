import { getEntityNumericValue, getEntityStateText, hasEntity, isEntityActive } from "../core/app-shared.js";
import { QUICK_STEPS } from "../core/config.js";
import { isCurveMode } from "../core/domain-helpers.js";
import { formatValue, getEntityValue, toTimeInputValue } from "../core/entity-store.js";
import { createScrollKeeper } from "../core/scroll-keeper.js";
import { renderModalShell } from "../core/modal-shell.js";
import { state } from "../core/state.js";
import { getDeviceMeta, getFirmwareBuildConnection, getInstallationTopology } from "./device-context.js";
import { getFirmwareBuildSwitchModel, getFirmwareChannelLabel, getFirmwareCurrentVersion, getFirmwareLatestVersion, getFirmwareProgressModel, getFirmwareUpdateEntity, isFirmwareEntityAlignedWithChannel, isFirmwareUpdateEntityForBuild, isQuickStartSetupFirmwareCurrent, reconcileStoredQuickStartSetupInstall } from "./firmware-update.js";
import { getOduGenerationDetectionModel } from "./odu-generation-ui.js";
import { formatSettingsOptionLabel, renderSettingsFieldCard, renderSettingsInfoToggle } from "../settings/controls.js";
import { renderCurveGraph, renderFlowSettingsFields, renderHeatingCurveProfileField, renderHeatingStrategyExplainCards, renderPowerHouseAdvancedField, renderPowerHouseBaseFields, renderSettingsCurveInputs, renderStrategySelectionFields } from "../settings/heating.js";
import { getHeatingEnableAdvice, getHeatingEnableCurrent, getHeatingEnableRecommendation } from "../core/heating-strategy-matrix.js";
import { renderBoilerCvFields, renderHpGenerationField } from "../settings/installation.js";
import { renderSilentSettingsGrid } from "../settings/silent.js";
import { renderWaterSettingsFields } from "../settings/water.js";
import { escapeHtml } from "../core/html.js";
import { renderUsageTelemetryConsent, renderUsageTelemetryDisclosure } from "./usage-telemetry.js";

  export function getQuickStartSetupModel() {
    const currentTopology = getInstallationTopology();
    const currentConnection = getFirmwareBuildConnection();
    const currentKey = `${currentTopology}:${currentConnection}`;
    const selectedKey = state.quickStartSetupDraft || currentKey;
    const [targetTopology, targetConnection] = selectedKey.split(":");
    const switchModel = getFirmwareBuildSwitchModel(targetTopology, targetConnection);
    return {
      ...switchModel,
      currentKey,
      selectedKey,
      changes: selectedKey !== currentKey,
      targetIsDuo: targetTopology === "duo",
      targetIsEthernet: targetConnection === "eth",
    };
  }

  export function renderSetupWorkspace() {
    const model = getQuickStartSetupModel();
    const progress = getFirmwareProgressModel();
    const busy = Boolean(progress || state.updateInstallBusy);
    const firmwareEntity = getFirmwareUpdateEntity() || {};
    const mainManifestReady = getFirmwareChannelLabel().toLowerCase() === "main"
      && isFirmwareEntityAlignedWithChannel(firmwareEntity, "main")
      && isFirmwareUpdateEntityForBuild(model.targetBuildLabel, firmwareEntity);
    const currentVersion = getFirmwareCurrentVersion(firmwareEntity) || "Onbekend";
    const mainVersion = mainManifestReady ? getFirmwareLatestVersion(firmwareEntity) || "Onbekend" : "Wordt na bevestigen gecontroleerd";
    const firmwareCurrent = mainManifestReady && isQuickStartSetupFirmwareCurrent(model);
    const options = [
      ["single:wifi", "Single · Wi-Fi", "Eén warmtepomp via het draadloze netwerk."],
      ["single:eth", "Single · Ethernet", "Eén warmtepomp via een vaste netwerkkabel."],
      ["duo:wifi", "Duo · Wi-Fi", "Twee warmtepompen via het draadloze netwerk."],
      ["duo:eth", "Duo · Ethernet", "Twee warmtepompen via een vaste netwerkkabel."],
    ];
    const requirements = [
      model.targetIsDuo ? "De tweede warmtepomp is aangesloten en hoort bij deze controller." : "Deze controller wordt voor één warmtepomp gebruikt.",
      model.targetIsEthernet ? "De netwerkkabel is aangesloten." : "De Wi-Fi-gegevens zijn beschikbaar op de controller.",
      firmwareCurrent
        ? "Configuratie en main-release zijn actueel; er is geen OTA nodig."
        : "Zo nodig wordt de stabiele main-release geïnstalleerd en vervangt deze een dev- of testbuild.",
    ];

    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("setup"))}</p>
        <h2 class="oq-helper-section-title">Configuratie en software-update</h2>
        <p class="oq-helper-section-copy">Kies de configuratie van je Q-edition. OpenQuatt controleert daarna de nieuwste stabiele main-release en installeert deze alleen als de versie of configuratie afwijkt.</p>
        <div class="oq-helper-fields">
          ${options.map(([key, title, copy]) => {
            const selected = model.selectedKey === key;
            const current = model.currentKey === key;
            return `
              <button
                class="oq-helper-field oq-helper-field--step${selected ? " is-current" : ""}"
                type="button"
                data-oq-action="select-quickstart-setup"
                data-setup-target="${escapeHtml(key)}"
                aria-pressed="${selected ? "true" : "false"}"
                ${busy ? "disabled" : ""}
              >
                <div class="oq-helper-field-step-head">
                  <h3>${escapeHtml(title)}</h3>
                  ${current ? '<span class="oq-helper-field-step-state">Actief</span>' : ""}
                </div>
                <p>${escapeHtml(copy)}</p>
              </button>
            `;
          }).join("")}
        </div>
        <div class="oq-firmware-advanced-detail">
            ${progress ? `
              <div class="oq-helper-modal-progress" aria-live="polite">
                <div class="oq-helper-modal-progress-head">
                  <strong>${escapeHtml(progress.phaseLabel)}</strong>
                  <span>${escapeHtml(`${progress.percent}%`)}</span>
                </div>
                <div class="oq-helper-modal-progress-track" aria-hidden="true">
                  <span class="oq-helper-modal-progress-fill" style="width:${Math.max(0, Math.min(100, progress.percent))}%"></span>
                </div>
                <p class="oq-helper-modal-note">${escapeHtml(progress.copy)}</p>
              </div>
            ` : ""}
            <div class="oq-helper-modal-grid">
              <div class="oq-helper-modal-row"><span class="oq-helper-modal-label">Huidige build</span><strong class="oq-helper-modal-value">${escapeHtml(model.currentBuildLabel)}</strong></div>
              <div class="oq-helper-modal-row"><span class="oq-helper-modal-label">Gekozen build</span><strong class="oq-helper-modal-value">${escapeHtml(model.targetBuildLabel)}</strong></div>
              <div class="oq-helper-modal-row"><span class="oq-helper-modal-label">Huidige versie</span><strong class="oq-helper-modal-value">${escapeHtml(currentVersion)}</strong></div>
              <div class="oq-helper-modal-row"><span class="oq-helper-modal-label">Nieuwste main-versie</span><strong class="oq-helper-modal-value">${escapeHtml(mainVersion)}</strong></div>
            </div>
            <p class="oq-helper-modal-note">${firmwareCurrent
              ? "Softwareversie en configuratie kloppen. Na bevestigen gaat Quick Start zonder OTA verder."
              : "OpenQuatt controleert de stabiele softwareversie en gekozen configuratie. Alleen bij een afwijking volgt OTA en herstart. Instellingen blijven behouden."}</p>
            <label class="oq-helper-modal-check">
              <input type="checkbox" data-oq-quickstart-setup-confirm="true" ${state.quickStartSetupConfirmed ? "checked" : ""} ${busy ? "disabled" : ""}>
              <span>${escapeHtml(requirements.join(" "))}</span>
            </label>
            <div class="oq-firmware-advanced-footer">
              <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="install-quickstart-setup" ${busy || !state.quickStartSetupConfirmed || !model.canInstall ? "disabled" : ""}>
                ${busy ? "Configuratie en software controleren..." : firmwareCurrent ? "Configuratie bevestigen" : "Configuratie bevestigen en software controleren"}
              </button>
            </div>
            ${!model.canInstall && !busy ? `<p class="oq-helper-modal-note oq-helper-modal-note--muted">${escapeHtml(
              !model.targetEntityAvailable || !model.installActionAvailable || !model.mainChannelAvailable
                ? "De firmwarebediening wordt nog geladen. Wacht een moment en probeer opnieuw."
                : "Deze firmware mist nog het vereiste OTA-target voor de gekozen configuratie.",
            )}</p>` : ""}
        </div>
        ${state.controlNotice ? `<p class="oq-helper-notice">${escapeHtml(state.controlNotice)}</p>` : ""}
        ${state.controlError ? `<p class="oq-helper-error">${escapeHtml(state.controlError)}</p>` : ""}
      </section>
    `;
  }

  export function renderGenerationWorkspace(mode = "wizard") {
    const pickerMode = mode === "picker";
    if (pickerMode) {
      return `
        <section class="oq-helper-panel oq-helper-panel--flush">
          ${renderHpGenerationField()}
          <div class="oq-helper-actions oq-settings-generation-actions">
            <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-quickstart-modal">Gereed</button>
          </div>
        </section>
      `;
    }

    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("generation"))}</p>
        <h2 class="oq-helper-section-title">Kies je Quatt Hybrid</h2>
        <p class="oq-helper-section-copy">Geef hier aan welke Quatt Hybrid je hebt. Dan zet OpenQuatt de juiste regeling klaar.</p>
        ${renderHpGenerationField()}
        ${renderQuickStartStepNav()}
      </section>
    `;
  }

  export function normalizeQuickStartCicFeedUrl(rawValue) {
    const value = String(rawValue || "").trim();
    if (!value) {
      return "";
    }

    try {
      const parsed = new URL(/^[a-z][a-z0-9+.-]*:\/\//i.test(value) ? value : `http://${value}`);
      if (parsed.protocol !== "http:" && parsed.protocol !== "https:") {
        return "";
      }
      if (!parsed.port) {
        parsed.port = "8080";
      }
      if (!parsed.pathname || parsed.pathname === "/") {
        parsed.pathname = "/beta/feed/data.json";
      }
      return parsed.toString();
    } catch (_error) {
      return "";
    }
  }

  export function getQuickStartCicFeedUrlModel() {
    const configuredUrl = String(getEntityValue("cicFeedUrl") || "").trim();
    const draftUrl = state.quickStartCicFeedUrlDraft === null
      ? configuredUrl
      : String(state.quickStartCicFeedUrlDraft || "");
    return {
      configuredUrl,
      draftUrl,
      normalizedDraftUrl: normalizeQuickStartCicFeedUrl(draftUrl),
    };
  }

  export function renderQuickStartCicFeedUrlField(model, busy) {
    return `
      <article class="oq-helper-surface oq-settings-field oq-settings-field--span-2" data-oq-settings-field="quickStartCicFeedUrl">
        <div class="oq-settings-field-head">
          <h3>CiC JSON-feed</h3>
          ${renderSettingsInfoToggle("quickStartCicFeedUrl", "CiC JSON-feed", "Vul een IP-adres, hostname of volledige URL in. Bij alleen een adres gebruikt OpenQuatt automatisch poort 8080 en /beta/feed/data.json.")}
        </div>
        <div class="oq-settings-field-control">
          <label class="oq-settings-control oq-settings-control--text">
            <input
              class="oq-helper-input oq-settings-integration-url-input"
              type="text"
              data-oq-quickstart-cic-url
              value="${escapeHtml(model.draftUrl)}"
              placeholder="192.168.2.117"
              autocomplete="off"
              spellcheck="false"
              ${busy ? "disabled" : ""}
            >
          </label>
          ${model.draftUrl && !model.normalizedDraftUrl ? `<p class="oq-settings-source-warning">Vul een geldig IP-adres, hostname of een geldige HTTP(S)-URL in.</p>` : ""}
          ${model.normalizedDraftUrl ? `<p class="oq-settings-action-note">Wordt ingesteld als ${escapeHtml(model.normalizedDraftUrl)}</p>` : ""}
        </div>
      </article>
    `;
  }

  export function normalizeQuickStartHardwareProfile(value) {
    const normalized = String(value || "").trim().toLowerCase();
    if (normalized === "heatpump_controller_q" || normalized.includes("q-edition") || normalized.includes("controller q")) {
      return "heatpump_controller_q";
    }
    if (normalized === "heatpump_listener" || normalized.includes("listener")) {
      return "heatpump_listener";
    }
    if (normalized === "waveshare" || normalized.includes("waveshare")) {
      return "waveshare";
    }
    return "";
  }

  export function getQuickStartHardwareProfileModel() {
    let profile = normalizeQuickStartHardwareProfile(getEntityValue("hardwareProfileText"));
    let inferred = false;
    if (!profile) {
      profile = normalizeQuickStartHardwareProfile(getDeviceMeta().hardwareProfile);
    }
    if (!profile && hasEntity("qFlowSource")) {
      profile = "heatpump_controller_q";
      inferred = true;
    } else if (!profile && hasEntity("flowSource") && hasEntity("cicPollingEnabled")) {
      profile = "remote";
      inferred = true;
    }

    return {
      profile,
      inferred,
      isQEdition: profile === "heatpump_controller_q",
      isRemoteProfile: profile === "heatpump_listener" || profile === "waveshare" || profile === "remote",
      hardwareKnown: Boolean(profile),
      hardwareLabel: profile === "heatpump_controller_q"
        ? "Heatpump Controller Q-edition"
        : profile === "heatpump_listener"
          ? "Heatpump Listener"
          : profile === "waveshare"
            ? "Waveshare"
            : profile === "remote"
              ? "Heatpump Listener / Waveshare"
              : "Onbekend hardwareprofiel",
    };
  }

  export function getQuickStartFlowSourceModel() {
    const generation = String(getEntityValue("hpGeneration") || "").trim();
    const hardware = getQuickStartHardwareProfileModel();
    const isV1 = generation === "V1";
    const { isQEdition, isRemoteProfile, hardwareKnown } = hardware;
    const requiresCic = isV1 && isRemoteProfile;
    const qFlowTarget = isQEdition ? (isV1 ? "Local" : "Outdoor unit") : "";
    const flowSourceTarget = requiresCic ? "CIC" : "Outdoor unit";
    const currentFlowSource = String(getEntityValue("flowSource") || "").trim();
    const currentQFlowSource = String(getEntityValue("qFlowSource") || "").trim();
    const cicEnabled = isEntityActive("cicPollingEnabled");
    const cicFeedOk = isEntityActive("cicJsonFeedOk");
    const cicStale = isEntityActive("cicDataStale");
    const cicUrl = getQuickStartCicFeedUrlModel();
    const sourceApplied = currentFlowSource === flowSourceTarget
      && (!qFlowTarget || currentQFlowSource === qFlowTarget);
    const configurationApplied = requiresCic
      ? sourceApplied && cicEnabled && Boolean(cicUrl.configuredUrl)
      : sourceApplied;
    const sensorKey = requiresCic
      ? "cicFlowrate"
      : isQEdition && isV1
        ? "controllerFlow"
        : getInstallationTopology() === "duo"
          ? "flowLocal"
          : "hp1Flow";
    const flowValue = getEntityNumericValue(sensorKey);
    const flowAvailable = Number.isFinite(flowValue);
    const flowTestActive = isEntityActive("quickFlowTest");

    let status = hardwareKnown ? requiresCic ? "Nog configureren" : "Nog activeren" : "Hardwareprofiel niet herkend";
    if (requiresCic && configurationApplied) {
      status = cicFeedOk && flowAvailable
        ? flowValue > 0 ? "Geldig" : "Bron actief, geen circulatie"
        : cicStale
          ? "Geen actuele CiC-data"
          : cicFeedOk
            ? "Verbonden, wacht op flow"
            : "Verbinding controleren";
    } else if (!requiresCic && configurationApplied) {
      status = flowAvailable
        ? flowValue > 0 ? "Geldig" : "Bron actief, geen circulatie"
        : "Wacht op actuele flow";
    }

    const sourceLabel = requiresCic
      ? "CiC JSON-feed"
      : isQEdition && isV1
        ? "Lokale flowmeter op de controller"
        : "Flowmeter in de buitenunit via Modbus";
    const explanation = requiresCic
      ? "Een Quatt V1 heeft op dit hardwareprofiel geen lokaal aangesloten flowmeter. Configureer daarom de lokale CiC JSON-feed."
      : isQEdition && isV1
        ? "Bij Quatt V1 is de centrale flowmeter lokaal aangesloten op de Q-edition controller."
        : `Bij Quatt ${generation || "V1.5/V2"} zit de flowmeter in de buitenunit en leest OpenQuatt deze via Modbus.`;

    return {
      generation,
      hardwareLabel: hardware.hardwareLabel,
      requiresCic,
      qFlowTarget,
      flowSourceTarget,
      configurationApplied,
      sourceLabel,
      explanation,
      status,
      flowValue,
      flowAvailable,
      flowTestActive,
      canRunFlowTest: configurationApplied,
      ...cicUrl,
      canApply: hardwareKnown
        && hasEntity("flowSource")
        && (!qFlowTarget || hasEntity("qFlowSource"))
        && (!requiresCic || (hasEntity("cicPollingEnabled") && hasEntity("cicFeedUrl") && Boolean(cicUrl.normalizedDraftUrl))),
    };
  }

  export function getQuickStartThermostatSourceModel() {
    const hardware = getQuickStartHardwareProfileModel();
    const { isQEdition, isRemoteProfile } = hardware;
    const currentRoomTempSource = String(getEntityValue("roomTempSource") || "").trim();
    const currentRoomSetpointSource = String(getEntityValue("roomSetpointSource") || "").trim();
    const pairedCurrentSource = currentRoomTempSource === currentRoomSetpointSource
      && ["CIC", "OT thermostat", "HA input"].includes(currentRoomTempSource)
      ? currentRoomTempSource
      : "";
    const selectedSource = isQEdition
      ? "OT thermostat"
      : state.quickStartThermostatSourceDraft || (pairedCurrentSource === "CIC" || pairedCurrentSource === "HA input" ? pairedCurrentSource : "CIC");
    const cicUrl = getQuickStartCicFeedUrlModel();
    const sourceApplied = currentRoomTempSource === selectedSource && currentRoomSetpointSource === selectedSource;
    const configurationApplied = sourceApplied
      && (selectedSource !== "OT thermostat" || isEntityActive("otEnabled"))
      && (selectedSource !== "CIC" || (isEntityActive("cicPollingEnabled") && Boolean(cicUrl.configuredUrl)));
    const sourceValueKeys = selectedSource === "OT thermostat"
      ? ["otRoomTemp", "otRoomSetpoint"]
      : selectedSource === "CIC"
        ? ["cicRoomTemp", "cicRoomSetpoint"]
        : ["roomTempHa", "roomSetpointHa"];
    const roomTempValue = getEntityNumericValue(sourceValueKeys[0]);
    const roomSetpointValue = getEntityNumericValue(sourceValueKeys[1]);
    const valuesAvailable = Number.isFinite(roomTempValue) && Number.isFinite(roomSetpointValue);
    const sourceHealthy = selectedSource === "OT thermostat"
      ? isEntityActive("otEnabled") && !isEntityActive("otLinkProblem") && valuesAvailable
      : selectedSource === "CIC"
        ? isEntityActive("cicJsonFeedOk") && !isEntityActive("cicDataStale") && valuesAvailable
        : isEntityActive("roomTempHaValid") && isEntityActive("roomSetpointHaValid") && valuesAvailable;

    let status = isQEdition || isRemoteProfile ? "Nog activeren" : "Hardwareprofiel niet herkend";
    if (configurationApplied) {
      status = sourceHealthy ? "Geldig" : selectedSource === "OT thermostat"
        ? "OpenTherm-verbinding controleren"
        : selectedSource === "CIC"
          ? "CiC-feed controleren"
          : "HA-proxy's controleren";
    }

    const sourceLabel = selectedSource === "OT thermostat"
      ? "OpenTherm-thermostaat"
      : selectedSource === "CIC"
        ? "CiC JSON-feed"
        : "Home Assistant-proxy's";
    const explanation = isQEdition
      ? "De Q-edition leest kamertemperatuur en kamer-setpoint rechtstreeks uit via OpenTherm."
      : selectedSource === "CIC"
        ? "OpenQuatt leest beide thermostaatwaarden samen uit de lokale CiC JSON-feed."
        : "OpenQuatt gebruikt de vaste HA-proxy's voor kamertemperatuur en kamer-setpoint.";

    return {
      hardwareLabel: hardware.hardwareLabel,
      isQEdition,
      isRemoteProfile,
      selectedSource,
      sourceLabel,
      explanation,
      configurationApplied,
      status,
      roomTempValue,
      roomSetpointValue,
      valuesAvailable,
      ...cicUrl,
      canApply: (isQEdition || isRemoteProfile)
        && hasEntity("roomTempSource")
        && hasEntity("roomSetpointSource")
        && (selectedSource !== "OT thermostat" || hasEntity("otEnabled"))
        && (selectedSource !== "CIC" || (hasEntity("cicPollingEnabled") && hasEntity("cicFeedUrl") && Boolean(cicUrl.normalizedDraftUrl))),
    };
  }

  export function renderFlowSourceWorkspace() {
    const model = getQuickStartFlowSourceModel();
    const busy = state.busyAction === "quickstart-flow-source" || state.busyAction === "quickstart-flow-refresh";
    const flowTestBusy = state.busyAction === "quickstart-flow-test-start" || state.busyAction === "quickstart-flow-test-abort";
    const controlsBusy = busy || flowTestBusy || model.flowTestActive;
    const statusClass = model.status === "Geldig" || model.status === "Bron actief, geen circulatie" ? " is-active" : "";
    const flowLabel = model.flowAvailable ? `${Math.round(model.flowValue)} L/h` : "Nog geen actuele waarde";
    const cicField = model.requiresCic ? renderQuickStartCicFeedUrlField(model, controlsBusy) : "";

    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("flow-source"))}</p>
        <h2 class="oq-helper-section-title">Flowmeting configureren</h2>
        <p class="oq-helper-section-copy">Je Quatt-versie en het hardwareprofiel bepalen automatisch welke flowbron nodig is. Controleer de uitkomst en activeer de configuratie.</p>
        <div class="oq-settings-grid oq-settings-grid--quickstart">
          ${renderSettingsFieldCard(
            "quickStartFlowSource",
            "Vastgestelde flowbron",
            model.explanation,
            `
              <div class="oq-settings-quickstart-status">
                <div class="oq-settings-quickstart-status-row">
                  <div>
                    <p class="oq-settings-quickstart-status-label">${escapeHtml(model.hardwareLabel)} · Quatt ${escapeHtml(model.generation || "onbekend")}</p>
                    <strong class="oq-settings-quickstart-status-value">${escapeHtml(model.sourceLabel)}</strong>
                    <p class="oq-settings-quickstart-status-copy">${escapeHtml(model.explanation)}</p>
                  </div>
                </div>
                <div class="oq-settings-source-rows">
                  <div class="oq-settings-source-row${statusClass}"><span>Status</span><strong>${escapeHtml(model.status)}</strong></div>
                  <div class="oq-settings-source-row"><span>Actuele flow</span><strong>${escapeHtml(flowLabel)}</strong></div>
                </div>
              </div>
            `,
            "oq-settings-field--span-2",
          )}
          ${cicField}
        </div>
        <div class="oq-helper-actions">
          <button
            class="oq-helper-button oq-helper-button--primary"
            type="button"
            data-oq-action="apply-quickstart-flow-source"
            ${controlsBusy || !model.canApply ? "disabled" : ""}
          >
            ${state.busyAction === "quickstart-flow-source" ? "Flowconfiguratie opslaan..." : model.configurationApplied ? "Flowconfiguratie opnieuw opslaan" : model.requiresCic ? "CiC-flowconfiguratie opslaan" : "Flowconfiguratie activeren"}
          </button>
          <button
            class="oq-helper-button oq-helper-button--ghost"
            type="button"
            data-oq-action="refresh-quickstart-flow-signal"
            ${controlsBusy || !model.configurationApplied ? "disabled" : ""}
          >
            ${state.busyAction === "quickstart-flow-refresh" ? "Signaal controleren..." : "Signaal opnieuw controleren"}
          </button>
          ${model.canRunFlowTest ? `
            <button
              class="oq-helper-button ${model.flowTestActive ? "" : "oq-helper-button--ghost"}"
              type="button"
              data-oq-action="${model.flowTestActive ? "abort-quickstart-flow-test" : "start-quickstart-flow-test"}"
              ${busy || flowTestBusy ? "disabled" : ""}
            >
              ${flowTestBusy
                ? model.flowTestActive ? "Waterpomptest stoppen..." : "Waterpomptest starten..."
                : model.flowTestActive
                  ? "Waterpomptest stoppen"
                  : "Waterpomptest starten (30 sec)"}
            </button>
          ` : ""}
        </div>
        <p class="oq-settings-action-note">${model.flowTestActive
          ? "Alleen de waterpomp draait op 400 iPWM. Het kan enkele seconden duren voordat de circulatie op gang komt en de flowmeter een waarde toont. De firmware stopt de test automatisch na maximaal 30 seconden."
          : "0 L/h kan normaal zijn als de circulatiepomp stilstaat. De waterpomptest gebruikt 400 iPWM, start geen compressor en stopt automatisch na 30 seconden."}</p>
        ${renderQuickStartStepNav({
          nextDisabled: !model.configurationApplied || model.flowTestActive || flowTestBusy,
          nextDisabledLabel: flowTestBusy
            ? "Even wachten"
            : model.flowTestActive
              ? "Test loopt"
              : model.requiresCic ? "Sla eerst op" : "Activeer eerst",
        })}
      </section>
    `;
  }

  export function renderThermostatSourceWorkspace() {
    const model = getQuickStartThermostatSourceModel();
    const busy = state.busyAction === "quickstart-thermostat-source";
    const statusClass = model.status === "Geldig" ? " is-active" : "";
    const sourceSelector = model.isRemoteProfile ? `
      <article class="oq-helper-surface oq-settings-field oq-settings-field--span-2" data-oq-settings-field="quickStartThermostatSource">
        <div class="oq-settings-field-head">
          <h3>Gegevensbron</h3>
          ${renderSettingsInfoToggle("quickStartThermostatSource", "Gegevensbron", "Kamertemperatuur en kamer-setpoint worden bewust als gekoppeld paar ingesteld.")}
        </div>
        <div class="oq-settings-field-control">
          <label class="oq-settings-control oq-settings-control--select">
            <select data-oq-quickstart-thermostat-source ${busy ? "disabled" : ""}>
              <option value="CIC" ${model.selectedSource === "CIC" ? "selected" : ""}>CiC JSON-feed</option>
              <option value="HA input" ${model.selectedSource === "HA input" ? "selected" : ""}>Home Assistant</option>
            </select>
          </label>
          <p class="oq-settings-action-note">Deze keuze geldt altijd voor zowel kamertemperatuur als kamer-setpoint.</p>
        </div>
      </article>
    ` : "";
    const cicField = model.selectedSource === "CIC" ? renderQuickStartCicFeedUrlField(model, busy) : "";
    const haNote = model.selectedSource === "HA input" ? `
      <article class="oq-helper-surface oq-settings-field oq-settings-field--span-2">
        <div class="oq-settings-field-head"><h3>Home Assistant-contract</h3></div>
        <div class="oq-settings-field-control">
          <p class="oq-settings-action-note">Verwacht <strong>sensor.openquatt_ext_room_temperature</strong> en <strong>sensor.openquatt_ext_room_setpoint</strong>, plus de bijbehorende <strong>_valid</strong> binary sensors.</p>
          <p class="oq-settings-action-note"><a href="https://github.com/OpenQuatt/OpenQuatt/tree/main/docs/dashboard#optioneel-dynamische-bronselectie-via-home-assistant" target="_blank" rel="noreferrer">Bekijk de Home Assistant-configuratie en het dynamische bronnenpakket</a>.</p>
        </div>
      </article>
    ` : "";

    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("thermostat-source"))}</p>
        <h2 class="oq-helper-section-title">Thermostaatgegevens configureren</h2>
        <p class="oq-helper-section-copy">Kamertemperatuur en kamer-setpoint horen bij dezelfde thermostaatbron en worden daarom samen ingesteld.</p>
        <div class="oq-settings-grid oq-settings-grid--quickstart">
          ${renderSettingsFieldCard(
            "quickStartThermostatSourceStatus",
            model.isQEdition ? "Vastgestelde thermostaatbron" : "Gekozen thermostaatbron",
            model.explanation,
            `
              <div class="oq-settings-quickstart-status">
                <div class="oq-settings-quickstart-status-row">
                  <div>
                    <p class="oq-settings-quickstart-status-label">${escapeHtml(model.hardwareLabel)}</p>
                    <strong class="oq-settings-quickstart-status-value">${escapeHtml(model.sourceLabel)}</strong>
                    <p class="oq-settings-quickstart-status-copy">${escapeHtml(model.explanation)}</p>
                  </div>
                </div>
                <div class="oq-settings-source-rows">
                  <div class="oq-settings-source-row${statusClass}"><span>Status</span><strong>${escapeHtml(model.status)}</strong></div>
                  <div class="oq-settings-source-row"><span>Kamertemperatuur</span><strong>${Number.isFinite(model.roomTempValue) ? `${model.roomTempValue.toFixed(1)} °C` : "Nog geen actuele waarde"}</strong></div>
                  <div class="oq-settings-source-row"><span>Kamer-setpoint</span><strong>${Number.isFinite(model.roomSetpointValue) ? `${model.roomSetpointValue.toFixed(1)} °C` : "Nog geen actuele waarde"}</strong></div>
                </div>
              </div>
            `,
            "oq-settings-field--span-2",
          )}
          ${sourceSelector}
          ${cicField}
          ${haNote}
        </div>
        <div class="oq-helper-actions">
          <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="apply-quickstart-thermostat-source" ${busy || !model.canApply ? "disabled" : ""}>
            ${busy ? "Thermostaatconfiguratie opslaan..." : model.configurationApplied ? "Thermostaatconfiguratie opnieuw opslaan" : model.selectedSource === "OT thermostat" ? "OpenTherm-configuratie activeren" : "Thermostaatconfiguratie opslaan"}
          </button>
        </div>
        ${renderQuickStartStepNav({
          nextDisabled: !model.configurationApplied,
          nextDisabledLabel: model.isQEdition ? "Activeer eerst" : "Sla eerst op",
        })}
      </section>
    `;
  }

  export function renderQuickStartModal() {
    if (!state.quickStartModalOpen || state.loadingEntities || state.complete === null || (state.complete && state.quickStartModalMode !== "generation")) {
      return "";
    }

    if (state.quickStartModalMode === "generation") {
      return renderModalShell({
        id: "quickstart-forced",
        titleId: "oq-generation-modal-title",
        kicker: "Installatie",
        title: "Quatt Hybrid-versie aanpassen",
        copy: "Kies de versie die bij jouw Quatt hoort. Deze keuze bepaalt de basis van de regeling.",
        copyInHeader: true,
        backdropClass: "oq-helper-modal-backdrop--quickstart",
        className: "oq-helper-modal--wide oq-helper-modal--scrollable",
        sectionAttributes: 'data-oq-quickstart-scroller data-oq-quickstart-step="generation"',
        closeAction: "close-quickstart-modal",
        closeLabel: "Sluit versie-popup",
        body: renderGenerationWorkspace("picker"),
      });
    }

    return renderModalShell({
      id: "quickstart-forced",
      titleId: "oq-quickstart-modal-title",
      kicker: "Quick Start",
      title: "Rond eerst de Quick Start af",
      copy: "Bevestig eerst je configuratie en laat de stabiele main-release controleren en zo nodig installeren. Loop daarna stap voor stap door de basisinstellingen.",
      copyInHeader: true,
      backdropClass: "oq-helper-modal-backdrop--quickstart",
      className: "oq-helper-modal--wide oq-helper-modal--quickstart",
      sectionAttributes: `data-oq-quickstart-scroller data-oq-quickstart-step="${escapeHtml(getCurrentQuickStep().id)}"`,
      closeAction: "close-quickstart-modal",
      closeLabel: "Sluit Quick Start-popup",
      body: `<div class="oq-helper-grid oq-helper-grid--quickstart oq-helper-grid--quickstart-modal">${renderActiveStep()}${renderQuickStartSidebar()}</div>`,
    });
  }

  export function getQuickStartModalScrollerElement() {
    if (!state.root) {
      return null;
    }
    return state.root.querySelector("[data-oq-quickstart-scroller]");
  }

  const quickStartScrollKeeper = createScrollKeeper({
    getScroller: getQuickStartModalScrollerElement,
    getToken: () => state.quickStartScrollRestoreToken,
    setToken: (token) => { state.quickStartScrollRestoreToken = token; },
    isActive: () => state.quickStartModalOpen,
    getIdentity: (scroller) => String(scroller.dataset.oqQuickstartStep || ""),
    preserveGrowth: true,
    stickToBottom: true,
  });

  export const captureQuickStartScrollState = quickStartScrollKeeper.capture;
  export const queueQuickStartScrollRestore = quickStartScrollKeeper.queue;

  export function renderHeatingEnableQuickStartAdvice() {
    if (!hasEntity("heatingEnableSource")) {
      return "";
    }
    const advice = getHeatingEnableAdvice();
    const deviant = Boolean(advice.deviant);
    return `
      <div class="oq-helper-surface oq-settings-field oq-settings-field--span-2${deviant ? " is-warning" : ""}">
        <div class="oq-settings-field-head">
          <h3>Warmtevraag bepalen</h3>
          <p class="oq-settings-action-note" style="margin:0">Bekijk welke warmtetoestemming logisch past bij je gekozen strategie. De gekoppelde en actieve thermostaatbron is het advies.</p>
        </div>
        <div class="oq-settings-field-control">
          <button class="oq-helper-button ${deviant ? "oq-helper-button--warning-soft" : "oq-helper-button--ghost"}" type="button" data-oq-action="open-heating-strategy-advice-modal">${deviant ? '<span class="oq-advice-warn-icon"><svg viewBox="0 0 20 18" aria-hidden="true"><path d="M10 1.6 L18.2 16.4 H1.8 Z"/><rect x="9.1" y="5.4" width="1.8" height="5.8" rx="0.9"/><circle cx="10" cy="13.6" r="1.1"/></svg></span> Advies per strategie bekijken' : "Advies per strategie bekijken"}</button>
        </div>
      </div>
    `;
  }

  export function renderStrategyWorkspace() {
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("strategy"))}</p>
        <h2 class="oq-helper-section-title">Kies de verwarmingsstrategie</h2>
        <p class="oq-helper-section-copy">Kies hier hoe OpenQuatt je verwarming regelt. Daarna lopen we samen de belangrijkste instellingen langs.</p>
        ${renderHeatingStrategyExplainCards()}
        ${renderStrategySelectionFields("oq-settings-grid oq-settings-grid--quickstart")}
        ${renderQuickStartStepNav()}
      </section>
    `;
  }

  export function renderBoilerWorkspace() {
    const boilerConnectionMismatch = isEntityActive("otbConnectionMismatch");
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("boiler"))}</p>
        <h2 class="oq-helper-section-title">Aanvullende warmtebron</h2>
        <p class="oq-helper-section-copy">Dit kan bijvoorbeeld een cv-ketel, elektrische cv-ketel (e-cv) of doorstroomverwarmer zijn. Kies of de warmtebron hybride meeverwarmt bij een vermogenstekort en of deze mag overnemen wanneer geen warmtepomp beschikbaar is.</p>
        ${renderBoilerCvFields("oq-settings-grid oq-settings-grid--quickstart oq-settings-boiler-simple-grid", true)}
        ${renderQuickStartStepNav({
          nextDisabled: boilerConnectionMismatch,
        })}
      </section>
    `;
  }

  export function renderFlowWorkspace() {
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("flow"))}</p>
        <h2 class="oq-helper-section-title">Flowregeling en afstelling</h2>
        <p class="oq-helper-section-copy">Kies hier hoe OpenQuatt de pomp regelt. De Kp- en Ki-waarden en autotune vind je later terug onder Instellingen → Installatie → Flowregeling en Service & commissioning.</p>
        ${renderFlowSettingsFields("oq-settings-grid oq-settings-grid--quickstart")}
        ${renderQuickStartStepNav()}
      </section>
    `;
  }

  export function renderHeatingWorkspace() {
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("heating"))}</p>
        <h2 class="oq-helper-section-title">${escapeHtml(isCurveMode() ? "Stooklijn instellen" : "Power House instellen")}</h2>
        <p class="oq-helper-section-copy">
          ${escapeHtml(
            isCurveMode()
              ? "Stel hier je stooklijn en fallback-aanvoertemperatuur in."
              : "Stel hier in hoe Power House het warmteverlies van je woning inschat en hoe snel het reageert.",
          )}
        </p>
        ${isCurveMode()
          ? `
            <div class="oq-settings-grid oq-settings-grid--quickstart">${renderHeatingCurveProfileField()}</div>
            <div class="oq-settings-curve-shell">
              ${renderCurveGraph()}
            </div>
            ${renderSettingsCurveInputs()}
          `
          : `
            ${renderPowerHouseBaseFields("oq-settings-grid oq-settings-grid--quickstart")}
            ${renderPowerHouseAdvancedField()}
          `}
        ${renderQuickStartStepNav()}
      </section>
    `;
  }

  export function renderWaterWorkspace() {
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("water"))}</p>
        <h2 class="oq-helper-section-title">Watertemperatuur beveiligen</h2>
        <p class="oq-helper-section-copy">Hier stel je de veilige bovengrens voor de watertemperatuur in. OpenQuatt regelt richting deze grens terug en grijpt 5°C erboven hard in.</p>
        ${renderWaterSettingsFields("oq-settings-grid oq-settings-grid--quickstart", { includeSensorCorrections: false })}
        ${renderQuickStartStepNav()}
      </section>
    `;
  }

  export function renderSilentWorkspace() {
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("silent"))}</p>
        <h2 class="oq-helper-section-title">Stille uren en niveaus</h2>
        <p class="oq-helper-section-copy">Kies hier wanneer het systeem stiller moet werken, en hoe ver het dan nog mag opschalen.</p>
        ${renderSilentSettingsGrid("oq-settings-grid oq-settings-grid--quickstart")}
        ${renderQuickStartStepNav()}
      </section>
    `;
  }

  export function renderUsageTelemetryWorkspace() {
    const enabled = isEntityActive("usageTelemetryEnabled");
    const choiceConfigured = isEntityActive("usageTelemetryChoiceConfigured");
    const busy = state.loadingEntities || Boolean(state.busyAction);
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("usage-telemetry"))}</p>
        <h2 class="oq-helper-section-title">Gebruiksstatistieken</h2>
        <p class="oq-helper-section-copy">Bij een nieuwe Quick Start staat het delen van beperkte technische statistieken standaard aan. Wil je dit niet, zet delen hier uit. Je kunt de keuze later altijd wijzigen.</p>
        ${renderUsageTelemetryConsent({ enabled, busy })}
        ${renderUsageTelemetryDisclosure()}
        ${state.controlNotice ? `<p class="oq-helper-notice">${escapeHtml(state.controlNotice)}</p>` : ""}
        ${state.controlError ? `<p class="oq-helper-error">${escapeHtml(state.controlError)}</p>` : ""}
        ${state.controlError ? `
          <div class="oq-helper-actions">
            <button class="oq-helper-button" type="button" data-oq-action="retry-usage-telemetry-choice" ${busy ? "disabled" : ""}>Keuze opnieuw opslaan</button>
          </div>
        ` : ""}
        ${!choiceConfigured && !busy ? `
          <div class="oq-helper-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="confirm-no-usage-telemetry">Niet delen bevestigen</button>
          </div>
        ` : ""}
        ${renderQuickStartStepNav({
          nextDisabled: busy || !choiceConfigured || Boolean(state.controlError),
          nextDisabledLabel: busy || !choiceConfigured ? "Keuze opslaan..." : "Controleer keuze",
        })}
      </section>
    `;
  }

  export function renderConfirmWorkspace() {
    return `
      <section class="oq-helper-panel">
        <p class="oq-helper-label">${escapeHtml(getQuickStepKicker("confirm"))}</p>
        <h2 class="oq-helper-section-title">Bevestigen en afronden</h2>
        <p class="oq-helper-section-copy">Controleer nog één keer je keuzes. Met afronden markeer je Quick Start als voltooid.</p>
        ${renderConfirmReviewCards()}
        <section class="oq-helper-surface oq-helper-surface--muted" aria-label="Lokale historie">
          <h3>Lokale historie</h3>
          <p>Energiegegevens en belangrijke regelgebeurtenissen worden lokaal bewaard zodat Resultaten en diagnose ook na een herstart beschikbaar blijven. Dit kan later worden aangepast onder Instellingen → Gegevens bewaren.</p>
        </section>
        ${state.controlNotice ? `<p class="oq-helper-notice">${escapeHtml(state.controlNotice)}</p>` : ""}
        ${state.controlError ? `<p class="oq-helper-error">${escapeHtml(state.controlError)}</p>` : ""}
        <div class="oq-helper-actions oq-helper-actions--step">
          <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="previous-step" ${state.busyAction ? "disabled" : ""}>
            Vorige
          </button>
        </div>
        <div class="oq-helper-actions">
          <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="apply" ${state.busyAction ? "disabled" : ""}>
            ${state.busyAction === "apply" ? "Afronden..." : "Quick Start afronden"}
          </button>
          <button class="oq-helper-button" type="button" data-oq-action="reset" ${state.busyAction ? "disabled" : ""}>
            ${state.busyAction === "reset" ? "Resetten..." : "Setup-status resetten"}
          </button>
        </div>
      </section>
    `;
  }

  export function renderActiveStep() {
    reconcileStoredQuickStartSetupInstall();
    const activeStep = getCurrentQuickStep().id;
    if (activeStep === "setup") {
      return renderSetupWorkspace();
    }
    if (activeStep === "generation") {
      return renderGenerationWorkspace();
    }
    if (activeStep === "boiler") {
      return hasEntity("boilerCvAssistEnabled") ? renderBoilerWorkspace() : renderStrategyWorkspace();
    }
    if (activeStep === "flow-source") {
      return renderFlowSourceWorkspace();
    }
    if (activeStep === "thermostat-source") {
      return renderThermostatSourceWorkspace();
    }
    if (activeStep === "heating") {
      return renderHeatingWorkspace();
    }
    if (activeStep === "flow") {
      return renderFlowWorkspace();
    }
    if (activeStep === "water") {
      return renderWaterWorkspace();
    }
    if (activeStep === "silent") {
      return renderSilentWorkspace();
    }
    if (activeStep === "usage-telemetry") {
      return renderUsageTelemetryWorkspace();
    }
    if (activeStep === "confirm") {
      return renderConfirmWorkspace();
    }
    return renderStrategyWorkspace();
  }

  export function getQuickSteps() {
    const isQEdition = getQuickStartHardwareProfileModel().isQEdition;
    return QUICK_STEPS.filter((step) => (step.id !== "setup" || isQEdition) && (!step.optionalEntity || hasEntity(step.optionalEntity)));
  }

  export function isQuickStartStepSelectionAllowed(stepId) {
    const steps = getQuickSteps();
    const setupIndex = steps.findIndex((step) => step.id === "setup");
    const targetIndex = steps.findIndex((step) => step.id === stepId);
    return targetIndex !== -1
      && (setupIndex === -1 || state.complete === true || state.quickStartSetupUpdateComplete || targetIndex <= setupIndex);
  }

  export function getQuickStepKicker(stepId) {
    const index = getQuickSteps().findIndex((step) => step.id === stepId);
    return `Stap ${Math.max(0, index) + 1}`;
  }

  export function getQuickStepStatus(index) {
    const currentIndex = getCurrentQuickStepIndex();
    const isSelected = index === currentIndex;
    const isDone = state.complete === true || index < currentIndex;
    return {
      tone: isSelected ? "current" : isDone ? "done" : "upcoming",
      label: isSelected ? "Actief" : isDone ? "Gereed" : "Volgend",
      current: isSelected,
    };
  }

  export function renderStepOverview(compact = false) {
    return getQuickSteps().map((step, index) => {
      const stepStatus = getQuickStepStatus(index);
      const selectionAllowed = isQuickStartStepSelectionAllowed(step.id);
      return `
        <button
          class="oq-helper-field oq-helper-field--step${compact ? " oq-helper-field--compact" : ""} is-${stepStatus.tone}"
          type="button"
          data-oq-action="select-step"
          data-step-id="${escapeHtml(step.id)}"
          aria-current="${stepStatus.current ? "step" : "false"}"
          ${selectionAllowed ? "" : "disabled"}
        >
          <div class="oq-helper-field-step-head">
            <h3>${String(index + 1).padStart(2, "0")}. ${escapeHtml(step.title)}</h3>
            <span class="oq-helper-field-step-state">${stepStatus.label}</span>
          </div>
          <p>${escapeHtml(step.copy)}</p>
        </button>
      `;
    }).join("");
  }

  export function getCurrentQuickStep() {
    const steps = getQuickSteps();
    return steps.find((step) => step.id === state.currentStep) || steps[0] || QUICK_STEPS[0];
  }

  export function getCurrentQuickStepIndex() {
    return Math.max(0, getQuickSteps().findIndex((step) => step.id === state.currentStep));
  }

  export function selectQuickStepByOffset(offset) {
    const steps = getQuickSteps();
    const nextIndex = Math.min(steps.length - 1, Math.max(0, getCurrentQuickStepIndex() + offset));
    const nextStepId = steps[nextIndex]?.id || QUICK_STEPS[0].id;
    if (!isQuickStartStepSelectionAllowed(nextStepId)) {
      return false;
    }
    state.currentStep = nextStepId;
    return true;
  }

  export function renderQuickStartStepNav(options = {}) {
    const index = getCurrentQuickStepIndex();
    const steps = getQuickSteps();
    const previousStep = index > 0 ? steps[index - 1] : null;
    const nextStep = index < steps.length - 1 ? steps[index + 1] : null;

    return `
      <div class="oq-helper-step-nav">
        <div class="oq-helper-step-nav-meta">
          <strong>Stap ${index + 1} van ${steps.length}</strong>
          <span>${escapeHtml(nextStep ? `Hierna: ${nextStep.title}` : "Je bent bij de laatste stap")}</span>
        </div>
        <div class="oq-helper-actions oq-helper-actions--step">
          <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="previous-step" ${previousStep ? "" : "disabled"}>
            Vorige
          </button>
          <button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="next-step" ${nextStep && !options.nextDisabled ? "" : "disabled"}>
            ${nextStep ? options.nextDisabled ? options.nextDisabledLabel || "Configureer eerst" : "Volgende" : "Laatste stap"}
          </button>
        </div>
      </div>
    `;
  }

  export function renderQuickStartSidebar() {
    const stepIndex = getCurrentQuickStepIndex();
    const steps = getQuickSteps();
    return `
      <section class="oq-helper-panel oq-helper-panel--aside">
        <p class="oq-helper-label">Quick Start</p>
        <h2 class="oq-helper-section-title">Snel van start, stap voor stap</h2>
        <p class="oq-helper-panel-note">Quick Start helpt je op weg met de belangrijkste keuzes. Later kun je alles verder verfijnen onder Instellingen.</p>
        <h3 class="oq-helper-aside-title">Stap ${stepIndex + 1} van ${steps.length}</h3>
        <div class="oq-helper-fields oq-helper-fields--compact">
          ${renderStepOverview(true)}
        </div>
        ${state.controlNotice ? `<p class="oq-helper-notice">${escapeHtml(state.controlNotice)}</p>` : ""}
        ${state.controlError ? `<p class="oq-helper-error">${escapeHtml(state.controlError)}</p>` : ""}
      </section>
    `;
  }

  export function renderConfirmReviewCards() {
    const selectedGeneration = formatSettingsOptionLabel(getEntityStateText("hpGeneration"));
    const generationTitle = selectedGeneration ? `Geselecteerd: ${selectedGeneration}` : "";
    const generationDetection = getOduGenerationDetectionModel();
    const strategyTitle = isCurveMode() ? "Stooklijn" : "Power House";
    const formatReviewOption = (key) => formatSettingsOptionLabel(getEntityStateText(key));
    const generationLines = generationDetection.available
      ? [
          ...generationDetection.heatPumps.map((heatPump) => [
            `HP${heatPump.index} gedetecteerd`,
            heatPump.known ? heatPump.generation : "Unknown",
          ]),
          ["Aanbevolen", generationDetection.recommendation || "Geen advies"],
        ]
      : [];
    const strategyLines = isCurveMode()
      ? [
          ["Regelprofiel", formatReviewOption("curveControlProfile")],
          ["Aanvoer bij -20°C", formatValue("curveM20")],
          ["Aanvoer bij -10°C", formatValue("curveM10")],
          ["Aanvoer bij 0°C", formatValue("curve0")],
          ["Aanvoer bij 5°C", formatValue("curve5")],
          ["Aanvoer bij 10°C", formatValue("curve10")],
          ["Aanvoer bij 15°C", formatValue("curve15")],
          ["Fallback-aanvoer", formatValue("curveFallbackSupply")],
        ]
      : [
          ["Profiel", formatReviewOption("phResponseProfile")],
          ["Rated maximum house power", formatValue("housePower")],
          ["Maximum heating outdoor temperature", formatValue("houseOutdoorMax")],
          ["Temperatuurreactie", formatValue("phKp")],
          ["Comfort onder setpoint", formatValue("phComfortBelow")],
          ["Comfort boven setpoint", formatValue("phComfortAbove")],
        ];

    const flowMode = String(getEntityValue("flowControlMode") || "");
    const flowSourceModel = getQuickStartFlowSourceModel();
    const flowSourceLines = [
      ["Status", flowSourceModel.status],
      ["Actuele flow", flowSourceModel.flowAvailable ? `${Math.round(flowSourceModel.flowValue)} L/h` : "Nog geen actuele waarde"],
    ];
    const thermostatSourceModel = getQuickStartThermostatSourceModel();
    const thermostatSourceLines = [
      ["Status", thermostatSourceModel.status],
      ["Kamertemperatuur", Number.isFinite(thermostatSourceModel.roomTempValue) ? `${thermostatSourceModel.roomTempValue.toFixed(1)} °C` : "Nog geen actuele waarde"],
      ["Kamer-setpoint", Number.isFinite(thermostatSourceModel.roomSetpointValue) ? `${thermostatSourceModel.roomSetpointValue.toFixed(1)} °C` : "Nog geen actuele waarde"],
    ];
    const flowLines = [
      ["Flowregeling", flowMode === "Manual PWM" ? "Vaste pompstand" : "Gewenste flow"],
      flowMode === "Manual PWM"
        ? ["Vaste pompstand", formatValue("manualIpwm")]
        : ["Gewenste flow", formatValue("flowSetpoint")],
    ];

    const sourcePresent = hasEntity("auxHeatSourcePresent")
      ? isEntityActive("auxHeatSourcePresent")
      : isEntityActive("boilerCvAssistEnabled");
    const boilerLines = hasEntity("auxHeatSourcePresent") || hasEntity("boilerCvAssistEnabled")
      ? [
          ["Warmtebron aangesloten", sourcePresent ? "Ja" : "Nee"],
          ...(sourcePresent
            ? [
                ...(hasEntity("boilerConnection")
                  ? [["Aansturing warmtebron", String(getEntityValue("boilerConnection") || "R1") === "OpenTherm" ? "OpenTherm (OTB)" : "Aan/uit (R1)"]]
                  : []),
                ["Beschikbaar verwarmingsvermogen", formatValue("boilerRatedHeatPower")],
                ...(hasEntity("boilerCvAssistEnabled")
                  ? [["Hybride verwarmen bij vermogenstekort", isEntityActive("boilerCvAssistEnabled") ? "Aan" : "Uit"]]
                  : []),
                ...(hasEntity("boilerFaultFallbackEnabled")
                  ? [["Overnemen wanneer de warmtepomp niet beschikbaar is", isEntityActive("boilerFaultFallbackEnabled") ? "Aan" : "Uit"]]
                  : []),
              ]
            : []),
        ]
      : [];

    const waterLines = [
      ["Maximale watertemperatuur", formatValue("maxWater")],
    ];

    const silentLines = [
      ["Start stille uren", toTimeInputValue(getEntityValue("silentStartTime")) || "—"],
      ["Einde stille uren", toTimeInputValue(getEntityValue("silentEndTime")) || "—"],
      ["Maximaal tijdens stille uren", formatValue("silentMaxHz")],
      ["Maximaal overdag", formatValue("dayMaxHz")],
    ];

    const usageTelemetryLines = hasEntity("usageTelemetryEnabled")
      ? [["Technische gebruiksstatistieken", isEntityActive("usageTelemetryEnabled") ? "Delen" : "Niet delen"]]
      : [];

    const renderReviewList = (lines) => `
      <div class="oq-helper-review-list">
        ${lines
          .filter((line) => line && line[1])
          .map(
            ([label, value]) => `
              <div class="oq-helper-review-row">
                <span class="oq-helper-review-label">${escapeHtml(label)}</span>
                <strong class="oq-helper-review-value">${escapeHtml(value)}</strong>
              </div>
            `,
          )
          .join("")}
      </div>
    `;
    const renderReviewCard = (title, lines, summary = "") => `
      <article class="oq-helper-field oq-helper-field--review">
        <h3>${escapeHtml(title)}</h3>
        ${summary ? `<p class="oq-helper-review-summary"><strong>${escapeHtml(summary)}</strong></p>` : ""}
        ${renderReviewList(lines)}
      </article>
    `;

    return `
      <div class="oq-helper-fields oq-helper-fields--review">
        ${renderReviewCard("Quatt Hybrid-versie", generationLines, generationTitle)}
        ${renderReviewCard("Flowmeting", flowSourceLines, flowSourceModel.sourceLabel)}
        ${renderReviewCard("Verwarmingsstrategie", strategyLines, strategyTitle)}
        ${renderReviewCard("Watertemperatuur", waterLines)}
        ${renderReviewCard("Thermostaatgegevens", thermostatSourceLines, thermostatSourceModel.sourceLabel)}
        ${renderReviewCard("Flowregeling", flowLines)}
        ${boilerLines.length ? renderReviewCard("CV-ketel / boiler", boilerLines) : ""}
        ${renderReviewCard("Stille uren", silentLines)}
        ${usageTelemetryLines.length ? renderReviewCard("Gebruiksstatistieken", usageTelemetryLines) : ""}
      </div>
    `;
  }
