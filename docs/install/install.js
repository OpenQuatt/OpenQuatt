const FACTORY_ROOT = new URL("../firmware/main/", window.location.href).toString().replace(/\/$/, "");
const VERSION_URL = new URL("../firmware/main/version.json", window.location.href).toString();
const FACTORY_FILES_URL = new URL("../firmware/main/factory_files.json", window.location.href).toString();
const FALLBACK_RELEASE_URL = "https://github.com/OpenQuatt/OpenQuatt/releases/latest";
const FALLBACK_DEVICE_URL = "http://openquatt.local";

const HARDWARE_LABELS = {
  waveshare: "Waveshare ESP32-S3-Relay-1CH",
  heatpump_listener: "Electropaultje Heatpump Listener",
  heatpump_controller_q: "Electropaultje Heatpump Controller Q-edition",
};

const CONNECTION_LABELS = {
  auto: "Automatisch",
  wifi: "Wi-Fi",
  eth: "Ethernet",
};

const PROFILES = {
  duo: {
    label: "Duo",
    waveshare: {
      wifi: {
        title: "OpenQuatt Duo / Waveshare / Wi-Fi",
        chipFamily: "ESP32-S3",
        hardwareLabel: HARDWARE_LABELS.waveshare,
        connectionLabel: CONNECTION_LABELS.wifi,
        fileName: "openquatt-waveshare-duo-wifi.firmware.factory.bin",
        legacyFileNames: ["openquatt-duo-waveshare.firmware.factory.bin"],
        releaseFallback: true,
      },
    },
    heatpump_listener: {
      wifi: {
        title: "OpenQuatt Duo / Heatpump Listener / Wi-Fi",
        chipFamily: "ESP32",
        hardwareLabel: HARDWARE_LABELS.heatpump_listener,
        connectionLabel: CONNECTION_LABELS.wifi,
        fileName: "openquatt-heatpump-listener-duo-wifi.firmware.factory.bin",
        legacyFileNames: ["openquatt-duo-heatpump-listener.firmware.factory.bin"],
        releaseFallback: true,
      },
    },
    heatpump_controller_q: {
      auto: {
        title: "OpenQuatt Duo / Heatpump Controller Q / Automatisch",
        chipFamily: "ESP32-S3",
        hardwareLabel: HARDWARE_LABELS.heatpump_controller_q,
        connectionLabel: CONNECTION_LABELS.auto,
        fileName: "openquatt-heatpump-controller-q-duo.firmware.factory.bin",
        releaseFallback: true,
        wifiProvisioning: true,
      },
    },
  },
  single: {
    label: "Single",
    waveshare: {
      wifi: {
        title: "OpenQuatt Single / Waveshare / Wi-Fi",
        chipFamily: "ESP32-S3",
        hardwareLabel: HARDWARE_LABELS.waveshare,
        connectionLabel: CONNECTION_LABELS.wifi,
        fileName: "openquatt-waveshare-single-wifi.firmware.factory.bin",
        legacyFileNames: ["openquatt-single-waveshare.firmware.factory.bin"],
        releaseFallback: true,
      },
    },
    heatpump_listener: {
      wifi: {
        title: "OpenQuatt Single / Heatpump Listener / Wi-Fi",
        chipFamily: "ESP32",
        hardwareLabel: HARDWARE_LABELS.heatpump_listener,
        connectionLabel: CONNECTION_LABELS.wifi,
        fileName: "openquatt-heatpump-listener-single-wifi.firmware.factory.bin",
        legacyFileNames: ["openquatt-single-heatpump-listener.firmware.factory.bin"],
        releaseFallback: true,
      },
    },
    heatpump_controller_q: {
      auto: {
        title: "OpenQuatt Single / Heatpump Controller Q / Automatisch",
        chipFamily: "ESP32-S3",
        hardwareLabel: HARDWARE_LABELS.heatpump_controller_q,
        connectionLabel: CONNECTION_LABELS.auto,
        fileName: "openquatt-heatpump-controller-q-single.firmware.factory.bin",
        releaseFallback: true,
        wifiProvisioning: true,
      },
    },
  },
};

const selectionTitle = document.getElementById("selection-title");
const selectionCopy = document.getElementById("selection-copy");
const selectionVersion = document.getElementById("selection-version");
const selectionTopology = document.getElementById("selection-topology");
const selectionHardware = document.getElementById("selection-hardware");
const selectionConnection = document.getElementById("selection-connection");
const selectionChip = document.getElementById("selection-chip");
const selectionFile = document.getElementById("selection-file");
const releaseLink = document.getElementById("release-link");
const installPanel = document.getElementById("install-panel");
const installState = document.getElementById("install-state");
const installButton = document.getElementById("install-button");
const connectionStep = document.getElementById("connection-step");
const provisionPanel = document.getElementById("wifi-provision-panel");
const provisionState = document.getElementById("wifi-provision-state");
const provisionDevice = document.getElementById("wifi-provision-device");
const provisionConnectButton = document.getElementById("wifi-provision-connect");
const provisionDisconnectButton = document.getElementById("wifi-provision-disconnect");
const provisionForm = document.getElementById("wifi-provision-form");
const provisionSsidInput = document.getElementById("wifi-provision-ssid");
const provisionPasswordInput = document.getElementById("wifi-provision-password");
const provisionSubmitButton = document.getElementById("wifi-provision-submit");
const provisionOpenDeviceLink = document.getElementById("wifi-provision-open-device");
const installRouteTabs = Array.from(document.querySelectorAll("[data-install-route-tab]"));
const installRoutePanels = Array.from(document.querySelectorAll("[data-install-route-panel]"));

const IMPROV_PACKET = {
  CURRENT_STATE: 1,
  ERROR_STATE: 2,
  RPC: 3,
  RPC_RESULT: 4,
};

const IMPROV_COMMAND = {
  WIFI_SETTINGS: 1,
  GET_CURRENT_STATE: 2,
  GET_DEVICE_INFO: 3,
};

const IMPROV_STATE = {
  READY: 2,
  PROVISIONING: 3,
  PROVISIONED: 4,
};

const IMPROV_ERROR_MESSAGES = {
  1: "Ongeldig Improv-pakket ontvangen.",
  2: "De module herkende het Improv-commando niet.",
  3: "Verbinden met dit Wi-Fi-netwerk is niet gelukt.",
  254: "Wi-Fi instellen duurde te lang.",
  255: "Onbekende Improv-fout.",
};

const IMPROV_HEADER = [73, 77, 80, 82, 79, 86, 1];
const IMPROV_INIT_TIMEOUT_MS = 12000;
const IMPROV_PROVISION_TIMEOUT_MS = 30000;

class ImprovSerialClient {
  constructor(port, onDisconnect, onStateChange) {
    this.port = port;
    this.onDisconnect = onDisconnect;
    this.onStateChange = onStateChange;
    this.reader = null;
    this.pendingRpc = null;
    this.pendingState = null;
    this.readLoopClosed = false;
    this.state = null;
    this.error = 0;
    this.info = null;
    this.nextUrl = "";
  }

  async connect() {
    await this.port.open({ baudRate: 115200, bufferSize: 8192 });
    this.startReadLoop();
    await delay(1000);

    const currentStateUrls = await this.requestCurrentState(IMPROV_INIT_TIMEOUT_MS);
    this.nextUrl = getProvisionUrl(currentStateUrls[0]);
    const info = await this.rpc(IMPROV_COMMAND.GET_DEVICE_INFO, [], { timeoutMs: IMPROV_INIT_TIMEOUT_MS });
    this.info = {
      firmware: info[0] || "OpenQuatt",
      version: info[1] || "",
      chipFamily: info[2] || "",
      name: info[3] || "",
    };
  }

  async close() {
    this.rejectPending(new Error("USB-verbinding verbroken."));
    this.rejectPendingState(new Error("USB-verbinding verbroken."));
    if (this.reader) {
      try {
        await this.reader.cancel();
      } catch (error) {
        console.debug("Kon Improv-reader niet annuleren", error);
      }
    }
    if (this.port?.readable || this.port?.writable) {
      try {
        await this.port.close();
      } catch (error) {
        console.debug("Kon seriele poort niet sluiten", error);
      }
    }
  }

  async provision(ssid, password, options = {}) {
    const encoder = new TextEncoder();
    const ssidBytes = Array.from(encoder.encode(ssid));
    const passwordBytes = Array.from(encoder.encode(password));
    const payload = [ssidBytes.length, ...ssidBytes, passwordBytes.length, ...passwordBytes];
    const urls = await this.rpc(IMPROV_COMMAND.WIFI_SETTINGS, payload, {
      timeoutMs: IMPROV_PROVISION_TIMEOUT_MS,
      afterWrite: options.afterWrite,
    });
    this.nextUrl = getProvisionUrl(urls[0] || this.nextUrl);
    return this.nextUrl;
  }

  startReadLoop() {
    this.readLoopClosed = false;
    this.readLoop().catch((error) => {
      if (!this.readLoopClosed) {
        console.debug("Improv read loop gestopt", error);
        this.rejectPending(error);
        this.onDisconnect?.();
      }
    });
  }

  async readLoop() {
    this.reader = this.port.readable.getReader();
    let packet = [];
    let expectedLength = 0;
    let skippingLogLine = false;

    try {
      while (true) {
        const { value, done } = await this.reader.read();
        if (done) {
          break;
        }
        if (!value) {
          continue;
        }

        for (const byte of value) {
          if (skippingLogLine) {
            if (byte === 10) {
              skippingLogLine = false;
            }
            continue;
          }

          if (!expectedLength && packet.length < 6 && (byte === 10 || byte === 13)) {
            packet = [];
            continue;
          }

          packet.push(byte);

          if (packet.length === 6 && String.fromCharCode(...packet) !== "IMPROV") {
            packet = [];
            expectedLength = 0;
            skippingLogLine = true;
            continue;
          }

          if (packet.length === 9) {
            expectedLength = 10 + packet[8];
          }

          if (expectedLength && packet.length === expectedLength) {
            this.handlePacket(packet);
            packet = [];
            expectedLength = 0;
          }
        }
      }
    } finally {
      this.readLoopClosed = true;
      this.reader?.releaseLock();
      this.reader = null;
    }
  }

  handlePacket(packet) {
    const version = packet[6];
    const type = packet[7];
    const dataLength = packet[8];
    const data = packet.slice(9, 9 + dataLength);
    const checksum = packet[9 + dataLength];
    const expectedChecksum = packet.slice(0, packet.length - 1).reduce((sum, byte) => (sum + byte) & 255, 0);

    if (version !== 1 || checksum !== expectedChecksum) {
      return;
    }

    if (type === IMPROV_PACKET.CURRENT_STATE) {
      this.state = data[0];
      this.onStateChange?.(data[0]);
      this.resolvePendingState(data[0]);
      return;
    }

    if (type === IMPROV_PACKET.ERROR_STATE) {
      this.error = data[0];
      if (this.error > 0) {
        this.rejectPending(new Error(IMPROV_ERROR_MESSAGES[this.error] || `Improv-fout ${this.error}.`));
      }
      return;
    }

    if (type === IMPROV_PACKET.RPC_RESULT) {
      this.handleRpcResult(data);
    }
  }

  handleRpcResult(data) {
    if (!this.pendingRpc) {
      return;
    }

    const command = data[0];
    if (command !== this.pendingRpc.command) {
      return;
    }

    const strings = parseImprovStrings(data.slice(1));
    if (this.pendingRpc.multiple) {
      if (strings.length) {
        this.pendingRpc.results.push(strings);
        return;
      }
      this.resolvePending(this.pendingRpc.results);
      return;
    }

    this.resolvePending(strings);
  }

  async rpc(command, payload, options = {}) {
    if (this.pendingRpc) {
      throw new Error("Er loopt al een Improv-actie.");
    }

    const timeoutMs = options.timeoutMs || IMPROV_INIT_TIMEOUT_MS;
    const response = new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.rejectPending(new Error("Geen Improv-antwoord ontvangen."));
      }, timeoutMs);
      this.pendingRpc = {
        command,
        multiple: Boolean(options.multiple),
        results: [],
        resolve,
        reject,
        timer,
      };
    });

    try {
      await this.writePacket(IMPROV_PACKET.RPC, [command, payload.length, ...payload]);
      options.afterWrite?.();
    } catch (error) {
      this.rejectPending(error);
      throw error;
    }
    return response;
  }

  async requestCurrentState(timeoutMs) {
    if (this.pendingRpc || this.pendingState) {
      throw new Error("Er loopt al een Improv-actie.");
    }

    const stateResponse = new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.rejectPendingState(new Error("Geen Improv-status ontvangen."));
      }, timeoutMs);
      this.pendingState = { resolve, reject, timer };
    });

    const urlResponse = new Promise((resolve, reject) => {
      const timer = window.setTimeout(() => {
        this.rejectPending(new Error("Geen Improv-antwoord ontvangen."));
      }, timeoutMs);
      this.pendingRpc = {
        command: IMPROV_COMMAND.GET_CURRENT_STATE,
        multiple: false,
        results: [],
        resolve,
        reject,
        timer,
      };
    });
    urlResponse.catch(() => {});

    try {
      await this.writePacket(IMPROV_PACKET.RPC, [IMPROV_COMMAND.GET_CURRENT_STATE, 0]);
      const state = await stateResponse;
      if (state !== IMPROV_STATE.PROVISIONED) {
        this.resolvePending([]);
        return [];
      }
      return await urlResponse;
    } catch (error) {
      this.rejectPending(error);
      this.rejectPendingState(error);
      throw error;
    }
  }

  async writePacket(type, data) {
    const packet = new Uint8Array([...IMPROV_HEADER, type, data.length, ...data, 0, 10]);
    packet[packet.length - 2] = packet.slice(0, packet.length - 2).reduce((sum, byte) => (sum + byte) & 255, 0);
    const writer = this.port.writable.getWriter();
    try {
      await writer.write(packet);
    } finally {
      writer.releaseLock();
    }
  }

  resolvePending(value) {
    if (!this.pendingRpc) {
      return;
    }
    window.clearTimeout(this.pendingRpc.timer);
    this.pendingRpc.resolve(value);
    this.pendingRpc = null;
  }

  rejectPending(error) {
    if (!this.pendingRpc) {
      return;
    }
    window.clearTimeout(this.pendingRpc.timer);
    this.pendingRpc.reject(error);
    this.pendingRpc = null;
  }

  resolvePendingState(value) {
    if (!this.pendingState) {
      return;
    }
    window.clearTimeout(this.pendingState.timer);
    this.pendingState.resolve(value);
    this.pendingState = null;
  }

  rejectPendingState(error) {
    if (!this.pendingState) {
      return;
    }
    window.clearTimeout(this.pendingState.timer);
    this.pendingState.reject(error);
    this.pendingState = null;
  }
}

let activeManifestUrl;
let availableFactoryFiles;
let provisionClient = null;
let releaseInfo = {
  version: "latest",
  releaseUrl: FALLBACK_RELEASE_URL,
};

function delay(ms) {
  return new Promise((resolve) => window.setTimeout(resolve, ms));
}

function parseImprovStrings(data) {
  const decoder = new TextDecoder();
  const stringCount = data[0] || 0;
  const strings = [];
  let offset = 1;

  for (let index = 0; index < stringCount; index += 1) {
    const length = data[offset] || 0;
    const start = offset + 1;
    const end = start + length;
    strings.push(decoder.decode(new Uint8Array(data.slice(start, end))));
    offset = end;
  }

  return strings;
}

function getProvisionUrl(url) {
  if (!url) {
    return FALLBACK_DEVICE_URL;
  }

  try {
    const parsedUrl = new URL(url, window.location.href);
    if (parsedUrl.hostname === "0.0.0.0" || parsedUrl.hostname === "::" || parsedUrl.hostname === "[::]") {
      return FALLBACK_DEVICE_URL;
    }
    if (parsedUrl.protocol !== "http:" && parsedUrl.protocol !== "https:") {
      return FALLBACK_DEVICE_URL;
    }
    return parsedUrl.toString();
  } catch (error) {
    return FALLBACK_DEVICE_URL;
  }
}

function setProvisionState(message, state = "") {
  provisionState.textContent = message;
  if (state) {
    provisionPanel.dataset.state = state;
  } else {
    delete provisionPanel.dataset.state;
  }
}

function setProvisionBusy(busy) {
  provisionConnectButton.disabled = busy;
  provisionDisconnectButton.disabled = busy;
  provisionSubmitButton.disabled = busy;
}

function setProvisionFormVisible(visible) {
  provisionForm.hidden = !visible;
  provisionDisconnectButton.hidden = !visible;
}

function setProvisionOpenLink(url) {
  provisionOpenDeviceLink.href = getProvisionUrl(url);
  provisionOpenDeviceLink.hidden = false;
}

function updateProvisionDeviceInfo() {
  if (!provisionClient?.info) {
    provisionDevice.hidden = true;
    provisionDevice.textContent = "";
    return;
  }

  const parts = [
    provisionClient.info.name || "OpenQuatt",
    provisionClient.info.firmware,
    provisionClient.info.version,
    provisionClient.info.chipFamily,
  ].filter(Boolean);
  provisionDevice.textContent = parts.join(" / ");
  provisionDevice.hidden = false;
}

function describeProvisionReadyState() {
  if (provisionClient?.state === IMPROV_STATE.PROVISIONED) {
    return "Module is al verbonden. Je kunt Wi-Fi wijzigen of direct OpenQuatt openen.";
  }
  if (provisionClient?.state === IMPROV_STATE.PROVISIONING) {
    return "Module is bezig met Wi-Fi verbinden.";
  }
  return "Kies je Wi-Fi-netwerk en sla de gegevens op de module op.";
}

function handleProvisionStateChange(state) {
  if (!provisionClient || provisionForm.hidden || !provisionSubmitButton.disabled) {
    return;
  }

  if (state === IMPROV_STATE.PROVISIONING) {
    setProvisionState("Wi-Fi-gegevens verzonden. De module probeert nu te verbinden; dit kan tot 30 seconden duren.");
  }
  if (state === IMPROV_STATE.PROVISIONED) {
    setProvisionState("Wi-Fi is verbonden. Bevestiging ophalen...");
  }
}

function getProvisionSubmitErrorMessage(error) {
  if (error.message === "Geen Improv-antwoord ontvangen.") {
    return "Geen bevestiging ontvangen. Controleer SSID en wachtwoord; als de module toch online kwam, open OpenQuatt.";
  }
  return error.message || "Verbinden met dit Wi-Fi-netwerk is niet gelukt.";
}

async function disconnectProvisionClient(message = "USB-verbinding verbroken.") {
  const client = provisionClient;
  provisionClient = null;
  if (client) {
    await client.close();
  }

  setProvisionFormVisible(false);
  provisionDevice.hidden = true;
  provisionOpenDeviceLink.hidden = true;
  provisionConnectButton.textContent = "Configureer Wi-Fi";
  setProvisionBusy(false);
  setProvisionState(message, "warning");
}

async function startProvisionFlow() {
  if (!("serial" in navigator)) {
    setProvisionState("Web Serial is in deze browser niet beschikbaar. Gebruik Chrome of Edge op desktop.", "error");
    return;
  }
  if (!window.isSecureContext) {
    setProvisionState("Open deze installatiehulp via HTTPS om Web Serial te kunnen gebruiken.", "error");
    return;
  }

  setProvisionBusy(true);
  provisionOpenDeviceLink.hidden = true;
  setProvisionState("Kies de USB-poort van je OpenQuatt-module...");

  try {
    if (provisionClient) {
      await disconnectProvisionClient("Nieuwe USB-poort kiezen.");
      setProvisionBusy(true);
    }

    const port = await navigator.serial.requestPort();
    const client = new ImprovSerialClient(
      port,
      () => {
        disconnectProvisionClient("USB-verbinding verbroken.");
      },
      handleProvisionStateChange,
    );
    provisionClient = client;

    setProvisionState("Verbinden met de module...");
    await client.connect();
    updateProvisionDeviceInfo();
    setProvisionFormVisible(true);
    provisionConnectButton.textContent = "Andere USB-poort";
    setProvisionOpenLink(client.nextUrl);
    setProvisionState(describeProvisionReadyState(), client.state === IMPROV_STATE.PROVISIONED ? "ok" : "");
  } catch (error) {
    if (error.name === "NotFoundError") {
      setProvisionState("Geen USB-poort gekozen.", "warning");
    } else {
      console.debug("Wi-Fi provisioning kon niet starten", error);
      setProvisionState(error.message || "Wi-Fi configureren kon niet starten.", "error");
    }
    if (provisionClient) {
      const client = provisionClient;
      provisionClient = null;
      await client.close();
      setProvisionFormVisible(false);
      provisionDevice.hidden = true;
      provisionOpenDeviceLink.hidden = true;
      provisionConnectButton.textContent = "Configureer Wi-Fi";
    }
  } finally {
    setProvisionBusy(false);
  }
}

async function submitProvisionForm(event) {
  event.preventDefault();
  if (!provisionClient) {
    setProvisionState("Verbind eerst de module via USB.", "warning");
    return;
  }

  const ssid = provisionSsidInput.value.trim();
  if (!ssid) {
    setProvisionState("Vul een Wi-Fi-netwerknaam in.", "warning");
    return;
  }

  setProvisionBusy(true);
  setProvisionState("Wi-Fi-gegevens verzenden...");
  try {
    const nextUrl = await provisionClient.provision(ssid, provisionPasswordInput.value, {
      afterWrite: () => {
        setProvisionState(
          "Wi-Fi-gegevens verzonden. De module probeert nu te verbinden; dit kan tot 30 seconden duren.",
        );
      },
    });
    setProvisionOpenLink(nextUrl);
    setProvisionState("Wi-Fi is ingesteld. Open daarna OpenQuatt en rond de Quick Start af.", "ok");
  } catch (error) {
    console.debug("Wi-Fi provisioning mislukt", error);
    setProvisionState(getProvisionSubmitErrorMessage(error), "error");
  } finally {
    setProvisionBusy(false);
  }
}

function setInstallRoute(route) {
  installRouteTabs.forEach((tab) => {
    const active = tab.dataset.installRouteTab === route;
    tab.setAttribute("aria-selected", active ? "true" : "false");
    tab.tabIndex = active ? 0 : -1;
  });
  installRoutePanels.forEach((panel) => {
    panel.hidden = panel.dataset.installRoutePanel !== route;
  });
}

function focusAdjacentInstallRoute(currentTab, direction) {
  const currentIndex = installRouteTabs.indexOf(currentTab);
  if (currentIndex < 0) {
    return;
  }

  const nextIndex = (currentIndex + direction + installRouteTabs.length) % installRouteTabs.length;
  const nextTab = installRouteTabs[nextIndex];
  setInstallRoute(nextTab.dataset.installRouteTab);
  nextTab.focus();
}

function getStableVersionLabel() {
  return releaseInfo.version === "latest" ? "Nieuwste stabiele" : releaseInfo.version;
}

function getSelectedValues() {
  const topology = document.querySelector('input[name="topology"]:checked')?.value || "";
  const hardware = document.querySelector('input[name="hardware"]:checked')?.value || "";
  return {
    topology,
    hardware,
    connection: hardware === "heatpump_controller_q"
      ? "auto"
      : document.querySelector('input[name="connection"]:checked')?.value || "",
  };
}

function getProfile(topology, hardware, connection) {
  return PROFILES[topology]?.[hardware]?.[connection] || null;
}

function getSelectedProfile() {
  const { topology, hardware, connection } = getSelectedValues();
  const profile = getProfile(topology, hardware, connection);

  if (!topology || !hardware || !connection || !profile) {
    return null;
  }

  return {
    topology,
    hardware,
    connection,
    ...profile,
  };
}

function getProfileFileCandidates(profile) {
  return [profile.fileName, ...(profile.legacyFileNames || [])];
}

function resolveFactoryFile(profile) {
  const candidates = getProfileFileCandidates(profile);
  if (!availableFactoryFiles) {
    return profile.releaseFallback ? candidates[0] : "";
  }
  return candidates.find((fileName) => availableFactoryFiles.has(fileName)) || "";
}

function isProfileAvailable(profile) {
  return Boolean(profile && resolveFactoryFile(profile));
}

function buildManifest(profile) {
  const factoryFile = resolveFactoryFile(profile);
  const manifest = {
    name: profile.title,
    version: releaseInfo.version,
    new_install_prompt_erase: true,
    builds: [
      {
        chipFamily: profile.chipFamily,
        parts: [
          {
            path: `${FACTORY_ROOT}/${factoryFile}`,
            offset: 0,
          },
        ],
      },
    ],
  };

  manifest.new_install_improv_wait_time = profile.wifiProvisioning || profile.connection === "wifi" ? 30 : 0;

  return manifest;
}

function updateInstallManifest(profile) {
  if (activeManifestUrl) {
    URL.revokeObjectURL(activeManifestUrl);
  }

  const manifest = buildManifest(profile);
  activeManifestUrl = URL.createObjectURL(
    new Blob([JSON.stringify(manifest)], { type: "application/json" }),
  );

  installButton.manifest = activeManifestUrl;
}

function updateEmptySummary() {
  const { topology, hardware, connection } = getSelectedValues();
  const stableVersionLabel = getStableVersionLabel();

  selectionTitle.textContent = "Kies eerst je opstelling, module en verbinding";
  selectionCopy.textContent =
    "Deze installatiehulp maakt automatisch een eenmalig ESP Web Tools-manifest voor het profiel dat je hier kiest.";
  selectionVersion.textContent = stableVersionLabel;
  selectionTopology.textContent = topology ? PROFILES[topology]?.label || topology : "Niet gekozen";
  selectionHardware.textContent = hardware ? HARDWARE_LABELS[hardware] || hardware : "Niet gekozen";
  selectionConnection.textContent = connection ? CONNECTION_LABELS[connection] || connection : "Niet gekozen";
  selectionChip.textContent = "Niet gekozen";
  selectionFile.textContent = "Niet gekozen";
  installPanel.dataset.ready = "false";
  installState.textContent = "Kies opstelling, module en verbinding om de installatieknop te activeren.";
  installButton.manifest = "";
}

function getReadyStateText(profile) {
  if (profile.connection === "auto") {
    return "Klaar om te flashen. Laat Ethernet tijdens de eerste start los en stel Wi-Fi via USB in. Sluit daarna de kabel aan en herstart; zonder opgeslagen Wi-Fi-gegevens is er geen Wi-Fi-fallback.";
  }
  if (profile.connection === "eth") {
    return "Klaar om te flashen. Sluit na de herstart Ethernet aan en open daarna http://openquatt.local voor de Quick Start.";
  }

  return "Klaar om te flashen. Laat deze pagina na de herstart open zodat ESP Web Tools Wi-Fi-instelling via USB kan aanbieden. Open daarna http://openquatt.local voor de Quick Start.";
}

function updateSummary() {
  const profile = getSelectedProfile();
  const stableVersionLabel = getStableVersionLabel();

  releaseLink.href = releaseInfo.releaseUrl;

  if (!profile) {
    updateEmptySummary();
    return;
  }

  const factoryFile = resolveFactoryFile(profile);
  if (!factoryFile) {
    selectionTitle.textContent = `${profile.title} is nog niet beschikbaar`;
    selectionCopy.textContent =
      "Deze keuze staat al in de installer, maar de nieuwste stabiele release bevat hiervoor nog geen factory-image.";
    selectionVersion.textContent = stableVersionLabel;
    selectionTopology.textContent = PROFILES[profile.topology].label;
    selectionHardware.textContent = profile.hardwareLabel;
    selectionConnection.textContent = profile.connectionLabel;
    selectionChip.textContent = profile.chipFamily;
    selectionFile.textContent = "Nog niet gepubliceerd";
    installPanel.dataset.ready = "false";
    installState.textContent =
      "Kies een profiel waarvoor al een stabiel factory-image beschikbaar is, of wacht op de volgende release.";
    installButton.manifest = "";
    return;
  }

  updateInstallManifest(profile);

  selectionTitle.textContent = profile.title;
  selectionCopy.textContent = profile.connection === "auto"
    ? `Deze Q-build bevat Wi-Fi en Ethernet. Ethernet krijgt bij opstart voorrang; stel Wi-Fi vooraf in om bij kabeluitval te kunnen terugvallen. Stabiele versie: ${stableVersionLabel}.`
    : `Deze installatiehulp wijst naar de stabiele versie ${stableVersionLabel} die voor dit profiel op deze site beschikbaar is.`;
  selectionVersion.textContent = stableVersionLabel;
  selectionTopology.textContent = PROFILES[profile.topology].label;
  selectionHardware.textContent = profile.hardwareLabel;
  selectionConnection.textContent = profile.connectionLabel;
  selectionChip.textContent = profile.chipFamily;
  selectionFile.textContent = factoryFile;
  installPanel.dataset.ready = "true";
  installState.textContent = getReadyStateText(profile);
}

document.querySelectorAll('input[name="topology"], input[name="hardware"], input[name="connection"]').forEach((input) => {
  input.addEventListener("change", () => {
    updateAvailability();
    updateSummary();
  });
});

provisionConnectButton.addEventListener("click", startProvisionFlow);
provisionDisconnectButton.addEventListener("click", () => {
  disconnectProvisionClient("USB-verbinding verbroken.");
});
provisionForm.addEventListener("submit", submitProvisionForm);
installRouteTabs.forEach((tab) => {
  tab.addEventListener("click", () => {
    setInstallRoute(tab.dataset.installRouteTab);
  });
  tab.addEventListener("keydown", (event) => {
    if (event.key === "ArrowLeft" || event.key === "ArrowUp") {
      event.preventDefault();
      focusAdjacentInstallRoute(tab, -1);
    }
    if (event.key === "ArrowRight" || event.key === "ArrowDown") {
      event.preventDefault();
      focusAdjacentInstallRoute(tab, 1);
    }
    if (event.key === "Home") {
      event.preventDefault();
      setInstallRoute(installRouteTabs[0].dataset.installRouteTab);
      installRouteTabs[0].focus();
    }
    if (event.key === "End") {
      event.preventDefault();
      const lastTab = installRouteTabs[installRouteTabs.length - 1];
      setInstallRoute(lastTab.dataset.installRouteTab);
      lastTab.focus();
    }
  });
});

function getHardwareProfiles(topology, hardware) {
  if (topology) {
    return Object.values(PROFILES[topology]?.[hardware] || {});
  }

  return Object.values(PROFILES).flatMap((topologyProfiles) => Object.values(topologyProfiles[hardware] || {}));
}

function updateHardwareAvailability() {
  const { topology } = getSelectedValues();
  const hardwareOptions = document.querySelectorAll('input[name="hardware"]');

  hardwareOptions.forEach((input) => {
    const profiles = getHardwareProfiles(topology, input.value);
    const available = profiles.some((profile) => isProfileAvailable(profile));
    input.disabled = !available;
    input.closest(".choice-card")?.toggleAttribute("data-unavailable", !available);
    if (!available && input.checked) {
      input.checked = false;
    }
  });
}

function updateConnectionAvailability() {
  const { topology, hardware } = getSelectedValues();
  const connectionOptions = document.querySelectorAll('input[name="connection"]');
  const automaticQProfile = topology && hardware === "heatpump_controller_q"
    ? getProfile(topology, hardware, "auto")
    : null;

  connectionStep.hidden = Boolean(automaticQProfile);

  connectionOptions.forEach((input) => {
    const profile = topology && hardware ? getProfile(topology, hardware, input.value) : null;
    const available = isProfileAvailable(profile);
    input.disabled = !available;
    input.closest(".choice-card")?.toggleAttribute("data-unavailable", !available);
    if (!available && input.checked) {
      input.checked = false;
    }
  });
}

function updateAvailability() {
  updateHardwareAvailability();
  updateConnectionAvailability();
}

async function loadReleaseInfo() {
  try {
    const [versionResponse, factoryFilesResponse] = await Promise.all([
      fetch(VERSION_URL, { cache: "no-cache" }),
      fetch(FACTORY_FILES_URL, { cache: "no-cache" }),
    ]);
    if (!versionResponse.ok) {
      throw new Error(`version HTTP ${versionResponse.status}`);
    }

    const metadata = await versionResponse.json();
    if (factoryFilesResponse.ok) {
      const factoryMetadata = await factoryFilesResponse.json();
      if (Array.isArray(factoryMetadata.factory_files)) {
        availableFactoryFiles = new Set(factoryMetadata.factory_files);
      }
    }

    if (typeof metadata.version === "string" && metadata.version) {
      releaseInfo.version = metadata.version;
    }
    if (typeof metadata.release_url === "string" && metadata.release_url) {
      releaseInfo.releaseUrl = metadata.release_url;
    }
  } catch (error) {
    console.warn("Kon de gespiegelde release-metadata niet laden", error);
  } finally {
    updateAvailability();
    updateSummary();
  }
}

window.addEventListener("beforeunload", () => {
  if (activeManifestUrl) {
    URL.revokeObjectURL(activeManifestUrl);
  }
  if (provisionClient) {
    provisionClient.close().catch((error) => console.debug("Kon Improv-client niet sluiten", error));
  }
});

updateSummary();
updateAvailability();
setInstallRoute("preinstalled");
loadReleaseInfo();

if (!("serial" in navigator)) {
  provisionConnectButton.disabled = true;
  setProvisionState("Web Serial is in deze browser niet beschikbaar. Gebruik Chrome of Edge op desktop.", "error");
} else if (!window.isSecureContext) {
  provisionConnectButton.disabled = true;
  setProvisionState("Open deze installatiehulp via HTTPS om Web Serial te kunnen gebruiken.", "error");
}
