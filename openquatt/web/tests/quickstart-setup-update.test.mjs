import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
const storage = new Map();
globalThis.window = {
  clearTimeout,
  location: { pathname: "/" },
  localStorage: {
    getItem: () => null,
  },
  sessionStorage: {
    getItem: (key) => storage.get(key) ?? null,
    removeItem: (key) => storage.delete(key),
    setItem: (key, value) => storage.set(key, String(value)),
  },
  setTimeout,
};

const {
  consumeStoredQuickStartResumeStep,
  state,
  storeQuickStartResumeStep,
} = await import("../js/src/core/state.js");
const {
  getFirmwareBuildSwitchModel,
  isFirmwareInstallCompletionConfirmed,
  isQuickStartSetupInstallCompletionConfirmed,
} = await import("../js/src/features/firmware-update.js");

function textEntity(value) {
  return { state: value, value };
}

function resetSetupState() {
  state.entities = {
    connectionText: textEntity("wifi"),
    firmwareUpdate: {
      current_version: "v0.48.0",
      latest_version: "v0.48.0",
      release_url: "https://github.com/OpenQuatt/OpenQuatt/releases/tag/v0.48.0",
      state: "up_to_date",
      title: "Heatpump Controller Q Single Wi-Fi",
    },
    firmwareUpdateChannel: { ...textEntity("main"), option: ["main", "dev"] },
    firmwareUpdateProgress: textEntity(0),
    firmwareUpdateStatus: textEntity("Idle"),
    firmwareUpdateTarget: {
      ...textEntity("current build"),
      option: ["current build", "alternate connection", "alternate topology", "alternate topology and connection"],
    },
    hardwareProfileText: textEntity("heatpump_controller_q"),
    installFirmwareUpdateTarget: textEntity(""),
    installationTopology: textEntity("single"),
    projectVersionText: textEntity("v0.48.0"),
    releaseChannelText: textEntity("main"),
  };
  state.quickStartSetupDraft = "";
  state.quickStartSetupConfirmed = false;
  state.updateInstallBusy = false;
  state.updateInstallMode = "";
  state.updateInstallPhaseHint = "";
  state.updateInstallProgressHint = Number.NaN;
  state.updateInstallStatusPollObserved = false;
  state.updateInstallTargetConnection = "";
  state.updateInstallTargetTopology = "";
  state.updateInstallTargetVersion = "";
  state.ota = { on: false, ok: 0, id: null, wait: false, base: null };
  state.controlError = "";
  state.controlNotice = "";
}

test("de actieve configuratie kan als software-update opnieuw worden geïnstalleerd", () => {
  resetSetupState();

  const model = getFirmwareBuildSwitchModel("single", "wifi");
  assert.equal(model.targetOption, "current build");
  assert.equal(model.canSwitch, false);
  assert.equal(model.canInstall, true);

  const changedModel = getFirmwareBuildSwitchModel("duo", "eth");
  assert.equal(changedModel.targetOption, "alternate topology and connection");
  assert.equal(changedModel.canSwitch, true);
  assert.equal(changedModel.canInstall, true);

  delete state.entities.installFirmwareUpdateTarget;
  assert.equal(getFirmwareBuildSwitchModel("single", "wifi").canInstall, false);
});

test("een Quick Start-installatie vereist doelconfiguratie én bewezen reboot", () => {
  resetSetupState();
  state.updateInstallMode = "quickstart-setup";
  state.updateInstallTargetTopology = "single";
  state.updateInstallTargetConnection = "wifi";
  state.ota = { on: true, ok: 1, id: null, wait: true, base: [3600000, "v0.48.0", 0] };
  state.updateInstallTargetVersion = "v0.48.0";

  assert.equal(isQuickStartSetupInstallCompletionConfirmed(), false);
  assert.equal(isFirmwareInstallCompletionConfirmed(), false);

  state.ota.id = {};
  state.ota.wait = false;
  state.entities.firmwareUpdateStatus = textEntity("Uploading");
  assert.equal(isQuickStartSetupInstallCompletionConfirmed(), false);

  state.entities.firmwareUpdateStatus = textEntity("Idle");
  assert.equal(isQuickStartSetupInstallCompletionConfirmed(), true);
  assert.equal(isFirmwareInstallCompletionConfirmed(), true);

  state.entities.connectionText = textEntity("eth");
  assert.equal(isQuickStartSetupInstallCompletionConfirmed(), false);
  assert.equal(isFirmwareInstallCompletionConfirmed(), false);
});

test("de wizard hervat na de bevestigde OTA eenmalig bij de volgende stap", () => {
  storage.clear();
  storeQuickStartResumeStep("generation");

  assert.equal(consumeStoredQuickStartResumeStep(), "generation");
  assert.equal(consumeStoredQuickStartResumeStep(), "setup");

  storeQuickStartResumeStep("confirm");
  assert.equal(consumeStoredQuickStartResumeStep(), "setup");
});

test("de Quick Start-actie slaat current build niet meer over", async () => {
  const [actionsSource, viewSource] = await Promise.all([
    readFile(new URL("../js/src/features/firmware-actions.js", import.meta.url), "utf8"),
    readFile(new URL("../js/src/features/quickstart.js", import.meta.url), "utf8"),
  ]);
  const start = actionsSource.indexOf("export async function installQuickStartSetupSwitch()");
  const end = actionsSource.indexOf("\n  export async function setFirmwareTestTextEntity", start);
  const action = actionsSource.slice(start, end);

  assert.notEqual(start, -1);
  assert.notEqual(end, -1);
  assert.doesNotMatch(action, /targetOption === "current build"/);
  assert.match(action, /await installQuickStartSetupFirmware\(model\);/);
  assert.match(viewSource, /Configuratie en software-update/);
  assert.match(viewSource, /data-oq-quickstart-setup-confirm="true"/);
  assert.match(viewSource, /Configuratie bevestigen en software bijwerken/);
  assert.doesNotMatch(viewSource, /model\.changes \? `[\s\S]*data-oq-action="install-quickstart-setup"/);
});
