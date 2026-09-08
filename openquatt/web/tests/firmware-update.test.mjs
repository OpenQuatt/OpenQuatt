import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = {
  clearTimeout,
  localStorage: { getItem: () => null },
  matchMedia: () => ({ matches: false }),
  setTimeout,
};

const { state } = await import("../js/src/core/state.js");
const { setRenderCallback } = await import("../js/src/core/render-scheduler.js");
const { installFirmwareTestUpdate, installFirmwareUpdate } = await import("../js/src/features/firmware-actions.js");
const {
  getFirmwareModalCopy,
  compareFirmwareVersions,
  hasInstalledFirmwareLatestVersion,
  hasInstalledFirmwareTargetVersion,
  isFirmwareChannelTransition,
  isFirmwareInstallSettled,
  getFirmwareProgressModel,
  getFirmwareTestAssetUrls,
  getFirmwareUpdateVersions,
  getUpdateStatus,
  isFirmwareDowngradeAvailable,
  isFirmwareInstallCompletionConfirmed,
  isFirmwareUpdateAvailable,
  isFirmwareUpdateJustCompleted,
  primeFirmwareInstallProgressHints,
  renderUpdateModal,
} = await import("../js/src/features/firmware-update.js");

function setDevToMainDowngradeState() {
  state.drafts = {};
  state.entities = {
    firmwareUpdate: {
      state: "UPDATE AVAILABLE",
      value: "v0.47.0",
      current_version: "v0.48.0-dev.696+86f5997",
      latest_version: "v0.47.0",
      release_url: "https://github.com/OpenQuatt/OpenQuatt/releases/tag/v0.47.0",
    },
    firmwareUpdateChannel: { state: "main", value: "main", option: ["main", "dev"] },
    installFirmwareUpdateTarget: { state: "" },
    projectVersionText: { state: "v0.48.0-dev.696+86f5997", value: "v0.48.0-dev.696+86f5997" },
    releaseChannelText: { state: "dev", value: "dev" },
  };
  state.updateCheckBusy = false;
  state.updateInstallBusy = false;
  state.updateInstallCompleted = false;
  state.updateInstallCompletedVersion = "";
  state.updateInstallMode = "";
  state.updateInstallTargetVersion = "";
  state.updateInstallPhaseHint = "";
  state.updateInstallProgressHint = Number.NaN;
  state.updateInstallStatusPollObserved = false;
  state.firmwareDowngradeConfirmedVersion = "";
  state.updateModalOpen = true;
}

function setPrToDevState() {
  state.drafts = {};
  state.entities = {
    firmwareUpdate: {
      state: "UPDATE AVAILABLE",
      value: "v0.49.0-dev.740+2f65a08",
      current_version: "v0.49.0-pr.555.1321+222bde1",
      latest_version: "v0.49.0-dev.740+2f65a08",
      release_url: "https://github.com/OpenQuatt/OpenQuatt/releases/tag/dev-latest",
    },
    firmwareUpdateChannel: { state: "dev", value: "dev", option: ["main", "dev"] },
    projectVersionText: { state: "v0.49.0-pr.555.1321+222bde1", value: "v0.49.0-pr.555.1321+222bde1" },
    releaseChannelText: { state: "dev", value: "dev" },
  };
  state.updateCheckBusy = false;
  state.updateInstallBusy = false;
  state.updateInstallCompleted = false;
  state.updateInstallCompletedVersion = "";
  state.updateInstallMode = "";
  state.updateInstallTargetVersion = "";
  state.updateInstallPhaseHint = "";
  state.updateInstallProgressHint = Number.NaN;
  state.updateInstallStatusPollObserved = false;
  state.firmwareDowngradeConfirmedVersion = "";
  state.updateModalOpen = true;
}

test("PR firmware uses deterministic release URLs without the GitHub REST API", () => {
  const target = {
    available: true,
    label: "Heatpump Controller Q Duo",
    artifactName: "openquatt-heatpump-controller-q-duo",
    otaFileName: "openquatt-heatpump-controller-q-duo.firmware.ota.bin",
    manifestFileName: "openquatt-heatpump-controller-q-duo-ota.manifest.json",
  };

  assert.deepEqual(getFirmwareTestAssetUrls(395, target), {
    otaUrl: "https://github.com/OpenQuatt/OpenQuatt/releases/download/pr-395/openquatt-heatpump-controller-q-duo.firmware.ota.bin",
    md5Url: "https://github.com/OpenQuatt/OpenQuatt/releases/download/pr-395/openquatt-heatpump-controller-q-duo.firmware.ota.bin.md5",
    manifestUrl: "https://github.com/OpenQuatt/OpenQuatt/releases/download/pr-395/openquatt-heatpump-controller-q-duo-ota.manifest.json",
    manifestFileName: "openquatt-heatpump-controller-q-duo-ota.manifest.json",
    label: "PR 395 · Heatpump Controller Q Duo",
  });
  assert.equal(getFirmwareTestAssetUrls("395/../../dev-latest", target), null);
});

test("PR test firmware can return to the dev channel", () => {
  setPrToDevState();

  assert.equal(isFirmwareUpdateAvailable(), true);
  assert.deepEqual(getFirmwareUpdateVersions(), {
    current: "v0.49.0-pr.555.1321+222bde1",
    latest: "v0.49.0-dev.740+2f65a08",
  });
  assert.equal(getUpdateStatus(), "Beschikbaar");
  assert.match(getFirmwareModalCopy(), /Dev-firmware kan de PR-testfirmware vervangen/);

  const modal = renderUpdateModal();
  const installButton = modal.match(/<button class="oq-helper-button[^"]*" type="button" data-oq-action="install-firmware-update"[^>]*>/)?.[0] || "";
  assert.doesNotMatch(installButton, /disabled/);
});

function setMainToDevState(target = "v0.49.1-dev.780+abcdef0") {
  setPrToDevState();
  state.entities.projectVersionText = { state: "v0.49.1" };
  state.entities.releaseChannelText = { state: "main" };
  Object.assign(state.entities.firmwareUpdate, {
    state: "NO UPDATE",
    current_version: "v0.49.1",
    latest_version: target,
    value: target,
  });
}

test("main to dev remains available regardless of semantic version ordering", () => {
  for (const target of ["v0.49.1-dev.780+abcdef0", "v0.50.0-dev.1+abcdef0", "v0.48.0-dev.1+abcdef0"]) {
    setMainToDevState(target);
    assert.equal(isFirmwareChannelTransition(), true);
    assert.equal(isFirmwareUpdateAvailable(), true);
    assert.equal(getUpdateStatus(), "Beschikbaar");
    assert.equal(getFirmwareUpdateVersions().latest, target);
    assert.equal(hasInstalledFirmwareLatestVersion(), false);
    assert.match(getFirmwareModalCopy(), /Dev-firmware kan de huidige main-firmware vervangen/);
    const button = renderUpdateModal().match(/<button[^>]*data-oq-action="install-firmware-update"[^>]*>/)?.[0];
    assert.ok(button);
    assert.doesNotMatch(button, /disabled/);
  }
  assert.equal(compareFirmwareVersions("v0.49.1", "v0.49.1-dev.780"), 1);
});

test("channel transition requires the selected dev channel and a known dev target", () => {
  for (const target of ["", "onbekend", "v0.49.1"]) {
    setMainToDevState(target);
    assert.equal(isFirmwareChannelTransition(), false);
    assert.equal(isFirmwareUpdateAvailable(), false);
  }
  setMainToDevState();
  state.entities.firmwareUpdateChannel = { state: "main" };
  assert.equal(isFirmwareChannelTransition(), false);
  assert.equal(isFirmwareUpdateAvailable(), false);

  setMainToDevState();
  delete state.entities.releaseChannelText;
  assert.equal(isFirmwareChannelTransition(), false);
  assert.equal(isFirmwareUpdateAvailable(), false);

  setMainToDevState();
  state.entities.projectVersionText = { state: "onbekend" };
  assert.equal(isFirmwareChannelTransition(), false);
});

test("same-channel updates retain ordinary version ordering", () => {
  setMainToDevState();
  state.entities.projectVersionText = { state: "v0.49.1-dev.779" };
  state.entities.releaseChannelText = { state: "dev" };
  assert.equal(isFirmwareChannelTransition(), false);
  assert.equal(isFirmwareUpdateAvailable(), true);

  setMainToDevState("v0.49.1");
  state.entities.firmwareUpdateChannel = { state: "main" };
  state.entities.firmwareUpdate.release_url = "https://github.com/OpenQuatt/OpenQuatt/releases/tag/v0.49.1";
  assert.equal(isFirmwareUpdateAvailable(), false);
  assert.equal(getUpdateStatus(), "Actueel");
  assert.equal(getFirmwareUpdateVersions().latest, "—");
});

test("channel-switch completion fails closed until the exact dev build and channel are reported", () => {
  for (const setup of [setMainToDevState, setPrToDevState]) {
    setup();
    const target = state.entities.firmwareUpdate.latest_version;
    state.updateInstallMode = "channel-switch";
    state.updateInstallBusy = true;
    state.updateInstallTargetVersion = target;
    assert.equal(hasInstalledFirmwareTargetVersion(), false);
    assert.equal(hasInstalledFirmwareLatestVersion(), false);
    assert.equal(isFirmwareInstallSettled(), false);
    assert.equal(isFirmwareInstallCompletionConfirmed(), false);

    // New channel alone, stale/wrong version, or a different build hash is not success.
    state.entities.releaseChannelText = { state: "dev" };
    for (const version of ["onbekend", "v0.50.0", "v0.50.0-dev.999", target.replace(/\+.*$/, "+other")]) {
      state.entities.projectVersionText = { state: version };
      assert.equal(isFirmwareInstallCompletionConfirmed(), false);
    }
    state.entities.projectVersionText = { state: target };
    for (const channel of ["main", "onbekend", ""]) {
      state.entities.releaseChannelText = { state: channel };
      assert.equal(isFirmwareInstallCompletionConfirmed(), false);
    }
    state.entities.releaseChannelText = { state: "dev" };
    // Cached pre-reboot update entity must not override live build evidence.
    assert.equal(isFirmwareInstallCompletionConfirmed(), true);
    state.entities.firmwareUpdateStatus = { state: "Uploading" };
    assert.equal(isFirmwareInstallCompletionConfirmed(), false);
    delete state.entities.firmwareUpdateStatus;
  }
});

test("channel-switch action captures its target and never completes on a failed request", async (t) => {
  const originalFetch = globalThis.fetch;
  const originalLocation = window.location;
  t.after(() => { globalThis.fetch = originalFetch; window.location = originalLocation; setRenderCallback(null); });
  window.location = { pathname: "/" };
  setMainToDevState();
  const observed = [];
  setRenderCallback(() => {});
  globalThis.fetch = async () => {
    observed.push([state.updateInstallMode, state.updateInstallTargetVersion]);
    assert.equal(isFirmwareInstallCompletionConfirmed(), false);
    return { ok: false, status: 503 };
  };
  await installFirmwareUpdate();
  assert.deepEqual(observed, [["channel-switch", "v0.49.1-dev.780+abcdef0"]]);
  assert.equal(state.updateInstallCompleted, false);
  assert.equal(state.updateInstallBusy, false);
  assert.match(state.controlError, /503/);
});

test("channel-switch aborts if target or channel changes while the target write is pending", async (t) => {
  const originalFetch = globalThis.fetch;
  const originalLocation = window.location;
  t.after(() => { globalThis.fetch = originalFetch; window.location = originalLocation; setRenderCallback(null); });
  window.location = { pathname: "/" };
  setRenderCallback(() => {});
  for (const change of [
    () => { state.entities.firmwareUpdate.latest_version = "v0.49.1-dev.781+changed"; },
    () => { state.entities.firmwareUpdateChannel = { state: "main" }; },
  ]) {
    setMainToDevState();
    state.entities.firmwareUpdateTarget = { state: "current build" };
    let requests = 0;
    globalThis.fetch = async () => {
      requests += 1;
      change();
      return { ok: true };
    };
    await installFirmwareUpdate();
    assert.equal(requests, 1, "no install request after target/channel changes");
    assert.equal(state.updateInstallCompleted, false);
    assert.equal(state.updateInstallBusy, false);
    assert.match(state.controlError, /dev-doelversie is gewijzigd/);
  }
});

test("PR firmware starts with one complete render before the first device write", async (t) => {
  const originalFetch = globalThis.fetch;
  const originalLocation = window.location;
  const originalState = { ...state };
  t.after(() => {
    globalThis.fetch = originalFetch;
    if (originalLocation === undefined) {
      delete window.location;
    } else {
      window.location = originalLocation;
    }
    setRenderCallback(null);
    for (const key of Object.keys(state)) {
      if (!(key in originalState)) {
        delete state[key];
      }
    }
    Object.assign(state, originalState);
  });

  state.drafts = {};
  state.entities = {
    hardwareProfileText: { state: "heatpump_controller_q" },
    installationTopology: { state: "duo" },
    connectionText: { state: "wifi" },
    preferredConnection: { state: "Automatic", value: "Automatic", option: ["Automatic", "WiFi", "Ethernet"] },
    installFirmwareTestManifest: { state: "" },
    firmwareTestManifestUrl: { state: "" },
  };
  state.updateTestFirmwarePr = "528";
  state.updateTestFirmwareConfirmed = true;
  window.location = { pathname: "/" };

  const events = [];
  setRenderCallback(() => {
    events.push({
      type: "render",
      busy: state.updateInstallBusy,
      build: state.updateTestFirmwareBuild,
    });
  });

  globalThis.fetch = async () => {
    events.push({ type: "fetch" });
    return { ok: false, status: 503 };
  };

  const operation = installFirmwareTestUpdate();
  await operation;

  assert.deepEqual(events, [
    {
      type: "render",
      busy: true,
      build: "PR 528 · Heatpump Controller Q Duo",
    },
    { type: "fetch" },
    {
      type: "render",
      busy: false,
      build: "PR 528 · Heatpump Controller Q Duo",
    },
  ]);
});

test("dev firmware exposes an explicit confirmed downgrade to the older main release", () => {
  setDevToMainDowngradeState();

  assert.equal(isFirmwareDowngradeAvailable(), true);
  assert.equal(isFirmwareUpdateAvailable(), false);
  assert.deepEqual(getFirmwareUpdateVersions(), {
    current: "v0.48.0-dev.696+86f5997",
    latest: "v0.47.0",
  });
  assert.equal(getUpdateStatus(), "Downgrade beschikbaar");
  assert.match(getFirmwareModalCopy(), /bewust teruggaan naar main/);

  let modal = renderUpdateModal();
  let installButton = modal.match(/<button class="oq-helper-button[^"]*" type="button" data-oq-action="install-firmware-update"[^>]*>/)?.[0] || "";
  assert.match(modal, /oq-firmware-downgrade-callout/);
  assert.match(modal, /data-oq-firmware-downgrade-confirm="true"/);
  assert.match(modal, /Main v0\.47\.0 vervangt de nieuwere dev-build v0\.48\.0-dev\.696\+86f5997/);
  assert.match(installButton, /oq-helper-button--warning/);
  assert.match(installButton, /disabled/);

  state.firmwareDowngradeConfirmedVersion = "v0.47.0";
  modal = renderUpdateModal();
  installButton = modal.match(/<button class="oq-helper-button[^"]*" type="button" data-oq-action="install-firmware-update"[^>]*>/)?.[0] || "";
  assert.doesNotMatch(installButton, /disabled/);
  assert.match(modal, /Terug naar main v0\.47\.0/);
});

test("firmware preview starts from a consistent running dev build", async () => {
  const mockSource = await readFile(new URL("../js/mock-device.js", import.meta.url), "utf8");

  assert.match(
    mockSource,
    /setEntity\("text_sensor", "OpenQuatt Version", \{ state: MOCK_DEV_VERSION, value: MOCK_DEV_VERSION \}\);/
  );
  assert.match(
    mockSource,
    /current_version: MOCK_DEV_VERSION,\s+latest_version: MOCK_DEV_VERSION,/
  );
});

test("opening the firmware modal closes the interface panel before rendering", async () => {
  const actionsSource = await readFile(new URL("../js/src/features/firmware-actions.js", import.meta.url), "utf8");
  const handlerStart = actionsSource.indexOf('    "open-update-modal": () => {');
  const handlerEnd = actionsSource.indexOf('\n    "close-update-modal":', handlerStart);
  const handlerSource = actionsSource.slice(handlerStart, handlerEnd);

  assert.notEqual(handlerStart, -1);
  assert.notEqual(handlerEnd, -1);
  assert.match(handlerSource, /state\.interfacePanelOpen = false;/);
  assert.ok(handlerSource.indexOf("state.interfacePanelOpen = false;") < handlerSource.indexOf("render();"));
});

test("downgrade remains unavailable outside the validated dev-to-main path", () => {
  setDevToMainDowngradeState();

  delete state.entities.installFirmwareUpdateTarget;
  assert.equal(isFirmwareDowngradeAvailable(), false);

  state.entities.installFirmwareUpdateTarget = { state: "" };
  state.entities.releaseChannelText = { state: "main", value: "main" };
  assert.equal(isFirmwareDowngradeAvailable(), false);

  state.entities.releaseChannelText = { state: "dev", value: "dev" };
  state.entities.firmwareUpdateChannel = { state: "dev", value: "dev", option: ["main", "dev"] };
  assert.equal(isFirmwareDowngradeAvailable(), false);

  state.entities.firmwareUpdateChannel = { state: "main", value: "main", option: ["main", "dev"] };
  state.entities.firmwareUpdate.latest_version = "onbekend";
  assert.equal(isFirmwareDowngradeAvailable(), false);
});

test("downgrade completion requires the device to boot the exact lower target", () => {
  setDevToMainDowngradeState();
  state.updateInstallBusy = true;
  state.updateInstallMode = "downgrade";
  state.updateInstallTargetVersion = "v0.47.0";

  assert.equal(isFirmwareInstallCompletionConfirmed(), false);

  state.entities.projectVersionText = { state: "onbekend", value: "onbekend" };
  assert.equal(isFirmwareInstallCompletionConfirmed(), false);

  state.entities.projectVersionText = { state: "v0.47.0", value: "v0.47.0" };
  state.entities.releaseChannelText = { state: "main", value: "main" };
  state.entities.firmwareUpdate = {
    ...state.entities.firmwareUpdate,
    state: "NO UPDATE",
    value: "v0.47.0",
    current_version: "v0.47.0",
    latest_version: "v0.47.0",
  };
  state.entities.firmwareUpdateStatus = { state: "Idle", value: "Idle" };

  assert.equal(isFirmwareInstallCompletionConfirmed(), true);
});

test("up-to-date firmware is only presented as completed after an install attempt", () => {
  setDevToMainDowngradeState();
  state.entities.projectVersionText = { state: "v0.47.0", value: "v0.47.0" };
  state.entities.releaseChannelText = { state: "main", value: "main" };
  state.entities.firmwareUpdate = {
    ...state.entities.firmwareUpdate,
    state: "up_to_date",
    value: "up_to_date",
    current_version: "v0.47.0",
    latest_version: "v0.47.0",
  };

  assert.equal(isFirmwareUpdateJustCompleted(), false);
  assert.doesNotMatch(renderUpdateModal(), /Firmware-update afgerond/);

  state.updateInstallCompleted = true;
  state.updateInstallCompletedVersion = "v0.47.0";

  assert.equal(isFirmwareUpdateJustCompleted(), true);
  assert.match(renderUpdateModal(), /Firmware-update afgerond/);
});

test("a new OTA attempt ignores cached reboot progress until a post-start poll", () => {
  state.entities = {
    firmwareUpdate: {
      current_version: "v0.41.0",
      latest_version: "v0.42.0",
    },
    firmwareUpdateProgress: { state: 100, value: 100 },
    firmwareUpdateStatus: { state: "Rebooting", value: "Rebooting" },
    projectVersionText: { state: "v0.41.0", value: "v0.41.0" },
  };
  state.updateInstallBusy = true;
  state.updateInstallMode = "normal";
  state.updateInstallTargetVersion = "v0.42.0";

  primeFirmwareInstallProgressHints();

  assert.deepEqual(getFirmwareProgressModel(), {
    phaseLabel: "Installeren",
    percent: 0,
    copy: "OTA-update is gestart voor OpenQuatt.",
  });
  assert.equal(state.entities.firmwareUpdateStatus.state, "Rebooting");
  assert.equal(state.entities.firmwareUpdateProgress.state, 100);

  state.entities.firmwareUpdateStatus = { state: "Rebooting", value: "Rebooting" };
  state.entities.firmwareUpdateProgress = { state: 100, value: 100 };

  assert.equal(getFirmwareProgressModel().phaseLabel, "Installeren");
  assert.equal(getFirmwareProgressModel().percent, 0);

  state.entities.firmwareUpdateStatus = { state: "Rebooting", value: "Rebooting" };
  state.entities.firmwareUpdateProgress = { state: 100, value: 100 };
  state.updateInstallStatusPollObserved = true;

  assert.equal(getFirmwareProgressModel().phaseLabel, "Herstarten");
  assert.equal(getFirmwareProgressModel().percent, 100);

  primeFirmwareInstallProgressHints();

  state.entities.firmwareUpdateStatus = { state: "Uploading", value: "Uploading" };
  state.entities.firmwareUpdateProgress = { state: 23, value: 23 };
  state.updateInstallStatusPollObserved = true;

  assert.deepEqual(getFirmwareProgressModel(), {
    phaseLabel: "Uploaden",
    percent: 23,
    copy: "Firmware wordt nu naar OpenQuatt verzonden.",
  });

  state.entities.firmwareUpdateStatus = { state: "Rebooting", value: "Rebooting" };
  state.entities.firmwareUpdateProgress = { state: 100, value: 100 };

  assert.equal(getFirmwareProgressModel().phaseLabel, "Herstarten");
  assert.equal(getFirmwareProgressModel().percent, 100);
});

test("local OTA hints do not create optional progress entities", () => {
  state.entities = {
    firmwareUpdate: {
      current_version: "v0.41.0",
      latest_version: "v0.42.0",
    },
    projectVersionText: { state: "v0.41.0", value: "v0.41.0" },
  };
  state.updateInstallBusy = true;
  state.updateInstallMode = "normal";
  state.updateInstallTargetVersion = "v0.42.0";

  primeFirmwareInstallProgressHints();

  assert.equal(state.entities.firmwareUpdateStatus, undefined);
  assert.equal(state.entities.firmwareUpdateProgress, undefined);
  assert.equal(getFirmwareProgressModel().phaseLabel, "Installeren");
  assert.equal(getFirmwareProgressModel().percent, 0);
});

test("manual OTA does not complete while live progress remains active", () => {
  state.entities = {
    firmwareUpdate: {
      current_version: "v0.42.0",
      latest_version: "v0.42.0",
    },
    firmwareUpdateProgress: { state: 100, value: 100 },
    firmwareUpdateStatus: { state: "Rebooting", value: "Rebooting" },
    projectVersionText: { state: "v0.42.0", value: "v0.42.0" },
  };
  state.updateInstallBusy = true;
  state.updateInstallMode = "";
  state.updateInstallTargetVersion = "v0.42.0";
  state.updateInstallStatusPollObserved = true;

  assert.equal(isFirmwareInstallCompletionConfirmed(), false);
});

test("manual OTA does not complete from an unchanged version without progress evidence", () => {
  state.entities = {
    firmwareUpdate: {
      current_version: "v0.42.0",
      latest_version: "v0.42.0",
    },
    projectVersionText: { state: "v0.42.0", value: "v0.42.0" },
  };
  state.updateInstallBusy = true;
  state.updateInstallMode = "";
  state.updateInstallTargetVersion = "v0.42.0";
  state.ota.on = true;
  state.ota.id = {};
  state.ota.wait = true;

  assert.equal(isFirmwareInstallCompletionConfirmed(), false);
});

test("normal OTA completes after its target version is inactive and installed", () => {
  state.entities = {
    firmwareUpdate: {
      current_version: "v0.43.0",
      latest_version: "v0.43.0",
      state: "up_to_date",
    },
    firmwareUpdateStatus: { state: "Idle", value: "Idle" },
    projectVersionText: { state: "v0.43.0", value: "v0.43.0" },
  };
  state.updateInstallBusy = true;
  state.updateInstallMode = "normal";
  state.updateInstallTargetVersion = "v0.43.0";

  assert.equal(isFirmwareInstallCompletionConfirmed(), true);
});
