import { hasEntity } from "../core/app-shared.js";
import { FIRMWARE_MODAL_KEYS, FIRMWARE_OTA_START_QUIET_MS, FIRMWARE_RELEASE_URLS } from "../core/config.js";
import { getEntityValue } from "../core/entity-store.js";
import { refreshEntities } from "../core/entity-sync.js";
import { awaitOtaEvidence, beginDeviceReconnect, clearOtaRefresh, getDeviceReconnectCopy, getDeviceReconnectStatusCopy, getDeviceReconnectStatusLabel, getDeviceReconnectTitle, scheduleOtaRefresh } from "../core/device-reconnect.js";
import { startEntityPolling, stopEntityPolling } from "../core/entity-polling-controls.js";
import { isFirmwareOtaQuietActive } from "../core/firmware-quiet.js";
import { updateFirmwareState } from "../core/feature-state.js";
import { renderModalShell } from "../core/modal-shell.js";
import { clearQuickStartSetupInstall, getStoredQuickStartSetupInstall, state, storeQuickStartSetupInstall } from "../core/state.js";
import { getDeviceMeta, getFirmwareAlternateConnection, getFirmwareAlternateTopology, getFirmwareBuildConnection, getFirmwareBuildLabelFor, getFirmwareConnectionLabel, getFirmwareDeviceLabel, getFirmwareHardwareProfile, getFirmwareTopologyLabel, getInstallationTopology, normalizeFirmwareConnection, normalizeInstallationTopologyLabel } from "./device-context.js";
import { closeWebServerLogStream } from "./webserver-logs.js";
import { escapeHtml } from "../core/html.js";
import { render } from "../core/render-scheduler.js";

  export function getFirmwareUpdateTargetOptions() {
    const targetEntity = state.entities.firmwareUpdateTarget || {};
    if (Array.isArray(targetEntity.option)) {
      return targetEntity.option;
    }
    if (Array.isArray(targetEntity.options)) {
      return targetEntity.options;
    }
    return [];
  }

  export function hasFirmwareUpdateTargetOption(option) {
    return getFirmwareUpdateTargetOptions().includes(option);
  }

  export function getFirmwareConnectionSwitchModel() {
    const hardware = getFirmwareHardwareProfile();
    const topology = getInstallationTopology();
    const currentConnection = getFirmwareBuildConnection();
    const targetConnection = getFirmwareAlternateConnection();
    if (
      hardware !== "heatpump_controller_q"
      || (topology !== "single" && topology !== "duo")
      || (currentConnection !== "wifi" && currentConnection !== "eth")
      || !targetConnection
    ) {
      return null;
    }

    return {
      canSwitch: hasEntity("firmwareUpdateTarget")
        && hasFirmwareUpdateTargetOption("alternate connection")
        && hasEntity("installFirmwareUpdateTarget"),
      currentConnection,
      targetConnection,
      currentLabel: getFirmwareConnectionLabel(currentConnection),
      targetLabel: getFirmwareConnectionLabel(targetConnection),
      currentBuildLabel: getFirmwareBuildLabelFor(topology, currentConnection),
      targetBuildLabel: getFirmwareBuildLabelFor(topology, targetConnection),
    };
  }

  export function getFirmwareTopologySwitchModel() {
    const hardware = getFirmwareHardwareProfile();
    const currentTopology = getInstallationTopology();
    const targetTopology = getFirmwareAlternateTopology();
    const currentConnection = getFirmwareBuildConnection();
    const supportedConnections = hardware === "heatpump_controller_q" ? ["wifi", "eth"] : ["wifi"];
    if (
      !["heatpump_controller_q", "heatpump_listener", "waveshare"].includes(hardware)
      || (currentTopology !== "single" && currentTopology !== "duo")
      || !targetTopology
      || !supportedConnections.includes(currentConnection)
    ) {
      return null;
    }

    return {
      canSwitch: hasEntity("firmwareUpdateTarget")
        && hasFirmwareUpdateTargetOption("alternate topology")
        && hasEntity("installFirmwareUpdateTarget"),
      currentTopology,
      targetTopology,
      currentConnection,
      targetConnection: currentConnection,
      currentLabel: getFirmwareTopologyLabel(currentTopology),
      targetLabel: getFirmwareTopologyLabel(targetTopology),
      currentBuildLabel: getFirmwareBuildLabelFor(currentTopology, currentConnection),
      targetBuildLabel: getFirmwareBuildLabelFor(targetTopology, currentConnection),
    };
  }

  export function getFirmwareBuildSwitchModel(targetTopology, targetConnection) {
    const hardware = getFirmwareHardwareProfile();
    const currentTopology = getInstallationTopology();
    const currentConnection = getFirmwareBuildConnection();
    const topology = normalizeInstallationTopologyLabel(targetTopology);
    const connection = normalizeFirmwareConnection(targetConnection);
    const topologyChanges = topology && topology !== currentTopology;
    const connectionChanges = connection && connection !== currentConnection;
    const targetOption = topologyChanges && connectionChanges
      ? "alternate topology and connection"
      : topologyChanges
        ? "alternate topology"
        : connectionChanges
          ? "alternate connection"
          : "current build";
    const valid = hardware === "heatpump_controller_q"
      && ["single", "duo"].includes(currentTopology)
      && ["single", "duo"].includes(topology)
      && ["wifi", "eth"].includes(currentConnection)
      && ["wifi", "eth"].includes(connection);
    const targetEntityAvailable = hasEntity("firmwareUpdateTarget");
    const targetOptionAvailable = hasFirmwareUpdateTargetOption(targetOption);
    const installActionAvailable = hasEntity("installFirmwareUpdateTarget");
    const downgradeAvailable = isFirmwareDowngradeAvailable();

    return {
      available: valid,
      canInstall: valid
        && targetEntityAvailable
        && targetOptionAvailable
        && installActionAvailable
        && !downgradeAvailable,
      canSwitch: valid
        && targetOption !== "current build"
        && targetEntityAvailable
        && targetOptionAvailable
        && installActionAvailable,
      targetEntityAvailable,
      targetOptionAvailable,
      installActionAvailable,
      downgradeAvailable,
      currentTopology,
      currentConnection,
      targetTopology: topology,
      targetConnection: connection,
      targetOption,
      currentBuildLabel: getFirmwareBuildLabelFor(currentTopology, currentConnection),
      targetBuildLabel: getFirmwareBuildLabelFor(topology, connection),
    };
  }

  export function getFirmwareTestPrNumber(value = state.updateTestFirmwarePr) {
    const normalized = String(value || "").trim().replace(/^#?pr[-\s]*/i, "").replace(/^#/, "");
    return /^\d{1,6}$/.test(normalized) ? normalized : "";
  }

  export function getFirmwareTestTargetModel() {
    const hardware = getFirmwareHardwareProfile();
    const topology = getInstallationTopology();
    const connection = getFirmwareBuildConnection();
    const hardwareMap = {
      waveshare: {
        slug: "waveshare",
        label: "Waveshare",
        connections: ["wifi"],
      },
      heatpump_listener: {
        slug: "heatpump-listener",
        label: "Heatpump Listener",
        connections: ["wifi"],
      },
      heatpump_controller_q: {
        slug: "heatpump-controller-q",
        label: "Heatpump Controller Q",
        connections: ["wifi", "eth"],
      },
    };
    const profile = hardwareMap[hardware];
    if (!profile || (topology !== "single" && topology !== "duo") || !profile.connections.includes(connection)) {
      return {
        available: false,
        label: "Onbekend target",
        error: "Deze firmware meldt geen herkenbaar hardware-, opstelling- of verbindingsprofiel.",
      };
    }

    const artifactName = `openquatt-${profile.slug}-${topology}-${connection}`;
    const topologyLabel = topology === "duo" ? "Duo" : "Single";
    return {
      available: true,
      artifactName,
      otaFileName: `${artifactName}.firmware.ota.bin`,
      label: `${profile.label} ${topologyLabel} ${getFirmwareConnectionLabel(connection)}`,
    };
  }

  export function getFirmwareTestAssetUrls(prNumber = getFirmwareTestPrNumber(), target = getFirmwareTestTargetModel()) {
    const normalizedPrNumber = getFirmwareTestPrNumber(prNumber);
    if (!normalizedPrNumber || !target.available) {
      return null;
    }
    const baseUrl = `https://github.com/OpenQuatt/OpenQuatt/releases/download/pr-${normalizedPrNumber}`;
    const otaUrl = `${baseUrl}/${target.otaFileName}`;
    return {
      otaUrl,
      md5Url: `${otaUrl}.md5`,
      label: `PR ${normalizedPrNumber} · ${target.label}`,
    };
  }

  export function getUpdateStatus() {
    if (isFirmwareUpdateChecking()) {
      return "Controleren";
    }
    const progress = getFirmwareProgressModel();
    if (progress) {
      return progress.phaseLabel;
    }
    if (isFirmwareUpdateJustCompleted()) {
      return "Bijgewerkt";
    }
    if (isFirmwareUpdateInstalling()) {
      return "Bezig";
    }
    if (isFirmwareDowngradeAvailable()) {
      return "Downgrade beschikbaar";
    }
    if (isFirmwareUpdateAvailable()) {
      return "Beschikbaar";
    }
    const relation = getFirmwareVersionRelation();
    if (relation !== null && relation <= 0) {
      return "Actueel";
    }
    const meta = getDeviceMeta();
    if (typeof meta.updateLabel === "string" && meta.updateLabel.trim()) {
      return meta.updateLabel.trim();
    }
    if (meta.updateAvailable === true) {
      return "Beschikbaar";
    }
    if (meta.updateAvailable === false) {
      return "Actueel";
    }
    if (isFirmwareEffectivelyCurrent()) {
      return "Actueel";
    }
    if (getFirmwareUpdateEntity()) {
      return "Nog niet gecontroleerd";
    }
    return "—";
  }

  export function getFirmwareUpdateEntity() {
    return state.entities.firmwareUpdate || null;
  }

  export function getFirmwareUpdateState() {
    const entity = getFirmwareUpdateEntity();
    if (!entity) {
      return "";
    }
    return String(entity.state ?? entity.value ?? "").trim().toLowerCase();
  }

  export function getFirmwareProgressPhaseRaw() {
    const entity = state.entities.firmwareUpdateStatus;
    if (!entity) {
      return "";
    }
    return String(entity.state ?? entity.value ?? "").trim();
  }

  export function getFirmwareProgressPhase() {
    return getFirmwareProgressPhaseRaw().toLowerCase();
  }

  export function getFirmwareProgressPercent() {
    const entity = state.entities.firmwareUpdateProgress;
    if (!entity) {
      return Number.NaN;
    }
    const numeric = Number(entity.value ?? entity.state);
    if (Number.isNaN(numeric)) {
      return Number.NaN;
    }
    return Math.max(0, Math.min(100, numeric));
  }

  export function hasInstalledFirmwareTargetVersion() {
    const target = String(state.updateInstallTargetVersion || "").trim();
    const current = getFirmwareCurrentVersion();
    if (!target || !current || !parseFirmwareVersion(target) || !parseFirmwareVersion(current)) {
      return false;
    }
    const relation = compareFirmwareVersions(current, target);
    return state.updateInstallMode === "downgrade" ? relation === 0 : relation >= 0;
  }

  export function hasInstalledFirmwareLatestVersion(entity = getFirmwareUpdateEntity() || {}) {
    const latest = getFirmwareLatestVersion(entity);
    const current = getFirmwareCurrentVersion(entity);
    if (!latest || !current) {
      return false;
    }
    if (isFirmwareDowngradeAvailable(entity)) {
      return false;
    }
    return compareFirmwareVersions(current, latest) >= 0;
  }

  export function isFirmwareInstallSettled() {
    return (hasInstalledFirmwareTargetVersion() || hasInstalledFirmwareLatestVersion())
      && !isFirmwareUpdateChecking()
      && !isFirmwareProgressActive()
      && !isFirmwareUpdateAvailable();
  }

  export function isFirmwareInstallCompletionConfirmed() {
    if (state.updateInstallMode === "" || state.updateInstallMode === "test-firmware") {
      return Boolean(state.ota.id && !state.ota.wait);
    }
    if (state.updateInstallMode === "quickstart-setup") {
      return isQuickStartSetupInstallCompletionConfirmed();
    }
    return !isFirmwareProgressActive()
      && !isFirmwareUpdateInstalling()
      && hasInstalledFirmwareTargetVersion();
  }

  export function isQuickStartSetupInstallCompletionConfirmed() {
    if (state.updateInstallMode !== "quickstart-setup") {
      return false;
    }
    const expectedTopology = normalizeInstallationTopologyLabel(state.updateInstallTargetTopology);
    const expectedConnection = normalizeFirmwareConnection(state.updateInstallTargetConnection);
    const rebootConfirmed = Boolean(state.ota.id && !state.ota.wait)
      || (state.updateInstallResumedAfterReload && !isFirmwareProgressActive());
    return expectedTopology
      && expectedConnection
      && rebootConfirmed
      && state.updateInstallSuccessfulPhaseObserved
      && getInstallationTopology() === expectedTopology
      && getFirmwareBuildConnection() === expectedConnection
      && hasInstalledFirmwareTargetVersion()
      && !isFirmwareProgressActive()
      && !isFirmwareUpdateInstalling();
  }

  export function markQuickStartSetupInstallSuccessfulPhase() {
    if (state.updateInstallMode !== "quickstart-setup") {
      return;
    }
    state.updateInstallSuccessfulPhaseObserved = true;
    const record = getStoredQuickStartSetupInstall();
    if (record && record.status !== "complete") {
      storeQuickStartSetupInstall({ ...record, status: "successful-phase" });
    }
  }

  export function reconcileStoredQuickStartSetupInstall() {
    if (state.updateInstallMode !== "quickstart-setup" || state.updateInstallBusy) {
      return false;
    }
    const record = getStoredQuickStartSetupInstall();
    if (!record || record.status === "complete") {
      return false;
    }
    const failureMessage = getFirmwareInstallFailureMessage();
    if (failureMessage) {
      clearQuickStartSetupInstall();
      resetFirmwareInstallUiState();
      state.controlError = failureMessage;
      return false;
    }
    if (getFirmwareProgressPhase() === "rebooting") {
      markQuickStartSetupInstallSuccessfulPhase();
    }
    if (isQuickStartSetupInstallCompletionConfirmed()) {
      storeQuickStartSetupInstall({ ...record, status: "complete" });
      state.quickStartSetupUpdateComplete = true;
      state.currentStep = "generation";
      state.updateInstallCompleted = true;
      state.updateInstallCompletedVersion = getFirmwareCurrentVersion() || state.updateInstallTargetVersion || "";
      state.controlError = "";
      state.controlNotice = "";
      resetFirmwareInstallUiState();
      return true;
    }
    if (record.startedAt && Date.now() - record.startedAt > 600000) {
      clearQuickStartSetupInstall();
      resetFirmwareInstallUiState();
      state.controlError = "De eerdere software-update kon niet worden bevestigd. Controleer de verbinding en probeer opnieuw.";
    }
    return false;
  }

  export function isFirmwareUpdateJustCompleted() {
    return state.updateInstallCompleted
      && !isFirmwareUpdateChecking()
      && !getFirmwareProgressModel()
      && !isFirmwareUpdateAvailable();
  }

  export function resetFirmwareInstallUiState() {
    updateFirmwareState({
      updateInstallBusy: false,
      updateInstallTargetVersion: "",
      updateInstallPhaseHint: "",
      updateInstallProgressHint: Number.NaN,
      updateInstallStatusPollObserved: false,
      updateInstallSuccessfulPhaseObserved: false,
      updateInstallResumedAfterReload: false,
      updateInstallMode: "",
      updateInstallTargetConnection: "",
      updateInstallTargetTopology: "",
      firmwareDowngradeConfirmedVersion: "",
    });
    clearFirmwareOtaQuietWindow();
  }

  export function primeFirmwareInstallProgressHints() {
    state.updateInstallPhaseHint = "starting";
    state.updateInstallProgressHint = 0;
    state.updateInstallStatusPollObserved = false;
    state.updateInstallSuccessfulPhaseObserved = false;
    state.updateInstallResumedAfterReload = false;
  }

  export function resetFirmwareManualUploadSelection() {
    updateFirmwareState({
      updateManualUploadFile: null,
      updateManualUploadFileName: "",
      updateManualUploadError: "",
    });
  }

  export function resetFirmwareTestSelection(options = {}) {
    updateFirmwareState({
      ...(options.clearPr ? { updateTestFirmwarePr: "" } : {}),
      updateTestFirmwareConfirmed: false,
      updateTestFirmwareError: "",
      updateTestFirmwareBuild: null,
    });
  }

  export function syncFirmwareInstallHints() {
    const phase = getFirmwareProgressPhase();
    const percent = getFirmwareProgressPercent();

    const freshRebootPhase = phase !== "rebooting"
      || !state.updateInstallBusy
      || state.updateInstallStatusPollObserved;
    if ((phase === "starting" || phase === "retrying" || phase === "uploading" || phase === "rebooting") && freshRebootPhase) {
      state.updateInstallPhaseHint = phase;
      if (!Number.isNaN(percent)) {
        state.updateInstallProgressHint = phase === "rebooting"
          ? Math.max(percent, 100)
          : percent;
      }
      return;
    }

    if (!state.updateInstallBusy) {
      return;
    }

    // Setup switches commonly keep the same semantic version across builds.
    // Only a normal version update may infer rebooting from the version alone;
    // build switches must wait for the live OTA phase from the device.
    if (state.updateInstallMode === "normal" && hasInstalledFirmwareTargetVersion()) {
      state.updateInstallPhaseHint = "rebooting";
      state.updateInstallProgressHint = 100;
      return;
    }

    if (state.controlNotice.includes("opnieuw is opgestart")) {
      state.updateInstallPhaseHint = "rebooting";
      state.updateInstallProgressHint = 100;
    }
  }

  export function isFirmwareProgressActive() {
    const phase = getFirmwareProgressPhase();
    return phase === "starting" || phase === "retrying" || phase === "uploading" || phase === "rebooting";
  }

  export function getFirmwareInstallFailureMessage() {
    const phase = getFirmwareProgressPhase();
    if (phase === "error") {
      return "De firmware-installatie op het device is mislukt. Controleer de netwerkverbinding en probeer opnieuw.";
    }
    if (phase === "aborted") {
      return "De firmware-installatie is door het device afgebroken. Probeer de installatie opnieuw.";
    }
    return "";
  }

  export function getFirmwareProgressModel() {
    syncFirmwareInstallHints();

    const livePhase = getFirmwareProgressPhase();
    const hasLivePhase = livePhase === "starting"
      || livePhase === "retrying"
      || livePhase === "uploading"
      || (livePhase === "rebooting" && (!state.updateInstallBusy || state.updateInstallStatusPollObserved));
    const phase = hasLivePhase ? livePhase : state.updateInstallPhaseHint;
    const rawPercent = getFirmwareProgressPercent();
    const hintedPercent = Number.isNaN(state.updateInstallProgressHint) ? 0 : Math.round(state.updateInstallProgressHint);
    const basePercent = hasLivePhase && !Number.isNaN(rawPercent) ? Math.round(rawPercent) : hintedPercent;
    const quickStartSetup = state.updateInstallMode === "quickstart-setup";
    const quickStartSetupPending = quickStartSetup
      && getStoredQuickStartSetupInstall()?.status !== "complete";
    const switchesBuild = state.updateInstallMode === "topology-switch"
      || state.updateInstallMode === "build-switch";
    const quickStartTargetBuildLabel = getFirmwareBuildLabelFor(
      state.updateInstallTargetTopology,
      state.updateInstallTargetConnection,
    );

    if (!isFirmwareProgressActive() && !state.updateInstallBusy && !quickStartSetupPending) {
      return null;
    }

    if (phase === "rebooting") {
      return {
        phaseLabel: "Herstarten",
        percent: Math.max(basePercent, 100),
        copy: state.updateInstallMode === "test-firmware"
          ? "Testfirmware is geplaatst. Het device start opnieuw op en komt daarna vanzelf terug."
          : state.updateInstallMode === "downgrade"
          ? "De stabiele main-firmware is geplaatst. Het device start opnieuw op en komt daarna vanzelf terug."
          : state.updateInstallMode === "connection-switch"
          ? "Firmware is geplaatst. Het device start opnieuw op en komt daarna via de gekozen verbinding terug."
          : quickStartSetup
          ? `Software voor ${quickStartTargetBuildLabel} is geplaatst. De controller start opnieuw op met deze configuratie.`
          : switchesBuild
          ? "Firmware is geplaatst. Het device start opnieuw op en komt daarna met de gekozen opstelling terug."
          : "Firmware is geplaatst. Het device start nu opnieuw op en komt daarna vanzelf terug.",
      };
    }

    if (phase === "retrying") {
      return {
        phaseLabel: "Opnieuw proberen",
        percent: 0,
        copy: state.updateInstallMode === "downgrade"
          ? "De eerste verbinding voor de main-firmwaredownload mislukte. OpenQuatt probeert het automatisch nog één keer."
          : "De eerste verbinding voor de firmwaredownload mislukte. OpenQuatt probeert het automatisch nog één keer.",
      };
    }

    if (phase === "uploading") {
      return {
        phaseLabel: "Uploaden",
        percent: basePercent,
        copy: state.updateInstallMode === "test-firmware"
          ? `Testfirmware wordt nu door ${getFirmwareDeviceLabel()} gedownload en geïnstalleerd.`
          : state.updateInstallMode === "downgrade"
          ? `De stabiele main-firmware wordt nu naar ${getFirmwareDeviceLabel()} verzonden.`
          : state.updateInstallMode === "connection-switch"
          ? `De ${getFirmwareConnectionLabel(state.updateInstallTargetConnection)}-build wordt nu naar ${getFirmwareDeviceLabel()} verzonden.`
          : quickStartSetup
          ? `De ${quickStartTargetBuildLabel}-build wordt nu naar ${getFirmwareDeviceLabel()} verzonden.`
          : switchesBuild
          ? `De ${getFirmwareBuildLabelFor(state.updateInstallTargetTopology, state.updateInstallTargetConnection)}-build wordt nu naar ${getFirmwareDeviceLabel()} verzonden.`
          : `Firmware wordt nu naar ${getFirmwareDeviceLabel()} verzonden.`,
      };
    }

    return {
      phaseLabel: "Installeren",
      percent: basePercent,
      copy: state.updateInstallMode === "test-firmware"
        ? `Testfirmware-installatie is gestart voor ${getFirmwareDeviceLabel()}.`
        : state.updateInstallMode === "downgrade"
        ? `Downgrade naar de stabiele main-firmware is gestart voor ${getFirmwareDeviceLabel()}.`
        : state.updateInstallMode === "connection-switch"
        ? `Verbindingswissel naar ${getFirmwareConnectionLabel(state.updateInstallTargetConnection)} is gestart.`
        : quickStartSetup
        ? `Configuratie- en software-update voor ${quickStartTargetBuildLabel} is gestart.`
        : switchesBuild
        ? `Opstellingswissel naar ${getFirmwareTopologyLabel(state.updateInstallTargetTopology)} is gestart.`
        : `OTA-update is gestart voor ${getFirmwareDeviceLabel()}.`,
    };
  }

  export function getFirmwareLatestVersion(entity = getFirmwareUpdateEntity() || {}) {
    const latest = String(entity.latest_version || "").trim();
    if (latest) {
      return latest;
    }
    const value = String(entity.value || "").trim();
    const current = String(entity.current_version || "").trim();
    if (value && value !== current && /^v/i.test(value)) {
      return value;
    }
    return "";
  }

  export function getFirmwareCurrentVersion(entity = getFirmwareUpdateEntity() || {}) {
    const runningVersion = String(
      state.entities.projectVersionText?.state
      || state.entities.projectVersionText?.value
      || ""
    ).trim();
    if (runningVersion) {
      return runningVersion;
    }
    return String(entity.current_version || "").trim();
  }

  export function hasFirmwareBootedIntoNewerVersion(entity = getFirmwareUpdateEntity() || {}) {
    const runningVersion = getFirmwareCurrentVersion(entity);
    const recordedVersion = String(entity.current_version || "").trim();
    if (!runningVersion || !recordedVersion || runningVersion === recordedVersion) {
      return false;
    }
    return compareFirmwareVersions(runningVersion, recordedVersion) > 0;
  }

  export function isFirmwareEntityAlignedWithChannel(entity = getFirmwareUpdateEntity() || {}, channel = getFirmwareChannelLabel()) {
    const normalizedChannel = String(channel || "").trim().toLowerCase();
    const releaseUrl = String(entity.release_url || "").trim().toLowerCase();
    const latest = getFirmwareLatestVersion(entity).toLowerCase();

    if (!normalizedChannel || normalizedChannel === "—") {
      return true;
    }

    if (normalizedChannel === "dev") {
      if (releaseUrl) {
        if (releaseUrl.includes("/dev-latest")) {
          return true;
        }
        if (latest) {
          return latest.includes("-dev");
        }
      }
      return latest ? latest.includes("-dev") : false;
    }

    if (normalizedChannel === "main") {
      if (releaseUrl) {
        if (releaseUrl.includes("/dev-latest")) {
          return false;
        }
        if (latest) {
          return !latest.includes("-dev");
        }
        // release_url is set and is not a dev release; sufficient proof for main channel
        // even when latest_version is absent (e.g. state="no_update" on topology/connection switch)
        return true;
      }
      return latest ? !latest.includes("-dev") : false;
    }

    return true;
  }

  export function parseFirmwareVersion(version) {
    const raw = String(version || "").trim();
    const match = raw.match(/^v?(\d+)\.(\d+)\.(\d+)(?:-([A-Za-z]+)(?:\.(\d+))?)?/);
    if (!match) {
      return null;
    }
    return {
      major: Number(match[1]),
      minor: Number(match[2]),
      patch: Number(match[3]),
      prereleaseTag: match[4] || "",
      prereleaseNumber: match[5] ? Number(match[5]) : null,
    };
  }

  export function compareFirmwareVersions(left, right) {
    const a = parseFirmwareVersion(left);
    const b = parseFirmwareVersion(right);
    if (!a || !b) {
      return 0;
    }
    if (a.major !== b.major) {
      return a.major > b.major ? 1 : -1;
    }
    if (a.minor !== b.minor) {
      return a.minor > b.minor ? 1 : -1;
    }
    if (a.patch !== b.patch) {
      return a.patch > b.patch ? 1 : -1;
    }
    const aStable = !a.prereleaseTag;
    const bStable = !b.prereleaseTag;
    if (aStable !== bStable) {
      return aStable ? 1 : -1;
    }
    if (a.prereleaseTag !== b.prereleaseTag) {
      return a.prereleaseTag > b.prereleaseTag ? 1 : -1;
    }
    if (a.prereleaseNumber !== b.prereleaseNumber) {
      return (a.prereleaseNumber || 0) > (b.prereleaseNumber || 0) ? 1 : -1;
    }
    return 0;
  }

  export function isFirmwareUpdateInstalling() {
    if (isFirmwareInstallSettled()) {
      return false;
    }
    const raw = getFirmwareUpdateState();
    return state.updateInstallBusy
      || raw === "installing"
      || raw === "in_progress"
      || raw === "updating"
      || raw.includes("install");
  }

  export function isFirmwareUpdateChecking() {
    const raw = getFirmwareUpdateState();
    return state.updateCheckBusy
      || raw === "checking"
      || raw === "check"
      || raw === "checking_for_update"
      || raw.includes("checking");
  }

  export function isFirmwareUpdateAvailable() {
    const raw = getFirmwareUpdateState();
    if (!isFirmwareEntityAlignedWithChannel()) {
      return false;
    }
    const relation = getFirmwareVersionRelation();
    if (relation !== null) {
      return relation > 0;
    }
    if (
      raw === "installed"
      || raw === "current"
      || raw === "up_to_date"
      || raw === "none"
      || raw.includes("up to date")
      || raw.includes("no update")
    ) {
      return false;
    }
    if (raw === "available" || raw === "pending" || raw.includes("available")) {
      return true;
    }
    return getDeviceMeta().updateAvailable === true;
  }

  export function isFirmwareEffectivelyCurrent() {
    const raw = getFirmwareUpdateState();
    return raw === "installed"
      || raw === "current"
      || raw === "up_to_date"
      || raw === "none"
      || raw.includes("up to date")
      || raw.includes("no update")
      || hasFirmwareBootedIntoNewerVersion();
  }

  export function getFirmwareUpdateVersions() {
    const entity = getFirmwareUpdateEntity() || {};
    const current = getFirmwareCurrentVersion(entity) || "—";
    let latest = isFirmwareEntityAlignedWithChannel(entity) ? getFirmwareLatestVersion(entity) : "";
    const relation = getFirmwareVersionRelation(entity);
    if (!isFirmwareUpdateChecking() && relation !== null && relation <= 0 && !isFirmwareDowngradeAvailable(entity)) {
      latest = "";
    }
    return {
      current,
      latest: latest || "—",
    };
  }

  export function getFirmwareVersionRelation(entity = getFirmwareUpdateEntity() || {}) {
    const current = getFirmwareCurrentVersion(entity);
    const latest = isFirmwareEntityAlignedWithChannel(entity) ? getFirmwareLatestVersion(entity) : "";
    if (!current || !latest || !parseFirmwareVersion(current) || !parseFirmwareVersion(latest)) {
      return null;
    }
    return compareFirmwareVersions(latest, current);
  }

  export function getFirmwareReleaseUrlFallback(channel = getFirmwareChannelLabel()) {
    const normalizedChannel = String(channel || "")
      .trim()
      .toLowerCase();
    return FIRMWARE_RELEASE_URLS[normalizedChannel] || FIRMWARE_RELEASE_URLS.main;
  }

  export function getFirmwareReleaseUrl() {
    const entityUrl = String((getFirmwareUpdateEntity() || {}).release_url || "").trim();
    const fallbackUrl = getFirmwareReleaseUrlFallback();
    if (!entityUrl) {
      return fallbackUrl;
    }
    if (fallbackUrl.includes("/dev-latest") && !entityUrl.includes("/dev-latest")) {
      return fallbackUrl;
    }
    if (!fallbackUrl.includes("/dev-latest") && entityUrl.includes("/dev-latest")) {
      return fallbackUrl;
    }
    return entityUrl;
  }

  export function getFirmwareTitle() {
    return getFirmwareDeviceLabel();
  }

  export function getFirmwareChannelLabel() {
    return String(
      getEntityValue("firmwareUpdateChannel")
      || state.entities.releaseChannelText?.state
      || state.entities.releaseChannelText?.value
      || "—"
    ).trim() || "—";
  }

  export function getFirmwareRunningChannelLabel() {
    return String(
      state.entities.releaseChannelText?.state
      || state.entities.releaseChannelText?.value
      || "—"
    ).trim() || "—";
  }

  export function isFirmwareDowngradeAvailable(entity = getFirmwareUpdateEntity() || {}) {
    const selectedChannel = getFirmwareChannelLabel().toLowerCase();
    const runningChannel = getFirmwareRunningChannelLabel().toLowerCase();
    if (
      selectedChannel !== "main"
      || runningChannel !== "dev"
      || !hasEntity("installFirmwareUpdateTarget")
      || !isFirmwareEntityAlignedWithChannel(entity, selectedChannel)
    ) {
      return false;
    }
    const relation = getFirmwareVersionRelation(entity);
    return relation !== null && relation < 0;
  }

  export function hasKnownFirmwareTargetVersion() {
    return getFirmwareUpdateVersions().latest !== "—";
  }

  export function getFirmwareBuildSignature(label) {
    return String(label || "")
      .toLowerCase()
      .replace(/wi[\s-]?fi/g, "wifi")
      .replace(/[^a-z0-9]+/g, "");
  }

  export function isFirmwareUpdateEntityForBuild(buildLabel, entity = getFirmwareUpdateEntity() || {}) {
    const expected = getFirmwareBuildSignature(buildLabel);
    if (!expected) {
      return true;
    }
    const text = getFirmwareBuildSignature(`${entity.title || ""} ${entity.summary || ""}`);
    return text.includes(expected);
  }

  export function wait(ms) {
    return new Promise((resolve) => window.setTimeout(resolve, ms));
  }

  export function beginFirmwareOtaQuietWindow(durationMs = FIRMWARE_OTA_START_QUIET_MS) {
    const now = Date.now();
    const until = now + durationMs;
    state.firmwareOtaQuietUntil = Math.max(Number(state.firmwareOtaQuietUntil || 0), until);
    state.pendingEntitySyncOptions = null;
    stopEntityPolling();
    if (typeof closeWebServerLogStream === "function") {
      closeWebServerLogStream();
    }
    if (state.firmwareOtaQuietTimer) {
      window.clearTimeout(state.firmwareOtaQuietTimer);
    }
    state.firmwareOtaQuietTimer = window.setTimeout(() => {
      state.firmwareOtaQuietTimer = null;
      state.firmwareOtaQuietUntil = 0;
      if (!state.updateInstallBusy && !state.nativeOpen) {
        startEntityPolling();
      }
    }, durationMs);
  }

  export function clearFirmwareOtaQuietWindow() {
    if (state.firmwareOtaQuietTimer) {
      window.clearTimeout(state.firmwareOtaQuietTimer);
      state.firmwareOtaQuietTimer = null;
    }
    state.firmwareOtaQuietUntil = 0;
    if (!state.nativeOpen) {
      startEntityPolling();
    }
  }

  export function renderDeviceReconnectModal() {
    if (!state.deviceReconnectMode) {
      return "";
    }
    return renderModalShell({
      modalId: "reconnect",
      titleId: "oq-reconnect-modal-title",
      kicker: "Systeem",
      title: getDeviceReconnectTitle(),
      modalClass: "oq-helper-modal--reconnect",
      role: "status",
      ariaLive: "polite",
      bodyMarkup: `
        <p class="oq-helper-modal-copy">${escapeHtml(getDeviceReconnectCopy())}</p>
        <div class="oq-helper-reconnect-status">
          <span class="oq-helper-reconnect-spinner" aria-hidden="true"></span>
          <div>
            <strong>${escapeHtml(getDeviceReconnectStatusLabel())}</strong>
            <span>${escapeHtml(getDeviceReconnectStatusCopy())}</span>
          </div>
        </div>
      `,
    });
  }

  export function primeFirmwareUpdateState(channel = getFirmwareChannelLabel()) {
    const entity = getFirmwareUpdateEntity() || {};
    const current = getFirmwareCurrentVersion(entity);
    state.entities.firmwareUpdate = {
      ...entity,
      state: "CHECKING",
      value: "",
      latest_version: "",
      latestVersion: "",
      summary: "",
      release_url: getFirmwareReleaseUrlFallback(channel),
      current_version: current,
    };
  }

  export async function pollFirmwareUpdateState(options = {}) {
    const expectedBuildLabel = String(options.expectedBuildLabel || "").trim();
    for (let attempt = 0; attempt < 6; attempt += 1) {
      await wait(attempt === 0 ? 900 : 1200);
      await refreshEntities(FIRMWARE_MODAL_KEYS, "all", { forceMissing: true });
      const entityAligned = isFirmwareEntityAlignedWithChannel();
      const targetAligned = !expectedBuildLabel || isFirmwareUpdateEntityForBuild(expectedBuildLabel);
      const knownTarget = hasKnownFirmwareTargetVersion();
      const checking = isFirmwareUpdateChecking();
      const status = getUpdateStatus();
      if (entityAligned && targetAligned && (knownTarget || (!checking && status !== "Nog niet gecontroleerd"))) {
        return true;
      }
    }
    return false;
  }

  export async function pollFirmwareInstallState(options = {}) {
    let waitingForReconnect = false;
    const initialDelayMs = Number.isFinite(Number(options.initialDelayMs))
      ? Math.max(0, Number(options.initialDelayMs))
      : 700;
    const pollDelayMs = Number.isFinite(Number(options.pollDelayMs))
      ? Math.max(250, Number(options.pollDelayMs))
      : 1000;

    for (let attempt = 0; attempt < 45; attempt += 1) {
      await wait(attempt === 0 ? initialDelayMs : pollDelayMs);
      try {
        const statusEntityBeforePoll = state.entities.firmwareUpdateStatus;
        const installPollKeys = state.ota.wait
          ? [...FIRMWARE_MODAL_KEYS, "uptime"]
          : FIRMWARE_MODAL_KEYS;
        await refreshEntities(installPollKeys, "all", { forceMissing: true });
        const livePhase = getFirmwareProgressPhase();
        if (state.entities.firmwareUpdateStatus !== statusEntityBeforePoll) {
          state.updateInstallStatusPollObserved = true;
        }
        const failureMessage = getFirmwareInstallFailureMessage();
        if (failureMessage) {
          const failure = new Error(failureMessage);
          failure.firmwareInstallTerminal = true;
          throw failure;
        }
        if (livePhase === "rebooting" && state.updateInstallStatusPollObserved) {
          markQuickStartSetupInstallSuccessfulPhase();
          beginDeviceReconnect("ota");
        }
        render();

        if (state.updateInstallMode === "connection-switch") {
          const expectedConnection = normalizeFirmwareConnection(state.updateInstallTargetConnection);
          if (
            expectedConnection
            && getFirmwareBuildConnection() === expectedConnection
            && !isFirmwareProgressActive()
            && !isFirmwareUpdateInstalling()
          ) {
            scheduleOtaRefresh();
            return true;
          }
        } else if (state.updateInstallMode === "topology-switch") {
          const expectedTopology = normalizeInstallationTopologyLabel(state.updateInstallTargetTopology);
          if (
            expectedTopology
            && getInstallationTopology() === expectedTopology
            && !isFirmwareProgressActive()
            && !isFirmwareUpdateInstalling()
          ) {
            scheduleOtaRefresh();
            return true;
          }
        } else if (state.updateInstallMode === "build-switch") {
          const expectedTopology = normalizeInstallationTopologyLabel(state.updateInstallTargetTopology);
          const expectedConnection = normalizeFirmwareConnection(state.updateInstallTargetConnection);
          if (
            expectedTopology
            && expectedConnection
            && getInstallationTopology() === expectedTopology
            && getFirmwareBuildConnection() === expectedConnection
            && !isFirmwareProgressActive()
            && !isFirmwareUpdateInstalling()
          ) {
            scheduleOtaRefresh();
            return true;
          }
        } else if (isQuickStartSetupInstallCompletionConfirmed()) {
          scheduleOtaRefresh();
          return true;
        } else if (isFirmwareInstallCompletionConfirmed()) {
          scheduleOtaRefresh();
          return true;
        }
      } catch (error) {
        if (error?.firmwareInstallTerminal) {
          clearOtaRefresh();
          throw error;
        }
        if (!waitingForReconnect) {
          state.controlNotice = "Wachten tot het device opnieuw is opgestart...";
          render();
          waitingForReconnect = true;
        }
      }
    }

    awaitOtaEvidence();
    return false;
  }

  export function getFirmwareModalCopy() {
    const channel = getFirmwareChannelLabel();
    const progress = getFirmwareProgressModel();

    if (progress) {
      return progress.copy;
    }
    if (isFirmwareUpdateJustCompleted()) {
      const version = state.updateInstallCompletedVersion || getFirmwareCurrentVersion() || getFirmwareChannelLabel();
      return `${getFirmwareDeviceLabel()} draait nu op ${version}.`;
    }
    if (isFirmwareUpdateInstalling()) {
      return `OTA-update wordt voorbereid voor ${getFirmwareDeviceLabel()}. Het device kan kort herstarten.`;
    }
    if (isFirmwareUpdateChecking()) {
      return `We controleren of er op kanaal ${channel} een nieuwe firmware beschikbaar is.`;
    }
    if (isFirmwareDowngradeAvailable()) {
      const { current, latest } = getFirmwareUpdateVersions();
      return `De stabiele main-release ${latest} is ouder dan de draaiende dev-build ${current}. Je kunt bewust teruggaan naar main.`;
    }
    if (isFirmwareUpdateAvailable()) {
      return "Er staat een nieuwere firmware klaar.";
    }
    if (isFirmwareEffectivelyCurrent()) {
      return `Je draait al de nieuwste firmware op kanaal ${channel}.`;
    }
    return "Kies een kanaal en controleer of er een nieuwere firmware klaarstaat.";
  }

  export function isFirmwareAdvancedOpen() {
    return Boolean(
      state.firmwareAdvancedOpen
      || state.firmwareConnectionSwitchOpen
      || state.firmwareTopologySwitchOpen
      || state.updateManualUploadOpen
      || state.updateTestFirmwareOpen
    );
  }

  export function renderFirmwareAdvancedOption(action, title, detail, active, disabled = false) {
    return `
      <button
        class="oq-firmware-advanced-option${active ? " is-active" : ""}"
        type="button"
        data-oq-action="${escapeHtml(action)}"
        aria-pressed="${active ? "true" : "false"}"
        ${disabled ? "disabled" : ""}
      >
        <strong>${escapeHtml(title)}</strong>
        <span>${escapeHtml(detail)}</span>
      </button>
    `;
  }

  export function renderFirmwareAdvancedSection(showConnectionSwitchAction, connectionSwitchModel, showTopologySwitchAction, topologySwitchModel) {
    if (!isFirmwareAdvancedOpen()) {
      return "";
    }

    const progress = getFirmwareProgressModel();
    const busy = Boolean(progress || state.updateInstallBusy || isFirmwareUpdateChecking());

    return `
      <div class="oq-helper-modal-callout oq-helper-modal-callout--subtle oq-firmware-advanced-panel">
        <div class="oq-firmware-advanced-head">
          <div>
            <strong>Geavanceerd</strong>
            <span>Gebruik deze opties alleen als je bewust van de normale OTA-flow afwijkt.</span>
          </div>
          <button class="oq-helper-button oq-helper-button--ghost oq-firmware-advanced-hide" type="button" data-oq-action="toggle-firmware-advanced" ${busy ? "disabled" : ""}>Verbergen</button>
        </div>
        <div class="oq-firmware-advanced-options">
          ${showConnectionSwitchAction
            ? renderFirmwareAdvancedOption(
              "toggle-firmware-connection-switch",
              "Verbinding wisselen",
              `Naar ${connectionSwitchModel.targetLabel}`,
              state.firmwareConnectionSwitchOpen,
              busy,
            )
            : ""}
          ${showTopologySwitchAction
            ? renderFirmwareAdvancedOption(
              "toggle-firmware-topology-switch",
              "Opstelling wisselen",
              `Naar ${topologySwitchModel.targetLabel}`,
              state.firmwareTopologySwitchOpen,
              busy,
            )
            : ""}
          ${renderFirmwareAdvancedOption("toggle-firmware-upload", "Handmatige upload", "Lokaal OTA-bestand", state.updateManualUploadOpen, busy)}
          ${renderFirmwareAdvancedOption("toggle-firmware-test", "Testfirmware", "PR-release installeren", state.updateTestFirmwareOpen, busy)}
        </div>
        ${renderFirmwareConnectionSwitchSection()}
        ${renderFirmwareTopologySwitchSection()}
        ${renderFirmwareManualUploadSection()}
        ${renderFirmwareTestSection()}
      </div>
    `;
  }

  export function renderFirmwareConnectionSwitchSection() {
    const model = getFirmwareConnectionSwitchModel();
    if (!model || !state.firmwareConnectionSwitchOpen) {
      return "";
    }

    const progress = getFirmwareProgressModel();
    const busy = Boolean(progress || state.updateInstallBusy || isFirmwareUpdateChecking());
    const confirmed = Boolean(state.firmwareConnectionSwitchConfirmed);
    const targetIsEthernet = model.targetConnection === "eth";
    const unavailable = !model.canSwitch;
    const warning = targetIsEthernet
      ? "Sluit eerst de netwerkkabel aan. Na de herstart verdwijnt Wi-Fi uit deze firmware."
      : "Na de herstart verdwijnt Ethernet uit deze firmware. Als er geen Wi-Fi-gegevens bekend zijn, start het OpenQuatt fallback access point.";
    const statusNote = unavailable
      ? '<p class="oq-helper-modal-note oq-helper-modal-note--muted">Verbindingswissel wordt geladen. Open deze modal opnieuw of wacht een moment als de knop disabled blijft.</p>'
      : "";

    return `
      <div class="oq-firmware-advanced-detail">
        <div class="oq-firmware-advanced-detail-head">
          <strong>Verbinding wisselen</strong>
          <span>Installeer dezelfde ${escapeHtml(getFirmwareChannelLabel())}-build voor de andere netwerkverbinding.</span>
        </div>
        <div class="oq-helper-modal-grid">
          <div class="oq-helper-modal-row">
            <span class="oq-helper-modal-label">Huidige build</span>
            <strong class="oq-helper-modal-value">${escapeHtml(model.currentBuildLabel)}</strong>
          </div>
          <div class="oq-helper-modal-row">
            <span class="oq-helper-modal-label">Alternatief</span>
            <strong class="oq-helper-modal-value">${escapeHtml(model.targetBuildLabel)}</strong>
          </div>
        </div>
        <p class="oq-helper-modal-note">${escapeHtml(warning)}</p>
        ${statusNote}
        <label class="oq-helper-modal-check">
          <input type="checkbox" data-oq-firmware-connection-confirm="true" ${confirmed ? "checked" : ""} ${busy || unavailable ? "disabled" : ""}>
          <span>${escapeHtml(targetIsEthernet ? "De netwerkkabel is aangesloten." : "Ik begrijp dat Ethernet na reboot verdwijnt.")}</span>
        </label>
        <div class="oq-firmware-advanced-footer">
          <button
            class="oq-helper-button oq-helper-button--ghost"
            type="button"
            data-oq-action="install-firmware-connection-switch"
            ${busy || unavailable || !confirmed ? "disabled" : ""}
          >
            ${escapeHtml(`Wissel naar ${model.targetLabel}`)}
          </button>
        </div>
      </div>
    `;
  }

  export function renderFirmwareTopologySwitchSection() {
    const model = getFirmwareTopologySwitchModel();
    if (!model || !state.firmwareTopologySwitchOpen) {
      return "";
    }

    const progress = getFirmwareProgressModel();
    const busy = Boolean(progress || state.updateInstallBusy || isFirmwareUpdateChecking());
    const confirmed = Boolean(state.firmwareTopologySwitchConfirmed);
    const unavailable = !model.canSwitch;
    const targetIsDuo = model.targetTopology === "duo";
    const warning = targetIsDuo
      ? "Controleer eerst dat de tweede warmtepomp is aangesloten en geconfigureerd. Na de herstart bevat deze firmware HP2-regeling en HP2-diagnostiek."
      : "Na de herstart verdwijnt HP2-regeling en HP2-diagnostiek uit deze firmware. Gebruik dit alleen als deze controller als Single-installatie verder moet draaien.";
    const statusNote = unavailable
      ? '<p class="oq-helper-modal-note oq-helper-modal-note--muted">Opstellingswissel vereist firmware met de target-optie alternate topology. Werk eerst normaal bij als de knop disabled blijft.</p>'
      : "";

    return `
      <div class="oq-firmware-advanced-detail">
        <div class="oq-firmware-advanced-detail-head">
          <strong>Opstelling wisselen</strong>
          <span>Installeer dezelfde ${escapeHtml(getFirmwareChannelLabel())}-build voor de andere Single/Duo-opstelling.</span>
        </div>
        <div class="oq-helper-modal-grid">
          <div class="oq-helper-modal-row">
            <span class="oq-helper-modal-label">Huidige build</span>
            <strong class="oq-helper-modal-value">${escapeHtml(model.currentBuildLabel)}</strong>
          </div>
          <div class="oq-helper-modal-row">
            <span class="oq-helper-modal-label">Alternatief</span>
            <strong class="oq-helper-modal-value">${escapeHtml(model.targetBuildLabel)}</strong>
          </div>
        </div>
        <p class="oq-helper-modal-note">${escapeHtml(warning)}</p>
        ${statusNote}
        <label class="oq-helper-modal-check">
          <input type="checkbox" data-oq-firmware-topology-confirm="true" ${confirmed ? "checked" : ""} ${busy || unavailable ? "disabled" : ""}>
          <span>${escapeHtml(targetIsDuo ? "De tweede warmtepomp is aangesloten en hoort bij deze controller." : "Ik begrijp dat HP2-bediening na reboot verdwijnt.")}</span>
        </label>
        <div class="oq-firmware-advanced-footer">
          <button
            class="oq-helper-button oq-helper-button--ghost"
            type="button"
            data-oq-action="install-firmware-topology-switch"
            ${busy || unavailable || !confirmed ? "disabled" : ""}
          >
            ${escapeHtml(`Wissel naar ${model.targetLabel}`)}
          </button>
        </div>
      </div>
    `;
  }

  export function renderFirmwareTestSection() {
    if (!state.updateTestFirmwareOpen) {
      return "";
    }

    const progress = getFirmwareProgressModel();
    const busy = Boolean(progress || state.updateInstallBusy || isFirmwareUpdateChecking());
    const prNumber = getFirmwareTestPrNumber();
    const target = getFirmwareTestTargetModel();
    const urls = getFirmwareTestAssetUrls(prNumber, target);
    const controlsAvailable = Boolean(target.available && hasEntity("firmwareTestOtaUrl") && hasEntity("firmwareTestOtaMd5Url") && hasEntity("installFirmwareTestOta"));
    const ready = Boolean(prNumber && controlsAvailable);
    const build = state.updateTestFirmwareBuild || null;
    const targetLabel = target.available ? target.label : target.error;
    const assetNote = urls
      ? target.otaFileName
      : "Vul een PR-nummer in om de OTA-build te kiezen.";

    return `
      <div class="oq-firmware-advanced-detail">
        <div class="oq-firmware-advanced-detail-head">
          <strong>Testfirmware</strong>
          <span>PR-release voor gericht testen. Gebruik dit alleen als iemand je expliciet vraagt om een PR te testen.</span>
        </div>
        <div class="oq-firmware-test-grid">
          <label class="oq-firmware-advanced-card">
            <span class="oq-helper-modal-label">PR-nummer</span>
            <input
              class="oq-helper-input oq-helper-input--compact-number oq-firmware-test-pr-input"
              type="text"
              inputmode="numeric"
              autocomplete="off"
              placeholder="244"
              value="${escapeHtml(state.updateTestFirmwarePr || "")}"
              data-oq-firmware-test-pr="true"
              ${busy ? "disabled" : ""}
            >
          </label>
          <div class="oq-firmware-advanced-card">
            <span class="oq-helper-modal-label">Doelbuild</span>
            <strong class="oq-helper-modal-value">${escapeHtml(targetLabel)}</strong>
          </div>
          <div class="oq-firmware-advanced-card oq-firmware-test-card--asset">
            <span class="oq-helper-modal-label">OTA-bestand</span>
            <strong class="oq-helper-modal-value" data-oq-firmware-test-asset-note="true">${escapeHtml(assetNote)}</strong>
          </div>
          ${build ? `
            <div class="oq-firmware-advanced-card oq-firmware-test-card--build" data-oq-firmware-test-build-row="true">
              <span class="oq-helper-modal-label">Build</span>
              <strong class="oq-helper-modal-value">${escapeHtml(build)}</strong>
            </div>
          ` : ""}
        </div>
        <p class="oq-helper-modal-note oq-firmware-test-note">De webapp zet alleen de URL klaar; het device downloadt en flasht daarna zelf via dezelfde OTA-backend.</p>
        ${!controlsAvailable ? `<p class="oq-helper-modal-note oq-helper-modal-note--error">${escapeHtml(target.available ? "Deze firmware mist de testfirmware-bediening. Installeer eerst een nieuwere build." : target.error)}</p>` : ""}
        ${state.updateTestFirmwareError ? `<p class="oq-helper-modal-note oq-helper-modal-note--error" data-oq-firmware-test-runtime-error="true">${escapeHtml(state.updateTestFirmwareError)}</p>` : ""}
        <div class="oq-firmware-advanced-footer">
          <label class="oq-helper-modal-check oq-firmware-advanced-check">
            <input type="checkbox" data-oq-firmware-test-confirm="true" ${state.updateTestFirmwareConfirmed ? "checked" : ""} ${busy || !controlsAvailable ? "disabled" : ""}>
            <span>Ik begrijp dat dit testfirmware uit een PR is.</span>
          </label>
          <button class="oq-helper-button" type="button" data-oq-action="install-firmware-test" ${busy || !ready || !state.updateTestFirmwareConfirmed ? "disabled" : ""}>PR-firmware installeren</button>
        </div>
      </div>
    `;
  }

  export function renderFirmwareManualUploadSection() {
    if (!state.updateManualUploadOpen) {
      return "";
    }

    const progress = getFirmwareProgressModel();
    const busy = Boolean(progress || state.updateInstallBusy || isFirmwareUpdateChecking());
    const selectedFileName = String(state.updateManualUploadFileName || state.updateManualUploadFile?.name || "").trim();

    return `
      <div class="oq-firmware-advanced-detail">
        <div class="oq-firmware-advanced-detail-head">
          <strong>Handmatige upload</strong>
          <span>Gebruik dit alleen als je een geschikte OTA-firmware hebt gedownload, bij voorkeur een *.firmware.ota.bin uit de release.</span>
        </div>
        <div class="oq-firmware-advanced-card">
          <span class="oq-helper-modal-label">Firmwarebestand</span>
          <input
            class="oq-settings-backup-input oq-settings-backup-import-input"
            type="file"
            accept=".bin,application/octet-stream"
            data-oq-firmware-upload-file-input="true"
            ${busy ? "disabled" : ""}
          >
          <span class="oq-helper-modal-subvalue">${escapeHtml(selectedFileName ? `Gekozen bestand: ${selectedFileName}` : "Nog geen bestand gekozen")}</span>
        </div>
        <p class="oq-helper-modal-note">De upload gebruikt dezelfde OTA-flow als de normale update. Laat deze pagina open tot het device weer terug is.</p>
        ${state.updateManualUploadError ? `<p class="oq-helper-modal-note oq-helper-modal-note--error">${escapeHtml(state.updateManualUploadError)}</p>` : ""}
        <div class="oq-firmware-advanced-footer">
          <button class="oq-helper-button" type="button" data-oq-action="upload-firmware-file" ${busy || !state.updateManualUploadFile ? "disabled" : ""}>Upload en installeer</button>
        </div>
      </div>
    `;
  }

  export function renderUpdateModal() {
    if (!state.updateModalOpen) {
      return "";
    }

    const entity = getFirmwareUpdateEntity();
    const channelEntity = state.entities.firmwareUpdateChannel || null;
    const { current, latest } = getFirmwareUpdateVersions();
    const checking = isFirmwareUpdateChecking();
    const installing = isFirmwareUpdateInstalling();
    const available = isFirmwareUpdateAvailable();
    const downgradeAvailable = isFirmwareDowngradeAvailable(entity);
    const downgradeConfirmed = downgradeAvailable
      && state.firmwareDowngradeConfirmedVersion === latest;
    const summary = getFirmwareModalCopy();
    const progress = getFirmwareProgressModel();
    const justCompleted = isFirmwareUpdateJustCompleted();
    const releaseUrl = getFirmwareReleaseUrl();
    const title = justCompleted
      ? "Firmware-update afgerond"
      : progress
      ? "Firmware-update bezig"
      : installing
      ? "Firmware-update bezig"
      : checking
        ? "Controleren op firmware-update"
        : downgradeAvailable
          ? "Terug naar main"
        : getFirmwareTitle();
    const channelOptions = channelEntity
      ? (Array.isArray(channelEntity.option) ? channelEntity.option : Array.isArray(channelEntity.options) ? channelEntity.options : [])
      : [];
    const connectionSwitchModel = getFirmwareConnectionSwitchModel();
    const topologySwitchModel = getFirmwareTopologySwitchModel();
    const showConnectionSwitchAction = Boolean(connectionSwitchModel && !justCompleted);
    const showTopologySwitchAction = Boolean(topologySwitchModel && !justCompleted);

    return renderModalShell({
      id: "firmware-update",
      titleId: "oq-update-modal-title",
      kicker: "OTA-update",
      title,
      copy: summary,
      backdropClass: checking || installing || progress ? "is-busy" : "",
      className: "oq-helper-modal--firmware oq-helper-modal--scrollable",
      closeAction: "close-update-modal",
      closeLabel: "Sluit update-popup",
      body: `
          ${justCompleted ? `
            <div class="oq-helper-modal-success" aria-live="polite">
              <strong>Bijgewerkt</strong>
              <span>De nieuwe firmware draait nu op het device.</span>
            </div>
          ` : ""}
          ${progress ? `
            <div class="oq-helper-modal-progress" aria-live="polite">
              <div class="oq-helper-modal-progress-head">
                <strong>${escapeHtml(progress.phaseLabel)}</strong>
                <span>${escapeHtml(`${progress.percent}%`)}</span>
              </div>
              <div class="oq-helper-modal-progress-track" aria-hidden="true">
                <span class="oq-helper-modal-progress-fill" style="width:${Math.max(0, Math.min(100, progress.percent))}%"></span>
              </div>
            </div>
          ` : ""}
          <div class="oq-helper-modal-grid">
            <div class="oq-helper-modal-row">
              <span class="oq-helper-modal-label">Status</span>
              <strong class="oq-helper-modal-value">${escapeHtml(getUpdateStatus())}</strong>
            </div>
            <div class="oq-helper-modal-row">
              <span class="oq-helper-modal-label">Huidige versie</span>
              <strong class="oq-helper-modal-value">${escapeHtml(current)}</strong>
            </div>
            <div class="oq-helper-modal-row">
              <span class="oq-helper-modal-label">Beschikbare versie</span>
              <strong class="oq-helper-modal-value">${escapeHtml(latest)}</strong>
            </div>
            <div class="oq-helper-modal-row">
              <span class="oq-helper-modal-label">Kanaal</span>
              <strong class="oq-helper-modal-value">${escapeHtml(getFirmwareChannelLabel())}</strong>
            </div>
          </div>
          ${channelOptions.length ? `
            <label class="oq-helper-modal-channel">
              <span class="oq-helper-modal-label">Releasekanaal</span>
              <select data-oq-field="firmwareUpdateChannel">
                ${channelOptions.map((option) => `
                  <option value="${escapeHtml(option)}" ${String(getEntityValue("firmwareUpdateChannel") || "") === option ? "selected" : ""}>${escapeHtml(option)}</option>
                `).join("")}
              </select>
            </label>
          ` : ""}
          ${downgradeAvailable && !installing && !progress ? `
            <div class="oq-helper-modal-callout oq-firmware-downgrade-callout">
              <strong>Bewuste downgrade</strong>
              <span>Main ${escapeHtml(latest)} vervangt de nieuwere dev-build ${escapeHtml(current)}. Functies en instellingen die alleen in dev bestaan, zijn daarna mogelijk niet meer beschikbaar.</span>
              <label class="oq-helper-modal-check">
                <input type="checkbox" data-oq-firmware-downgrade-confirm="true" ${downgradeConfirmed ? "checked" : ""} ${checking ? "disabled" : ""}>
                <span>Ik begrijp dat ik terugga naar een oudere stabiele firmwareversie.</span>
              </label>
            </div>
          ` : ""}
          <p class="oq-helper-modal-note">${downgradeAvailable
            ? "Maak zo nodig eerst een instellingenbackup. Laat deze pagina open; het device herstart na de downgrade en komt daarna vanzelf weer terug."
            : "Laat deze pagina open tijdens de OTA-update. Het device kan na installatie kort herstarten en daarna vanzelf weer terugkomen. Bestaande OpenQuatt-instellingen blijven behouden."}</p>
          <div class="oq-helper-modal-actions oq-firmware-modal-actions">
            <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="run-firmware-check" ${checking || installing || progress ? "disabled" : ""}>
              ${checking ? "Controleren..." : "Controleer opnieuw"}
            </button>
            ${justCompleted
              ? '<button class="oq-helper-button oq-helper-button--primary" type="button" data-oq-action="close-update-modal">Gereed</button>'
              : `<button class="oq-helper-button${downgradeAvailable ? " oq-helper-button--warning" : ""}" type="button" data-oq-action="install-firmware-update" ${(!available && !downgradeAvailable) || (downgradeAvailable && !downgradeConfirmed) || installing || checking || progress || !entity ? "disabled" : ""}>
              ${installing ? (state.updateInstallMode === "downgrade" ? "Downgraden..." : "Bijwerken...") : downgradeAvailable ? `Terug naar main ${escapeHtml(latest)}` : "Nu bijwerken"}
            </button>`}
            ${releaseUrl ? `<a class="oq-helper-button oq-helper-button--ghost oq-helper-modal-link" href="${escapeHtml(releaseUrl)}" target="_blank" rel="noreferrer">Release notes</a>` : ""}
            ${isFirmwareAdvancedOpen() ? "" : `
              <button class="oq-helper-button oq-helper-button--ghost" type="button" data-oq-action="toggle-firmware-advanced" ${checking || installing || progress ? "disabled" : ""}>
                Geavanceerd
              </button>
            `}
          </div>
          ${renderFirmwareAdvancedSection(showConnectionSwitchAction, connectionSwitchModel, showTopologySwitchAction, topologySwitchModel)}`,
    });
  }
