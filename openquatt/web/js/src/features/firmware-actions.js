import { hasEntity } from "../core/app-shared.js";
import { invokeActionMap } from "../core/action-router.js";
import { ENTITY_DEFS, ENTITY_REFRESH_CONCURRENCY, FIRMWARE_MODAL_KEYS, FIRMWARE_OTA_INSTALL_POLL_INTERVAL_MS, FIRMWARE_OTA_START_QUIET_MS } from "../core/config.js";
import { buildEntityPath } from "../core/domain-helpers.js";
import { setEntityBackupValue } from "../core/entity-backup.js";
import { getEntityValue } from "../core/entity-store.js";
import { isLikelyDeviceConnectionError, refreshEntities } from "../core/entity-sync.js";
import { armOtaRefresh, awaitOtaEvidence, beginDeviceReconnect, clearOtaRefresh } from "../core/device-reconnect.js";
import { clearQuickStartSetupInstall, state, storeQuickStartSetupInstall } from "../core/state.js";
import { getFirmwareConnectionLabel, getFirmwareTopologyLabel, getInstallationTopology } from "./device-context.js";
import { beginFirmwareOtaQuietWindow, clearFirmwareOtaQuietWindow, getFirmwareBuildSwitchModel, getFirmwareConnectionSwitchModel, getFirmwareCurrentVersion, getFirmwareLatestVersion, getFirmwareTestAssetUrls, getFirmwareTestPrNumber, getFirmwareTestTargetModel, getFirmwareTopologySwitchModel, getFirmwareUpdateEntity, hasKnownFirmwareTargetVersion, isFirmwareDowngradeAvailable, isFirmwareEntityAlignedWithChannel, isFirmwareUpdateEntityForBuild, pollFirmwareInstallState, pollFirmwareUpdateState, primeFirmwareInstallProgressHints, primeFirmwareUpdateState, resetFirmwareInstallUiState, resetFirmwareManualUploadSelection, resetFirmwareTestSelection } from "./firmware-update.js";
import { render } from "../core/render-scheduler.js";

  export async function requestFirmwareOta(path, options) {
    armOtaRefresh();
    try {
      const response = await fetch(path, options);
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      state.ota.ok = 1;
    } catch (error) {
      if (isLikelyDeviceConnectionError(error.message)) {
        awaitOtaEvidence();
        beginDeviceReconnect("ota", error.message);
      } else {
        clearOtaRefresh();
      }
      throw error;
    }
  }

  export async function triggerFirmwareUpdateCheck() {
    const entity = ENTITY_DEFS.checkFirmwareUpdates;
    if (!entity) {
      return;
    }

    state.updateInstallCompleted = false;
    state.updateInstallCompletedVersion = "";
    state.updateCheckBusy = true;
    state.controlError = "";
    state.controlNotice = "";
    render();

    try {
      await setFirmwareUpdateTarget("current build", { poll: false, force: true });
      primeFirmwareUpdateState();
      const response = await fetch(buildEntityPath(entity.domain, entity.name, "press"), {
        method: "POST",
      });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      await pollFirmwareUpdateState();
      state.controlNotice = "Firmwarecontrole bijgewerkt.";
    } catch (error) {
      state.controlError = `Firmwarecontrole mislukte. ${error.message}`;
    } finally {
      state.updateCheckBusy = false;
      render();
    }
  }

  export async function hydrateFirmwareUpdateModal() {
    try {
      await refreshEntities(FIRMWARE_MODAL_KEYS, "all", { concurrency: ENTITY_REFRESH_CONCURRENCY, forceMissing: true });
      if (state.updateModalOpen) {
        render();
      }
    } catch (_error) {
      // Keep the modal usable with known state; OTA actions still show detailed failures.
    }
  }

  export async function setFirmwareUpdateTarget(option, options = {}) {
    const entity = ENTITY_DEFS.firmwareUpdateTarget;
    if (!entity || !hasEntity("firmwareUpdateTarget")) {
      return false;
    }

    const value = String(option || "").trim();
    if (!value) {
      return false;
    }

    if (!options.force && String(getEntityValue("firmwareUpdateTarget") || "").trim() === value) {
      return true;
    }

    state.entities.firmwareUpdateTarget = {
      ...(state.entities.firmwareUpdateTarget || {}),
      state: value,
      value,
    };

    if (options.expectedBuildLabel
        && isFirmwareUpdateEntityForBuild(options.expectedBuildLabel)
        && hasKnownFirmwareTargetVersion()
        && isFirmwareEntityAlignedWithChannel()) {
      return true;
    }

    const response = await fetch(
      `${buildEntityPath(entity.domain, entity.name, "set")}?option=${encodeURIComponent(value)}`,
      { method: "POST" }
    );
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }

    if (options.poll !== false) {
      primeFirmwareUpdateState();
      return await pollFirmwareUpdateState({ expectedBuildLabel: options.expectedBuildLabel || "" });
    }
    return true;
  }

  export async function installFirmwareUpdate() {
    const entity = getFirmwareUpdateEntity();
    if (!entity) {
      return;
    }

    const targetVersion = getFirmwareLatestVersion(entity);
    const downgrade = isFirmwareDowngradeAvailable(entity);
    if (downgrade && state.firmwareDowngradeConfirmedVersion !== targetVersion) {
      state.controlError = "Bevestig opnieuw dat je naar de oudere main-firmware wilt teruggaan.";
      render();
      return;
    }

    state.firmwareAdvancedOpen = false;
    state.updateManualUploadOpen = false;
    state.firmwareConnectionSwitchOpen = false;
    state.firmwareTopologySwitchOpen = false;
    state.updateTestFirmwareOpen = false;
    state.firmwareConnectionSwitchConfirmed = false;
    state.firmwareTopologySwitchConfirmed = false;
    resetFirmwareManualUploadSelection();
    resetFirmwareTestSelection();
    state.updateInstallCompleted = false;
    state.updateInstallCompletedVersion = "";
    state.updateInstallBusy = true;
    state.updateInstallTargetVersion = targetVersion;
    primeFirmwareInstallProgressHints();
    state.updateInstallMode = downgrade ? "downgrade" : "normal";
    state.updateInstallTargetConnection = "";
    state.updateInstallTargetTopology = "";
    state.controlError = "";
    state.controlNotice = "";
    render();

    try {
      if (downgrade) {
        const targetReady = await setFirmwareUpdateTarget("current build", { force: true });
        const refreshedEntity = getFirmwareUpdateEntity() || {};
        const refreshedTargetVersion = getFirmwareLatestVersion(refreshedEntity);
        if (
          !targetReady
          || !isFirmwareDowngradeAvailable(refreshedEntity)
          || state.firmwareDowngradeConfirmedVersion !== refreshedTargetVersion
        ) {
          throw new Error("De main-doelversie is gewijzigd of niet meer beschikbaar. Controleer en bevestig de getoonde versie opnieuw.");
        }
        state.updateInstallTargetVersion = refreshedTargetVersion;
      } else {
        await setFirmwareUpdateTarget("current build", { poll: false, force: true });
        state.updateInstallTargetVersion = getFirmwareLatestVersion(getFirmwareUpdateEntity() || {}) || state.updateInstallTargetVersion;
      }
      beginFirmwareOtaQuietWindow();
      const installButtonEntity = ENTITY_DEFS.installFirmwareUpdateTarget;
      const installPath = installButtonEntity && hasEntity("installFirmwareUpdateTarget")
        ? buildEntityPath(installButtonEntity.domain, installButtonEntity.name, "press")
        : buildEntityPath("update", "Firmware Update", "install");
      await requestFirmwareOta(installPath, {
        method: "POST",
      });
      const completed = await pollFirmwareInstallState({
        initialDelayMs: FIRMWARE_OTA_START_QUIET_MS,
        pollDelayMs: FIRMWARE_OTA_INSTALL_POLL_INTERVAL_MS,
      });
      if (completed) {
        state.updateInstallCompleted = true;
        state.updateInstallCompletedVersion = getFirmwareCurrentVersion() || state.updateInstallTargetVersion;
        state.controlNotice = "";
      } else {
        state.controlNotice = "OTA-update gestart. Wacht tot het device weer online is.";
      }
    } catch (error) {
      state.controlError = `OTA-update is mislukt. ${error.message}`;
    } finally {
      resetFirmwareInstallUiState();
      render();
    }
  }

  export async function installFirmwareConnectionSwitch() {
    const model = getFirmwareConnectionSwitchModel();
    const buttonEntity = ENTITY_DEFS.installFirmwareUpdateTarget;
    if (!model || !model.canSwitch || !buttonEntity) {
      return;
    }
    if (!state.firmwareConnectionSwitchConfirmed) {
      state.controlError = "Bevestig eerst de waarschuwing voor de verbindingswissel.";
      render();
      return;
    }

    state.updateManualUploadOpen = false;
    state.firmwareTopologySwitchOpen = false;
    state.firmwareTopologySwitchConfirmed = false;
    resetFirmwareManualUploadSelection();
    state.updateInstallCompleted = false;
    state.updateInstallCompletedVersion = "";
    state.updateInstallBusy = true;
    state.updateInstallMode = "connection-switch";
    state.updateInstallTargetConnection = model.targetConnection;
    state.updateInstallTargetTopology = getInstallationTopology();
    state.updateInstallTargetVersion = getFirmwareCurrentVersion() || "";
    primeFirmwareInstallProgressHints();
    state.controlError = "";
    state.controlNotice = "";
    render();

    try {
      const targetReady = await setFirmwareUpdateTarget("alternate connection", {
        force: true,
        expectedBuildLabel: model.targetBuildLabel,
      });
      if (!targetReady) {
        throw new Error("Doelmanifest is nog niet geladen. Probeer het over enkele seconden opnieuw.");
      }
      state.updateInstallTargetVersion = getFirmwareLatestVersion(getFirmwareUpdateEntity() || {}) || getFirmwareCurrentVersion() || "";
      primeFirmwareInstallProgressHints();
      render();

      beginFirmwareOtaQuietWindow();
      await requestFirmwareOta(buildEntityPath(buttonEntity.domain, buttonEntity.name, "press"), {
        method: "POST",
      });

      const completed = await pollFirmwareInstallState({
        initialDelayMs: FIRMWARE_OTA_START_QUIET_MS,
        pollDelayMs: FIRMWARE_OTA_INSTALL_POLL_INTERVAL_MS,
      });
      if (completed) {
        state.updateInstallCompleted = true;
        state.updateInstallCompletedVersion = getFirmwareCurrentVersion() || state.updateInstallTargetVersion || "";
        state.firmwareConnectionSwitchConfirmed = false;
        state.controlNotice = "";
      } else {
        const targetLabel = getFirmwareConnectionLabel(model.targetConnection);
        state.controlNotice = `Verbindingswissel naar ${targetLabel} is gestart. Wacht tot het device via die verbinding terugkomt.`;
      }
    } catch (error) {
      state.controlError = `Verbindingswissel is mislukt. ${error.message}`;
    } finally {
      resetFirmwareInstallUiState();
      render();
    }
  }

  export async function installFirmwareTopologySwitch() {
    const model = getFirmwareTopologySwitchModel();
    const buttonEntity = ENTITY_DEFS.installFirmwareUpdateTarget;
    if (!model || !model.canSwitch || !buttonEntity) {
      return;
    }
    if (!state.firmwareTopologySwitchConfirmed) {
      state.controlError = "Bevestig eerst de waarschuwing voor de opstellingswissel.";
      render();
      return;
    }

    state.updateManualUploadOpen = false;
    state.firmwareConnectionSwitchOpen = false;
    state.firmwareConnectionSwitchConfirmed = false;
    state.firmwareTopologySwitchOpen = false;
    state.firmwareTopologySwitchConfirmed = false;
    resetFirmwareManualUploadSelection();
    state.updateInstallCompleted = false;
    state.updateInstallCompletedVersion = "";
    state.updateInstallBusy = true;
    state.updateInstallMode = "topology-switch";
    state.updateInstallTargetConnection = model.targetConnection;
    state.updateInstallTargetTopology = model.targetTopology;
    state.updateInstallTargetVersion = getFirmwareCurrentVersion() || "";
    primeFirmwareInstallProgressHints();
    state.controlError = "";
    state.controlNotice = "";
    render();

    try {
      const targetReady = await setFirmwareUpdateTarget("alternate topology", {
        force: true,
        expectedBuildLabel: model.targetBuildLabel,
      });
      if (!targetReady) {
        throw new Error("Doelmanifest is nog niet geladen. Probeer het over enkele seconden opnieuw.");
      }
      state.updateInstallTargetVersion = getFirmwareLatestVersion(getFirmwareUpdateEntity() || {}) || getFirmwareCurrentVersion() || "";
      primeFirmwareInstallProgressHints();
      render();

      beginFirmwareOtaQuietWindow();
      await requestFirmwareOta(buildEntityPath(buttonEntity.domain, buttonEntity.name, "press"), {
        method: "POST",
      });

      const completed = await pollFirmwareInstallState({
        initialDelayMs: FIRMWARE_OTA_START_QUIET_MS,
        pollDelayMs: FIRMWARE_OTA_INSTALL_POLL_INTERVAL_MS,
      });
      if (completed) {
        state.updateInstallCompleted = true;
        state.updateInstallCompletedVersion = getFirmwareCurrentVersion() || state.updateInstallTargetVersion || "";
        state.firmwareTopologySwitchConfirmed = false;
        state.controlNotice = "";
      } else {
        const targetLabel = getFirmwareTopologyLabel(model.targetTopology);
        state.controlNotice = `Opstellingswissel naar ${targetLabel} is gestart. Wacht tot het device met die opstelling terugkomt.`;
      }
    } catch (error) {
      state.controlError = `Opstellingswissel is mislukt. ${error.message}`;
    } finally {
      resetFirmwareInstallUiState();
      render();
    }
  }

  async function installQuickStartSetupFirmware(model) {
    const buttonEntity = ENTITY_DEFS.installFirmwareUpdateTarget;
    if (!model || !model.canInstall || !buttonEntity) {
      return;
    }

    state.updateManualUploadOpen = false;
    state.firmwareConnectionSwitchOpen = false;
    state.firmwareTopologySwitchOpen = false;
    resetFirmwareManualUploadSelection();
    state.updateInstallCompleted = false;
    state.updateInstallCompletedVersion = "";
    state.updateInstallBusy = true;
    state.updateInstallMode = "quickstart-setup";
    state.updateInstallTargetConnection = model.targetConnection;
    state.updateInstallTargetTopology = model.targetTopology;
    state.updateInstallTargetVersion = getFirmwareCurrentVersion() || "";
    state.quickStartSetupUpdateComplete = false;
    primeFirmwareInstallProgressHints();
    state.controlError = "";
    state.controlNotice = "";
    render();

    let preservePendingInstall = false;
    try {
      const targetReady = await setFirmwareUpdateTarget(model.targetOption, {
        force: true,
        expectedBuildLabel: model.targetBuildLabel,
      });
      if (!targetReady) {
        throw new Error("Doelmanifest is nog niet geladen. Probeer het over enkele seconden opnieuw.");
      }
      state.updateInstallTargetVersion = getFirmwareLatestVersion(getFirmwareUpdateEntity() || {}) || getFirmwareCurrentVersion() || "";
      if (isFirmwareDowngradeAvailable()) {
        throw new Error("Quick Start voert geen automatische downgrade uit. Bevestig de downgrade eerst bewust via Instellingen → Systeem → Updates.");
      }
      storeQuickStartSetupInstall({
        status: "pending",
        targetTopology: model.targetTopology,
        targetConnection: model.targetConnection,
        targetVersion: state.updateInstallTargetVersion,
        startedAt: Date.now(),
      });
      primeFirmwareInstallProgressHints();
      render();

      beginFirmwareOtaQuietWindow();
      await requestFirmwareOta(buildEntityPath(buttonEntity.domain, buttonEntity.name, "press"), { method: "POST" });
      awaitOtaEvidence();

      const completed = await pollFirmwareInstallState({
        initialDelayMs: FIRMWARE_OTA_START_QUIET_MS,
        pollDelayMs: FIRMWARE_OTA_INSTALL_POLL_INTERVAL_MS,
      });
      if (completed) {
        state.updateInstallCompleted = true;
        state.updateInstallCompletedVersion = getFirmwareCurrentVersion() || state.updateInstallTargetVersion || "";
        state.currentStep = "generation";
        state.quickStartSetupUpdateComplete = true;
        storeQuickStartSetupInstall({
          status: "complete",
          targetTopology: model.targetTopology,
          targetConnection: model.targetConnection,
          targetVersion: state.updateInstallCompletedVersion,
          startedAt: Date.now(),
        });
        state.controlNotice = "";
      } else {
        preservePendingInstall = true;
        state.controlNotice = `Configuratie en software-update voor ${model.targetBuildLabel} is gestart. Wacht tot het device opnieuw bereikbaar is.`;
      }
    } catch (error) {
      if (state.ota.wait) {
        preservePendingInstall = true;
        state.controlNotice = `Configuratie en software-update voor ${model.targetBuildLabel} is gestart. OpenQuatt controleert het resultaat zodra de controller terug is.`;
      } else {
        clearQuickStartSetupInstall();
        state.controlError = `Configuratie en software-update is mislukt. ${error.message}`;
      }
    } finally {
      if (preservePendingInstall) {
        state.updateInstallBusy = false;
        clearFirmwareOtaQuietWindow();
      } else {
        resetFirmwareInstallUiState();
      }
      render();
    }
  }

  export async function installQuickStartSetupSwitch() {
    const [targetTopology, targetConnection] = String(state.quickStartSetupDraft || "").split(":");
    const model = getFirmwareBuildSwitchModel(targetTopology, targetConnection);
    if (!model.available) {
      return;
    }
    if (!state.quickStartSetupConfirmed) {
      state.controlError = "Bevestig eerst dat de gekozen setup klaar is voor gebruik.";
      render();
      return;
    }
    if (!model.canInstall) {
      state.controlError = model.downgradeAvailable
        ? "Quick Start voert geen automatische downgrade uit. Bevestig de downgrade eerst bewust via Instellingen → Systeem → Updates."
        : "Deze firmware kan de gekozen configuratie en software nog niet direct installeren.";
      render();
      return;
    }
    await installQuickStartSetupFirmware(model);
  }

  export async function setFirmwareTestTextEntity(key, value) {
    if (!hasEntity(key)) {
      throw new Error(`${ENTITY_DEFS[key]?.name || key} is niet beschikbaar op deze firmware.`);
    }
    const applied = await setEntityBackupValue(key, value);
    state.entities[key] = {
      ...(state.entities[key] || {}),
      state: applied,
      value: applied,
    };
  }

  export async function installFirmwareTestUpdate() {
    const prNumber = getFirmwareTestPrNumber();
    const target = getFirmwareTestTargetModel();
    const buttonEntity = ENTITY_DEFS.installFirmwareTestOta;
    if (!prNumber) {
      state.updateTestFirmwareError = "Vul een geldig PR-nummer in.";
      render();
      return;
    }
    if (!target.available) {
      state.updateTestFirmwareError = target.error || "Dit firmwaretarget wordt niet herkend.";
      render();
      return;
    }
    if (!state.updateTestFirmwareConfirmed) {
      state.updateTestFirmwareError = "Bevestig eerst dat je testfirmware wilt installeren.";
      render();
      return;
    }
    if (!buttonEntity || !hasEntity("installFirmwareTestOta")) {
      state.updateTestFirmwareError = "Deze firmware bevat de testfirmware-installatieknop nog niet.";
      render();
      return;
    }

    state.updateManualUploadOpen = false;
    state.firmwareConnectionSwitchOpen = false;
    state.firmwareConnectionSwitchConfirmed = false;
    state.firmwareTopologySwitchOpen = false;
    state.firmwareTopologySwitchConfirmed = false;
    resetFirmwareManualUploadSelection();
    state.updateInstallCompleted = false;
    state.updateInstallCompletedVersion = "";
    state.updateInstallBusy = true;
    state.updateInstallTargetVersion = "";
    primeFirmwareInstallProgressHints();
    state.updateInstallMode = "test-firmware";
    state.updateInstallTargetConnection = "";
    state.updateInstallTargetTopology = "";
    state.controlError = "";
    state.controlNotice = "";
    state.updateTestFirmwareError = "";
    state.updateTestFirmwareBuild = null;
    render();

    let flashRequested = false;
    try {
      // PR release asset URLs are deterministic. The device reports download
      // and checksum failures, so the webapp does not need GitHub's rate-limited API.
      const testAsset = getFirmwareTestAssetUrls(prNumber, target);
      if (!testAsset) {
        throw new Error("Geen geldig PR-target gevonden.");
      }
      state.updateTestFirmwareBuild = testAsset.label;
      render();

      await setFirmwareTestTextEntity("firmwareTestOtaUrl", testAsset.otaUrl);
      await setFirmwareTestTextEntity("firmwareTestOtaMd5Url", testAsset.md5Url);

      flashRequested = true;
      beginFirmwareOtaQuietWindow();
      await requestFirmwareOta(buildEntityPath(buttonEntity.domain, buttonEntity.name, "press"), {
        method: "POST",
      });
      awaitOtaEvidence();

      const completed = await pollFirmwareInstallState({
        initialDelayMs: FIRMWARE_OTA_START_QUIET_MS,
        pollDelayMs: FIRMWARE_OTA_INSTALL_POLL_INTERVAL_MS,
      });
      if (completed) {
        state.updateInstallCompleted = true;
        state.updateInstallCompletedVersion = getFirmwareCurrentVersion() || `PR ${prNumber}`;
        state.updateTestFirmwareOpen = false;
        resetFirmwareTestSelection();
        state.controlNotice = "";
      } else {
        state.controlNotice = `Testfirmware uit PR ${prNumber} is gestart. Wacht tot het device weer online is.`;
      }
    } catch (error) {
      if (flashRequested && isLikelyDeviceConnectionError(error.message)) {
        state.controlNotice = `Testfirmware uit PR ${prNumber} is gestart. Wacht tot het device weer online is.`;
      } else {
        state.updateTestFirmwareError = `Testfirmware installeren mislukte. ${error.message}`;
      }
    } finally {
      resetFirmwareInstallUiState();
      render();
    }
  }

  export async function uploadFirmwareUpdate() {
    const file = state.updateManualUploadFile;
    if (!file) {
      state.updateManualUploadError = "Kies eerst een firmwarebestand.";
      render();
      return;
    }

    state.updateInstallCompleted = false;
    state.updateInstallCompletedVersion = "";
    state.updateInstallBusy = true;
    state.updateInstallTargetVersion = getFirmwareCurrentVersion() || "";
    primeFirmwareInstallProgressHints();
    state.updateInstallMode = "";
    state.updateInstallTargetConnection = "";
    state.updateInstallTargetTopology = "";
    state.controlError = "";
    state.controlNotice = "";
    state.updateManualUploadError = "";
    render();

    try {
      const formData = new FormData();
      formData.append("update", file, file.name || "firmware.bin");
      await requestFirmwareOta("/update", {
        method: "POST",
        body: formData,
      });
      awaitOtaEvidence();

      state.updateManualUploadOpen = false;
      resetFirmwareManualUploadSelection();
      const completed = await pollFirmwareInstallState();
      if (completed) {
        state.updateInstallCompleted = true;
        state.updateInstallCompletedVersion = getFirmwareCurrentVersion() || state.updateInstallTargetVersion || "";
        state.controlNotice = "";
      } else {
        state.controlNotice = "Handmatige OTA-upload gestart. Wacht tot het device weer online is.";
      }
    } catch (error) {
      state.updateManualUploadError = `Handmatige upload mislukte. ${error.message}`;
    } finally {
      resetFirmwareInstallUiState();
      render();
    }
  }

  const firmwareActionHandlers = {
    "open-update-modal": () => {
      state.interfacePanelOpen = false;
      state.updateModalOpen = true;
      state.firmwareDowngradeConfirmedVersion = "";
      render();
      return hydrateFirmwareUpdateModal();
    },
    "close-update-modal": () => {
      state.updateModalOpen = false;
      state.updateInstallCompleted = false;
      state.updateInstallCompletedVersion = "";
      state.firmwareAdvancedOpen = false;
      state.firmwareConnectionSwitchOpen = false;
      state.firmwareTopologySwitchOpen = false;
      state.updateManualUploadOpen = false;
      state.updateTestFirmwareOpen = false;
      state.firmwareConnectionSwitchConfirmed = false;
      state.firmwareTopologySwitchConfirmed = false;
      state.firmwareDowngradeConfirmedVersion = "";
      resetFirmwareManualUploadSelection();
      resetFirmwareTestSelection();
      render();
    },
    "run-firmware-check": () => triggerFirmwareUpdateCheck(),
    "install-firmware-update": () => installFirmwareUpdate(),
    "install-firmware-connection-switch": () => installFirmwareConnectionSwitch(),
    "install-firmware-topology-switch": () => installFirmwareTopologySwitch(),
    "toggle-firmware-advanced": () => {
      if (state.firmwareAdvancedOpen || state.firmwareConnectionSwitchOpen || state.firmwareTopologySwitchOpen || state.updateManualUploadOpen || state.updateTestFirmwareOpen) {
        state.firmwareAdvancedOpen = false;
        state.firmwareConnectionSwitchOpen = false;
        state.firmwareConnectionSwitchConfirmed = false;
        state.firmwareTopologySwitchOpen = false;
        state.firmwareTopologySwitchConfirmed = false;
        state.updateManualUploadOpen = false;
        state.updateTestFirmwareOpen = false;
        resetFirmwareManualUploadSelection();
        resetFirmwareTestSelection();
      } else {
        state.firmwareAdvancedOpen = true;
      }
      render();
    },
    "toggle-firmware-connection-switch": () => {
      state.firmwareConnectionSwitchOpen = !state.firmwareConnectionSwitchOpen;
      state.firmwareConnectionSwitchConfirmed = false;
      if (state.firmwareConnectionSwitchOpen) {
        state.firmwareAdvancedOpen = true;
        state.firmwareTopologySwitchOpen = false;
        state.firmwareTopologySwitchConfirmed = false;
        state.updateManualUploadOpen = false;
        state.updateTestFirmwareOpen = false;
        resetFirmwareManualUploadSelection();
        resetFirmwareTestSelection();
      }
      render();
    },
    "toggle-firmware-topology-switch": () => {
      state.firmwareTopologySwitchOpen = !state.firmwareTopologySwitchOpen;
      state.firmwareTopologySwitchConfirmed = false;
      if (state.firmwareTopologySwitchOpen) {
        state.firmwareAdvancedOpen = true;
        state.firmwareConnectionSwitchOpen = false;
        state.firmwareConnectionSwitchConfirmed = false;
        state.updateManualUploadOpen = false;
        state.updateTestFirmwareOpen = false;
        resetFirmwareManualUploadSelection();
        resetFirmwareTestSelection();
      }
      render();
    },
    "toggle-firmware-upload": () => {
      if (state.updateManualUploadOpen) {
        state.updateManualUploadOpen = false;
        resetFirmwareManualUploadSelection();
      } else {
        state.firmwareAdvancedOpen = true;
        state.updateManualUploadOpen = true;
        state.firmwareConnectionSwitchOpen = false;
        state.firmwareConnectionSwitchConfirmed = false;
        state.firmwareTopologySwitchOpen = false;
        state.firmwareTopologySwitchConfirmed = false;
        state.updateTestFirmwareOpen = false;
        resetFirmwareTestSelection();
        state.updateManualUploadError = "";
      }
      render();
    },
    "upload-firmware-file": () => uploadFirmwareUpdate(),
    "toggle-firmware-test": () => {
      if (state.updateTestFirmwareOpen) {
        state.updateTestFirmwareOpen = false;
        resetFirmwareTestSelection();
      } else {
        state.firmwareAdvancedOpen = true;
        state.updateTestFirmwareOpen = true;
        state.updateManualUploadOpen = false;
        state.firmwareConnectionSwitchOpen = false;
        state.firmwareConnectionSwitchConfirmed = false;
        state.firmwareTopologySwitchOpen = false;
        state.firmwareTopologySwitchConfirmed = false;
        resetFirmwareManualUploadSelection();
        state.updateTestFirmwareError = "";
      }
      render();
    },
    "install-firmware-test": () => installFirmwareTestUpdate(),
  };

  export function handleFirmwareAction(action) {
    return invokeActionMap(firmwareActionHandlers, action);
  }
