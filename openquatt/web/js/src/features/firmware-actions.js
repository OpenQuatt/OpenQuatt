import { hasEntity } from "../core/app-shared.js";
import { invokeActionMap } from "../core/action-router.js";
import { ENTITY_DEFS, ENTITY_REFRESH_CONCURRENCY, FIRMWARE_MODAL_KEYS, FIRMWARE_OTA_INSTALL_POLL_INTERVAL_MS, FIRMWARE_OTA_START_QUIET_MS } from "../core/config.js";
import { buildEntityPath } from "../core/domain-helpers.js";
import { setEntityBackupValue } from "../core/entity-backup.js";
import { getEntityValue } from "../core/entity-store.js";
import { isLikelyDeviceConnectionError, refreshEntities } from "../core/entity-sync.js";
import { armOtaRefresh, awaitOtaEvidence, beginDeviceReconnect, clearOtaRefresh } from "../core/device-reconnect.js";
import { clearQuickStartSetupInstall, state, storeQuickStartSetupInstall } from "../core/state.js";
import { getFirmwareBuildConnection, getFirmwareConnectionLabel, getFirmwareTopologyLabel, getInstallationTopology } from "./device-context.js";
import { formatNumber, t } from "../i18n/index.js";
import { beginFirmwareOtaQuietWindow, clearFirmwareOtaQuietWindow, getFirmwareBuildSwitchModel, getFirmwareConnectionSwitchModel, getFirmwareCurrentVersion, getFirmwareLatestVersion, getFirmwareRunningChannelLabel, getFirmwareTestAssetUrls, getFirmwareTestPrNumber, getFirmwareTestTargetModel, getFirmwareTopologySwitchModel, getFirmwareUpdateEntity, hasFirmwareTestLegacyCapability, hasFirmwareTestManifestCapability, hasKnownFirmwareTargetVersion, isFirmwareChannelTransition, isFirmwareDowngradeAvailable, isFirmwareEntityAlignedWithChannel, isFirmwareUpdateEntityForBuild, isQuickStartSetupFirmwareCurrent, pollFirmwareInstallState, pollFirmwareUpdateState, primeFirmwareInstallProgressHints, primeFirmwareUpdateState, resetFirmwareInstallUiState, resetFirmwareManualUploadSelection, resetFirmwareTestSelection, wait } from "./firmware-update.js";
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
      state.controlNotice = t("firmware.checkUpdated");
    } catch (error) {
      state.controlError = t("firmware.checkFailed", { error: error.message });
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

    if (!options.force
        && options.expectedBuildLabel
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

  async function setQuickStartFirmwareUpdateChannelMain() {
    const entity = ENTITY_DEFS.firmwareUpdateChannel;
    const channelEntity = state.entities.firmwareUpdateChannel || {};
    const channelOptions = Array.isArray(channelEntity.option)
      ? channelEntity.option
      : Array.isArray(channelEntity.options) ? channelEntity.options : [];
    if (!entity || !hasEntity("firmwareUpdateChannel") || !channelOptions.includes("main")) {
      return false;
    }
    const alreadyReady = String(getEntityValue("firmwareUpdateChannel") || "").trim() === "main"
      && isFirmwareEntityAlignedWithChannel(getFirmwareUpdateEntity() || {}, "main")
      && hasKnownFirmwareTargetVersion();
    if (alreadyReady) {
      return true;
    }

    state.entities.firmwareUpdateChannel = {
      ...channelEntity,
      state: "main",
      value: "main",
    };
    const response = await fetch(
      `${buildEntityPath(entity.domain, entity.name, "set")}?option=main`,
      { method: "POST" },
    );
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}`);
    }
    primeFirmwareUpdateState("main");
    render();
    const ready = await pollFirmwareUpdateState();
    if (ready) {
      // Firmware accepts at most one manifest check per second. Leave enough room
      // for the target switch below to start its own check on the first attempt.
      await wait(1100);
    }
    return ready;
  }

  export async function installFirmwareUpdate() {
    const entity = getFirmwareUpdateEntity();
    if (!entity) {
      return;
    }

    const targetVersion = getFirmwareLatestVersion(entity);
    const downgrade = isFirmwareDowngradeAvailable(entity);
    const channelSwitch = isFirmwareChannelTransition(entity);
    if (downgrade && state.firmwareDowngradeConfirmedVersion !== targetVersion) {
      state.controlError = t("firmware.confirmDowngrade");
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
    state.updateInstallMode = downgrade ? "downgrade" : channelSwitch ? "channel-switch" : "normal";
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
          throw new Error(t("firmware.mainTargetChanged"));
        }
        state.updateInstallTargetVersion = refreshedTargetVersion;
      } else {
        await setFirmwareUpdateTarget("current build", { poll: false, force: true });
        if (channelSwitch) {
          if (!isFirmwareChannelTransition() || getFirmwareLatestVersion() !== targetVersion) {
            throw new Error(t("firmware.devTargetChanged"));
          }
        } else {
          state.updateInstallTargetVersion = getFirmwareLatestVersion(getFirmwareUpdateEntity() || {}) || state.updateInstallTargetVersion;
        }
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
        state.controlNotice = t("firmware.otaStarted");
      }
    } catch (error) {
      state.controlError = t("firmware.otaFailed", { error: error.message });
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
      state.controlError = t("firmware.confirmConnectionSwitch");
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
        throw new Error(t("firmware.connectionTargetMissing"));
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
        state.controlNotice = t("firmware.connectionStarted", { target: targetLabel });
      }
    } catch (error) {
      state.controlError = t("firmware.connectionFailed", { error: error.message });
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
      state.controlError = t("firmware.confirmTopologySwitch");
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
        throw new Error(t("firmware.connectionTargetMissing"));
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
        state.controlNotice = t("firmware.topologyStarted", { target: targetLabel });
      }
    } catch (error) {
      state.controlError = t("firmware.topologyFailed", { error: error.message });
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
    state.updateInstallPhaseHint = "";
    state.updateInstallProgressHint = Number.NaN;
    state.controlError = "";
    state.controlNotice = "";
    render();

    const sourceTopology = model.currentTopology;
    const sourceConnection = model.currentConnection;
    const sourceChannel = getFirmwareRunningChannelLabel().toLowerCase();
    const sourceVersion = getFirmwareCurrentVersion() || "";
    let preservePendingInstall = false;
    try {
      const mainChannelReady = await setQuickStartFirmwareUpdateChannelMain();
      if (!mainChannelReady) {
        throw new Error(t("firmware.mainCheckFailed"));
      }
      const targetReady = await setFirmwareUpdateTarget(model.targetOption, {
        force: true,
        expectedBuildLabel: model.targetBuildLabel,
      });
      if (!targetReady) {
        throw new Error(t("firmware.connectionTargetMissing"));
      }
      state.updateInstallTargetVersion = getFirmwareLatestVersion(getFirmwareUpdateEntity() || {}) || getFirmwareCurrentVersion() || "";
      if (!isFirmwareEntityAlignedWithChannel(getFirmwareUpdateEntity() || {}, "main")
          || !isFirmwareUpdateEntityForBuild(model.targetBuildLabel)
          || !hasKnownFirmwareTargetVersion()) {
        throw new Error(t("firmware.mainMismatch"));
      }
      if (isQuickStartSetupFirmwareCurrent(model)) {
        state.currentStep = "generation";
        state.quickStartSetupUpdateComplete = true;
        storeQuickStartSetupInstall({
          status: "complete",
          targetTopology: model.targetTopology,
          targetConnection: model.targetConnection,
          targetChannel: "main",
          targetVersion: state.updateInstallTargetVersion,
          startedAt: Date.now(),
        });
        state.controlNotice = t("firmware.alreadyCurrent");
        return;
      }
      storeQuickStartSetupInstall({
        status: "pending",
        targetTopology: model.targetTopology,
        targetConnection: model.targetConnection,
        targetChannel: "main",
        targetVersion: state.updateInstallTargetVersion,
        startedAt: Date.now(),
        sourceTopology,
        sourceConnection,
        sourceChannel,
        sourceVersion,
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
          targetChannel: "main",
          targetVersion: state.updateInstallCompletedVersion,
          startedAt: Date.now(),
        });
        state.controlNotice = "";
      } else {
        preservePendingInstall = true;
        state.controlNotice = t("firmware.setupUpdateStarted", { build: model.targetBuildLabel });
      }
    } catch (error) {
      if (state.ota.wait && !error.firmwareInstallTerminal) {
        preservePendingInstall = true;
        state.controlNotice = t("firmware.setupUpdateStartedCheck", { build: model.targetBuildLabel });
      } else {
        clearQuickStartSetupInstall();
        state.controlError = t("firmware.setupUpdateFailed", { error: error.message });
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
    const currentSetup = `${getInstallationTopology()}:${getFirmwareBuildConnection()}`;
    const [targetTopology, targetConnection] = String(state.quickStartSetupDraft || currentSetup).split(":");
    const model = getFirmwareBuildSwitchModel(targetTopology, targetConnection);
    if (!model.available) {
      return;
    }
    if (!state.quickStartSetupConfirmed) {
      state.controlError = t("firmware.setupNotReady");
      render();
      return;
    }
    if (!model.canInstall) {
      state.controlError = t("firmware.setupNoMainInstall");
      render();
      return;
    }
    await installQuickStartSetupFirmware(model);
  }

  export function keepCurrentQuickStartSetup() {
    const [targetTopology, targetConnection] = String(state.quickStartSetupDraft || "").split(":");
    const model = getFirmwareBuildSwitchModel(targetTopology, targetConnection);
    if (!model.available) {
      state.controlError = t("firmware.setupUnconfirmed");
      render();
      return;
    }
    if (!state.quickStartSetupConfirmed) {
      state.controlError = t("firmware.setupNotReady");
      render();
      return;
    }
    if (model.currentTopology !== model.targetTopology || model.currentConnection !== model.targetConnection) {
      state.controlError = t("firmware.setupKeepRequiresActive");
      render();
      return;
    }

    const currentVersion = getFirmwareCurrentVersion() || "";
    if (isQuickStartSetupFirmwareCurrent(model)) {
      storeQuickStartSetupInstall({
        status: "complete",
        targetTopology: model.targetTopology,
        targetConnection: model.targetConnection,
        targetChannel: "main",
        targetVersion: currentVersion,
        startedAt: Date.now(),
      });
      state.currentStep = "generation";
      state.quickStartSetupUpdateComplete = true;
      state.controlError = "";
      state.controlNotice = t("firmware.alreadyCurrent");
      render();
      return;
    }

    const runningChannel = String(getFirmwareRunningChannelLabel() || "").trim().toLowerCase();
    const currentChannel = ["main", "dev"].includes(runningChannel) ? runningChannel : "current";
    storeQuickStartSetupInstall({
      status: "skipped",
      targetTopology: model.targetTopology,
      targetConnection: model.targetConnection,
      targetChannel: currentChannel,
      targetVersion: currentVersion,
      startedAt: Date.now(),
    });
    state.currentStep = "generation";
    state.quickStartSetupUpdateComplete = true;
    state.controlError = "";
    state.controlNotice = currentVersion
      ? t("firmware.qsContinueVersion", { version: currentVersion })
      : t("firmware.qsContinueCurrent");
    render();
  }

  export async function setFirmwareTestTextEntity(key, value) {
    if (!hasEntity(key)) {
      throw new Error(t("firmware.entityUnavailable", { name: ENTITY_DEFS[key]?.name || key }));
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
    const useManifest = hasFirmwareTestManifestCapability();
    const useLegacy = !useManifest && hasFirmwareTestLegacyCapability();
    const buttonEntity = useManifest
      ? ENTITY_DEFS.installFirmwareTestManifest
      : useLegacy ? ENTITY_DEFS.installFirmwareTestOta : null;
    if (!prNumber) {
      state.updateTestFirmwareError = t("firmware.testPrInvalid");
      render();
      return;
    }
    if (!target.available) {
      state.updateTestFirmwareError = target.error || t("firmware.testTargetUnknown");
      render();
      return;
    }
    if (!state.updateTestFirmwareConfirmed) {
      state.updateTestFirmwareError = t("firmware.testConfirmFirst");
      render();
      return;
    }
    if (!buttonEntity || (!useManifest && !useLegacy)) {
      state.updateTestFirmwareError = t("firmware.testNoButton");
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

    let flashRequested = false;
    try {
      const testAsset = getFirmwareTestAssetUrls(prNumber, target);
      if (!testAsset || (useManifest && !testAsset.manifestUrl) || (useLegacy && !testAsset.otaUrl)) {
        throw new Error(t("firmware.testTargetMissing"));
      }
      state.updateTestFirmwareBuild = testAsset.label;
      render();

      if (useManifest) {
        // PR-manifest-URL is deterministisch. Het device valideert fail-closed
        // op repository, PR-tag en exact target en gebruikt daarna dezelfde
        // bewaakte update.check → update.perform-lifecycle als main/dev.
        await setFirmwareTestTextEntity("firmwareTestManifestUrl", testAsset.manifestUrl);
      } else {
        // Legacy fallback voor firmware zonder manifest-capability (#607).
        // Let op: voor oude uniforme HCQ bestaan geen wifi/eth bins meer.
        await setFirmwareTestTextEntity("firmwareTestOtaUrl", testAsset.otaUrl);
        await setFirmwareTestTextEntity("firmwareTestOtaMd5Url", testAsset.md5Url);
      }

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
        state.controlNotice = t("firmware.testStarted", { pr: formatNumber(prNumber, { maximumFractionDigits: 0 }) });
      }
    } catch (error) {
      if (flashRequested && isLikelyDeviceConnectionError(error.message)) {
        state.controlNotice = t("firmware.testStarted", { pr: formatNumber(prNumber, { maximumFractionDigits: 0 }) });
      } else {
        state.updateTestFirmwareError = t("firmware.testInstallFailed", { error: error.message });
      }
    } finally {
      resetFirmwareInstallUiState();
      render();
    }
  }

  export async function uploadFirmwareUpdate() {
    const file = state.updateManualUploadFile;
    if (!file) {
      state.updateManualUploadError = t("firmware.uploadChooseFile");
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
        state.controlNotice = t("firmware.uploadStarted");
      }
    } catch (error) {
      state.updateManualUploadError = t("firmware.uploadFailed", { error: error.message });
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
