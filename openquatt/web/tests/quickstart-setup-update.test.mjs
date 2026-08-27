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
  clearQuickStartSetupInstall,
  getStoredQuickStartSetupInstall,
  hasCompletedQuickStartSetupInstallFor,
  restoreStoredQuickStartSetupInstall,
  state,
  storeQuickStartSetupInstall,
} = await import("../js/src/core/state.js");
const {
  getFirmwareBuildSwitchModel,
  getFirmwareProgressModel,
  hasKnownFirmwareTargetVersion,
  isFirmwareInstallCompletionConfirmed,
  isQuickStartSetupInstallCompletionConfirmed,
  isQuickStartSetupFirmwareCurrent,
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
  state.quickStartSetupUpdateComplete = false;
  state.complete = false;
  state.updateInstallBusy = false;
  state.updateInstallMode = "";
  state.updateInstallPhaseHint = "";
  state.updateInstallProgressHint = Number.NaN;
  state.updateInstallStatusPollObserved = false;
  state.updateInstallSuccessfulPhaseObserved = false;
  state.updateInstallResumedAfterReload = false;
  state.updateInstallTargetConnection = "";
  state.updateInstallTargetTopology = "";
  state.updateInstallTargetVersion = "";
  state.ota = { on: false, ok: 0, id: null, wait: false, base: null };
  state.controlError = "";
  state.controlNotice = "";
}

test("de actuele main-versie en configuratie hebben geen OTA nodig", () => {
  resetSetupState();
  storage.clear();

  const model = getFirmwareBuildSwitchModel("single", "wifi");
  assert.equal(model.targetOption, "current build");
  assert.equal(model.canSwitch, false);
  assert.equal(model.canInstall, true);
  assert.equal(hasKnownFirmwareTargetVersion(), true);
  assert.equal(isQuickStartSetupFirmwareCurrent(model), true);

  const changedModel = getFirmwareBuildSwitchModel("duo", "eth");
  assert.equal(changedModel.targetOption, "alternate topology and connection");
  assert.equal(changedModel.canSwitch, true);
  assert.equal(changedModel.canInstall, true);
  assert.equal(isQuickStartSetupFirmwareCurrent(changedModel), false);

  state.entities.firmwareUpdate.latest_version = "v0.49.0";
  assert.equal(isQuickStartSetupFirmwareCurrent(model), false);

  state.entities.firmwareUpdate.latest_version = "v0.48.0";
  state.entities.releaseChannelText = textEntity("dev");
  assert.equal(isQuickStartSetupFirmwareCurrent(model), false);

  resetSetupState();
  delete state.entities.installFirmwareUpdateTarget;
  assert.equal(getFirmwareBuildSwitchModel("single", "wifi").canInstall, false);

  resetSetupState();
  delete state.entities.firmwareUpdateChannel;
  assert.equal(getFirmwareBuildSwitchModel("single", "wifi").canInstall, false);
});

test("Quick Start staat de expliciet bevestigde overstap van dev naar main toe", () => {
  resetSetupState();
  state.entities.firmwareUpdate.current_version = "v0.49.0";
  state.entities.firmwareUpdate.latest_version = "v0.48.0";
  state.entities.firmwareUpdate.state = "available";
  state.entities.projectVersionText = textEntity("v0.49.0");
  state.entities.releaseChannelText = textEntity("dev");

  const model = getFirmwareBuildSwitchModel("single", "wifi");
  assert.equal(model.downgradeAvailable, true);
  assert.equal(model.mainChannelAvailable, true);
  assert.equal(model.canInstall, true);
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
  state.updateInstallSuccessfulPhaseObserved = true;
  assert.equal(isQuickStartSetupInstallCompletionConfirmed(), false);

  state.entities.firmwareUpdateStatus = textEntity("Idle");
  state.entities.releaseChannelText = textEntity("dev");
  assert.equal(isQuickStartSetupInstallCompletionConfirmed(), false);

  state.entities.releaseChannelText = textEntity("main");
  assert.equal(isQuickStartSetupInstallCompletionConfirmed(), true);
  assert.equal(isFirmwareInstallCompletionConfirmed(), true);

  state.entities.connectionText = textEntity("eth");
  assert.equal(isQuickStartSetupInstallCompletionConfirmed(), false);
  assert.equal(isFirmwareInstallCompletionConfirmed(), false);
});

test("de wizard bewaart een lopende en afgeronde Quick Start-update in de sessie", () => {
  storage.clear();
  const pending = {
    status: "pending",
    targetTopology: "duo",
    targetConnection: "eth",
    targetChannel: "main",
    targetVersion: "v0.49.0",
    startedAt: 1234,
  };
  storeQuickStartSetupInstall(pending);
  assert.deepEqual(getStoredQuickStartSetupInstall(), pending);

  resetSetupState();
  restoreStoredQuickStartSetupInstall();
  assert.equal(state.updateInstallMode, "quickstart-setup");
  assert.equal(state.updateInstallTargetTopology, "duo");
  assert.equal(state.updateInstallTargetConnection, "eth");
  assert.equal(state.updateInstallResumedAfterReload, true);

  storeQuickStartSetupInstall({ ...pending, status: "complete" });
  assert.equal(getStoredQuickStartSetupInstall().status, "complete");
  assert.equal(hasCompletedQuickStartSetupInstallFor("duo", "eth"), true);
  assert.equal(hasCompletedQuickStartSetupInstallFor("single", "eth"), false);
  clearQuickStartSetupInstall();
  assert.equal(getStoredQuickStartSetupInstall(), null);

  storage.set("oq-quickstart-setup-install", JSON.stringify({
    ...pending,
    targetChannel: undefined,
  }));
  assert.equal(getStoredQuickStartSetupInstall(), null);
});

test("een hervatte update vereist een waargenomen succesvolle OTA-fase", () => {
  resetSetupState();
  state.updateInstallMode = "quickstart-setup";
  state.updateInstallTargetTopology = "single";
  state.updateInstallTargetConnection = "wifi";
  state.updateInstallTargetVersion = "v0.48.0";
  state.updateInstallResumedAfterReload = true;

  assert.equal(isQuickStartSetupInstallCompletionConfirmed(), false);
  state.updateInstallSuccessfulPhaseObserved = true;
  assert.equal(isQuickStartSetupInstallCompletionConfirmed(), true);

  state.updateInstallTargetVersion = "v0.49.0";
  assert.equal(isQuickStartSetupInstallCompletionConfirmed(), false);
});

test("een hervatte update accepteert een duurzame overgang naar het OTA-doel", () => {
  resetSetupState();
  storage.clear();
  storeQuickStartSetupInstall({
    status: "pending",
    targetTopology: "single",
    targetConnection: "wifi",
    targetChannel: "main",
    targetVersion: "v0.48.0",
    startedAt: Date.now(),
    sourceTopology: "single",
    sourceConnection: "wifi",
    sourceChannel: "dev",
    sourceVersion: "v0.48.0",
  });
  restoreStoredQuickStartSetupInstall();

  state.entities.releaseChannelText = textEntity("dev");
  assert.equal(isQuickStartSetupInstallCompletionConfirmed(), false);

  state.entities.releaseChannelText = textEntity("main");
  assert.equal(state.updateInstallSuccessfulPhaseObserved, false);
  assert.equal(isQuickStartSetupInstallCompletionConfirmed(), true);
});

test("een hervatte lopende update houdt de configuratiekeuze geblokkeerd", () => {
  resetSetupState();
  storage.clear();
  storeQuickStartSetupInstall({
    status: "pending",
    targetTopology: "single",
    targetConnection: "wifi",
    targetChannel: "main",
    targetVersion: "v0.48.0",
    startedAt: Date.now(),
  });
  restoreStoredQuickStartSetupInstall();

  const progress = getFirmwareProgressModel();
  assert.equal(progress.phaseLabel, "Installeren");
  assert.match(progress.copy, /Single.*Wi-Fi/i);
});

test("Quick Start-voortgang noemt de volledige doelbuild", () => {
  resetSetupState();
  storage.clear();
  state.updateInstallMode = "quickstart-setup";
  state.updateInstallBusy = true;
  state.updateInstallTargetTopology = "single";
  state.updateInstallTargetConnection = "eth";
  state.entities.firmwareUpdateStatus = textEntity("Starting");

  assert.match(getFirmwareProgressModel().copy, /Single.*Ethernet/i);
});

test("de Quick Start-actie controleert current build en blokkeert vervolgstappen", async () => {
  const [actionsSource, viewSource, uiActionsSource] = await Promise.all([
    readFile(new URL("../js/src/features/firmware-actions.js", import.meta.url), "utf8"),
    readFile(new URL("../js/src/features/quickstart.js", import.meta.url), "utf8"),
    readFile(new URL("../js/src/features/quickstart-ui-actions.js", import.meta.url), "utf8"),
  ]);
  const start = actionsSource.indexOf("export async function installQuickStartSetupSwitch()");
  const end = actionsSource.indexOf("\n  export async function setFirmwareTestTextEntity", start);
  const action = actionsSource.slice(start, end);
  const installStart = actionsSource.indexOf("async function installQuickStartSetupFirmware(model)");
  const installEnd = actionsSource.indexOf("\n  export async function installQuickStartSetupSwitch()", installStart);
  const installAction = actionsSource.slice(installStart, installEnd);

  assert.notEqual(start, -1);
  assert.notEqual(end, -1);
  assert.notEqual(installStart, -1);
  assert.notEqual(installEnd, -1);
  assert.doesNotMatch(action, /targetOption === "current build"/);
  assert.match(action, /await installQuickStartSetupFirmware\(model\);/);
  assert.match(viewSource, /Configuratie en software-update/);
  assert.match(viewSource, /data-oq-quickstart-setup-confirm="true"/);
  assert.match(viewSource, /Configuratie bevestigen en software bijwerken/);
  assert.match(viewSource, /Configuratie bevestigen/);
  assert.match(viewSource, /Nieuwste main-versie/);
  assert.match(viewSource, /dev- of testbuild/);
  assert.match(viewSource, /selectionAllowed \? "" : "disabled"/);
  assert.match(uiActionsSource, /isQuickStartStepSelectionAllowed\(stepId\)/);
  assert.match(uiActionsSource, /hasCompletedQuickStartSetupInstallFor\(targetTopology, targetConnection\)/);
  assert.match(actionsSource, /setQuickStartFirmwareUpdateChannelMain\(\)/);
  assert.match(actionsSource, /isFirmwareEntityAlignedWithChannel\(getFirmwareUpdateEntity\(\) \|\| \{\}, "main"\)/);
  assert.ok(
    installAction.indexOf("await setQuickStartFirmwareUpdateChannelMain()")
      < installAction.indexOf("await setFirmwareUpdateTarget(model.targetOption"),
  );
  assert.ok(
    installAction.indexOf("if (isQuickStartSetupFirmwareCurrent(model))")
      < installAction.indexOf('status: "pending"'),
  );
  assert.ok(
    installAction.indexOf("if (isQuickStartSetupFirmwareCurrent(model))")
      < installAction.indexOf("beginFirmwareOtaQuietWindow()"),
  );
  assert.match(actionsSource, /storeQuickStartSetupInstall\(/);
  assert.match(actionsSource, /targetChannel: "main"/);
  assert.match(actionsSource, /sourceChannel,/);
  assert.match(viewSource, /state\.complete === true/);
  assert.doesNotMatch(viewSource, /model\.changes \? `[\s\S]*data-oq-action="install-quickstart-setup"/);
});
