import { hasEntity } from "../core/app-shared.js";
import { TOPOLOGY_HINT_KEYS } from "../core/config.js";
import { getEntityValue, isDeviceTimeValid } from "../core/entity-store.js";
import { formatDurationFromMinutes } from "../core/formatting.js";
import { state } from "../core/state.js";

  export function getDeviceMeta() {
    const meta = __OQ_PREVIEW__ && typeof window !== "undefined" && window.__OQ_DEV_META && typeof window.__OQ_DEV_META === "object"
      ? window.__OQ_DEV_META
      : {};
    return meta;
  }

  export function getHybridGenerationLabel() {
    const generation = String(getEntityValue("hpGeneration") || "").trim();
    if (generation) {
      return generation;
    }
    return "";
  }

  export function normalizeInstallationTopologyLabel(value) {
    const normalized = String(value || "").trim().toLowerCase();
    if (normalized === "single" || normalized.includes("quatt single") || normalized.includes("openquatt single")) {
      return "single";
    }
    if (normalized === "duo" || normalized.includes("quatt duo") || normalized.includes("openquatt duo")) {
      return "duo";
    }
    return "";
  }

  export function inferInstallationTopologyFromEntities() {
    if (!Array.isArray(TOPOLOGY_HINT_KEYS)) {
      return "";
    }
    if (TOPOLOGY_HINT_KEYS.some((key) => hasEntity(key))) {
      return "duo";
    }
    const missingHints = state.optionalMissingEntities || {};
    return TOPOLOGY_HINT_KEYS.every((key) => missingHints[key]) ? "single" : "";
  }

  export function rememberInstallationTopology(topology) {
    const normalized = normalizeInstallationTopologyLabel(topology);
    if ((normalized === "single" || normalized === "duo") && typeof state !== "undefined" && state && typeof state === "object") {
      state.lastKnownInstallationTopology = normalized;
    }
    return normalized;
  }

  export function getCachedInstallationTopology() {
    if (typeof state !== "undefined" && state && typeof state === "object") {
      const cached = String(state.lastKnownInstallationTopology || "").trim().toLowerCase();
      if (cached === "single" || cached === "duo") {
        return cached;
      }
    }
    return "";
  }

  export function getInstallationTopology() {
    const entityTopology = normalizeInstallationTopologyLabel(getEntityValue("installationTopology"));
    if (entityTopology === "single" || entityTopology === "duo") {
      return rememberInstallationTopology(entityTopology);
    }

    const metaTopology = normalizeInstallationTopologyLabel(getDeviceMeta().installation);
    if (metaTopology === "single" || metaTopology === "duo") {
      return rememberInstallationTopology(metaTopology);
    }

    const inferredTopology = inferInstallationTopologyFromEntities();
    if (inferredTopology) {
      return rememberInstallationTopology(inferredTopology);
    }

    return getCachedInstallationTopology();
  }

  export function getInstallationLabel() {
    const installation = getInstallationTopology();
    const generation = getHybridGenerationLabel();
    if (installation === "single") {
      return generation ? `Quatt Single ${generation}` : "Quatt Single";
    }
    if (installation === "duo") {
      return generation ? `Quatt Duo ${generation}` : "Quatt Duo";
    }
    return generation ? `Quatt Hybrid ${generation}` : "Quatt Hybrid";
  }

  export function getFirmwareDeviceLabel() {
    return "OpenQuatt";
  }

  export function normalizeFirmwareConnection(value) {
    const normalized = String(value || "").trim().toLowerCase();
    if (normalized === "wifi" || normalized === "wi-fi" || normalized.includes("wifi") || normalized.includes("wi-fi")) {
      return "wifi";
    }
    if (normalized === "eth" || normalized === "ethernet" || normalized.includes("ethernet")) {
      return "eth";
    }
    return "";
  }

  export function getFirmwareConnectionLabel(connection = getFirmwareBuildConnection()) {
    if (connection === "wifi") {
      return "Wi-Fi";
    }
    if (connection === "eth") {
      return "Ethernet";
    }
    return "Onbekend";
  }

  export function getFirmwareTopologyLabel(topology = getInstallationTopology()) {
    if (topology === "single") {
      return "Single";
    }
    if (topology === "duo") {
      return "Duo";
    }
    return "Onbekende opstelling";
  }

  export function getFirmwareHardwareProfile() {
    const entityProfile = String(getEntityValue("hardwareProfileText") || "").trim().toLowerCase();
    if (entityProfile && entityProfile !== "unknown" && entityProfile !== "onbekend") {
      return entityProfile;
    }
    return String(getDeviceMeta().hardwareProfile || entityProfile).trim().toLowerCase();
  }

  export function getFirmwareBuildConnection() {
    return normalizeFirmwareConnection(getEntityValue("preferredConnection"))
      || normalizeFirmwareConnection(getEntityValue("connectionText"))
      || normalizeFirmwareConnection(getDeviceMeta().connection);
  }

  export function getFirmwareAlternateConnection() {
    const current = getFirmwareBuildConnection();
    if (current === "wifi") {
      return "eth";
    }
    if (current === "eth") {
      return "wifi";
    }
    return "";
  }

  export function getFirmwareAlternateTopology() {
    const current = getInstallationTopology();
    if (current === "single") {
      return "duo";
    }
    if (current === "duo") {
      return "single";
    }
    return "";
  }

  export function getFirmwareBuildLabelFor(topology = getInstallationTopology(), connection = getFirmwareBuildConnection()) {
    const topologyLabel = getFirmwareTopologyLabel(topology);
    const hardware = getFirmwareHardwareProfile();
    if (hardware === "heatpump_controller_q") {
      if (hasEntity("preferredConnection")) {
        return `Heatpump Controller Q ${topologyLabel}`;
      }
      return `Heatpump Controller Q ${topologyLabel} ${getFirmwareConnectionLabel(connection)}`;
    }
    if (hardware === "heatpump_listener") {
      return `Heatpump Listener ${topologyLabel} ${getFirmwareConnectionLabel(connection)}`;
    }
    if (hardware === "waveshare") {
      return `Waveshare ${topologyLabel} ${getFirmwareConnectionLabel(connection)}`;
    }
    return `${getFirmwareDeviceLabel()} ${topologyLabel} ${getFirmwareConnectionLabel(connection)}`;
  }

  export function formatDeviceClock() {
    const deviceClock = String(getEntityValue("timeNowHhmm") || "").trim();
    if (isDeviceTimeValid(deviceClock)) {
      return deviceClock;
    }
    if (hasEntity("timeNowHhmm")) {
      return "Geen tijdsync";
    }
    try {
      return new Intl.DateTimeFormat("nl-NL", {
        hour: "2-digit",
        minute: "2-digit",
      }).format(new Date());
    } catch (_error) {
      return new Date().toLocaleTimeString("nl-NL", { hour: "2-digit", minute: "2-digit" });
    }
  }

  export function formatDiagnosticsDateTime() {
    if (hasEntity("timeNowHhmm") && !isDeviceTimeValid(getEntityValue("timeNowHhmm"))) {
      return "Geen tijdsync";
    }

    const datePart = new Intl.DateTimeFormat("nl-NL", {
      day: "numeric",
      month: "short",
      year: "numeric",
    }).format(new Date());
    return `${datePart} · ${formatDeviceClock()}`;
  }

  export { formatDurationFromMinutes };

  export function getNumericEntityUnit(entity) {
    return String(entity?.uom ?? entity?.unit_of_measurement ?? "").trim().toLowerCase();
  }

  export function getNumericEntityValue(entity) {
    const rawState = entity?.state;
    if (rawState !== "" && rawState !== null && rawState !== undefined) {
      const numericState = Number(rawState);
      if (Number.isFinite(numericState)) {
        return numericState;
      }
    }
    const rawValue = entity?.value;
    const numericValue = Number(rawValue);
    return Number.isFinite(numericValue) ? numericValue : NaN;
  }

  export function formatUptimeFromMeta() {
    const uptimeValue = getNumericEntityValue(state.entities.uptime);
    if (Number.isFinite(uptimeValue) && uptimeValue >= 0) {
      const uptimeUnit = getNumericEntityUnit(state.entities.uptime);
      if (uptimeUnit === "d") {
        return formatDurationFromMinutes(uptimeValue * 1440);
      }
      if (uptimeUnit === "h") {
        return formatDurationFromMinutes(uptimeValue * 60);
      }
      if (uptimeUnit === "s") {
        return formatDurationFromMinutes(uptimeValue / 60);
      }
    }
    const uptimeText = String(
      state.entities.uptimeReadable?.state
      ?? state.entities.uptimeReadable?.value
      ?? ""
    ).trim();
    if (uptimeText && uptimeText.toLowerCase() !== "unknown") {
      return uptimeText;
    }
    const bootedAt = Number(getDeviceMeta().bootedAt);
    if (!Number.isFinite(bootedAt) || bootedAt <= 0) {
      return "—";
    }
    return formatDurationFromMinutes((Date.now() - bootedAt) / 60000);
  }

  export function getDeviceIpAddress() {
    const entityText = String(state.entities.ipAddress?.state ?? state.entities.ipAddress?.value ?? "").trim();
    if (entityText && entityText !== "0.0.0.0" && entityText !== "::") {
      return entityText;
    }
    const explicit = String(getDeviceMeta().ipAddress || "").trim();
    if (explicit && explicit !== "0.0.0.0" && explicit !== "::") {
      return explicit;
    }
    const host = typeof window !== "undefined" ? String(window.location.hostname || "").trim() : "";
    return host || "—";
  }
