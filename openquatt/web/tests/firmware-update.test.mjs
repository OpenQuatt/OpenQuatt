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
const { installFirmwareTestUpdate } = await import("../js/src/features/firmware-actions.js");
const {
  getFirmwareModalCopy,
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

test("PR firmware uses deterministic release URLs without the GitHub REST API", () => {
  const target = {
    available: true,
    label: "Heatpump Controller Q Duo Wi-Fi",
    otaFileName: "openquatt-heatpump-controller-q-duo-wifi.firmware.ota.bin",
  };

  assert.deepEqual(getFirmwareTestAssetUrls(395, target), {
    otaUrl: "https://github.com/OpenQuatt/OpenQuatt/releases/download/pr-395/openquatt-heatpump-controller-q-duo-wifi.firmware.ota.bin",
    md5Url: "https://github.com/OpenQuatt/OpenQuatt/releases/download/pr-395/openquatt-heatpump-controller-q-duo-wifi.firmware.ota.bin.md5",
    label: "PR 395 · Heatpump Controller Q Duo Wi-Fi",
  });
  assert.equal(getFirmwareTestAssetUrls("395/../../dev-latest", target), null);
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
    installFirmwareTestOta: { state: "" },
    firmwareTestOtaUrl: { state: "" },
    firmwareTestOtaMd5Url: { state: "" },
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
      build: "PR 528 · Heatpump Controller Q Duo Wi-Fi",
    },
    { type: "fetch" },
    {
      type: "render",
      busy: false,
      build: "PR 528 · Heatpump Controller Q Duo Wi-Fi",
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
