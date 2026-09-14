import { copyTextToClipboard } from "../core/browser-utils.js";
import { invokeActionMap } from "../core/action-router.js";
import { updateMqttState } from "../core/feature-state.js";
import { getEntitySignatureFragment } from "../core/render-signatures.js";
import { state } from "../core/state.js";
import { shouldRefreshSupplementaryStatus } from "../core/supplementary-refresh.js";
import { isIntegrationsSettingsGroupActive } from "../core/surface-state.js";
import { getMqttInputTopic, isMqttInputAcceptRetained, isMqttInputEnabled } from "./mqtt.js";
import { render } from "../core/render-scheduler.js";

  export function getMqttStatusSignature(status = state.mqttStatus || {}) {
    const inputTopics = status.input_topics && typeof status.input_topics === "object"
      ? status.input_topics
      : {};
    const inputEnabled = status.input_enabled && typeof status.input_enabled === "object"
      ? status.input_enabled
      : {};
    const inputRetained = status.input_retained && typeof status.input_retained === "object"
      ? status.input_retained
      : {};
    const inputAcceptRetained = status.input_accept_retained && typeof status.input_accept_retained === "object"
      ? status.input_accept_retained
      : {};
    return [
      status.enabled ? "on" : "off",
      status.connected ? "connected" : "idle",
      String(status.broker || ""),
      String(status.port || ""),
      String(status.username || ""),
      status.password_set ? "password" : "nopassword",
      String(status.dew_point_topic || ""),
      JSON.stringify(inputTopics),
      JSON.stringify(inputEnabled),
      JSON.stringify(inputRetained),
      JSON.stringify(inputAcceptRetained),
      String(status.non_retained_stateful_timeout_s || ""),
      String(status.source || ""),
      String(status.csrf_token || ""),
    ].join(":");
  }

  export function getMqttSensorsModalRenderSignature() {
    return [
      state.systemModal,
      state.mqttExpandedTopicKey || "",
      state.mqttCopiedTopicKey || "",
      state.mqttInputToggleBusyKey || "",
      state.mqttRetainedToggleBusyKey || "",
      state.mqttError || "",
      getMqttStatusSignature(),
      getEntitySignatureFragment("mqttCoolingDewPoint"),
      getEntitySignatureFragment("mqttCoolingDewPointAge"),
      getEntitySignatureFragment("mqttCoolingDewPointValid"),
      getEntitySignatureFragment("mqttOutsideTemperature"),
      getEntitySignatureFragment("mqttOutsideTemperatureAge"),
      getEntitySignatureFragment("mqttOutsideTemperatureValid"),
      getEntitySignatureFragment("mqttRoomTemperature"),
      getEntitySignatureFragment("mqttRoomTemperatureAge"),
      getEntitySignatureFragment("mqttRoomTemperatureValid"),
      getEntitySignatureFragment("mqttRoomSetpoint"),
      getEntitySignatureFragment("mqttRoomSetpointAge"),
      getEntitySignatureFragment("mqttRoomSetpointValid"),
      getEntitySignatureFragment("mqttHeatingEnable"),
      getEntitySignatureFragment("mqttHeatingEnableAge"),
      getEntitySignatureFragment("mqttHeatingEnableValid"),
      getEntitySignatureFragment("mqttCoolingEnable"),
      getEntitySignatureFragment("mqttCoolingEnableAge"),
      getEntitySignatureFragment("mqttCoolingEnableValid"),
    ].join("|");
  }

  export function syncMqttDraftsFromStatus() {
    const status = state.mqttStatus || {};
    updateMqttState({
      mqttDraftEnabled: status.enabled === true,
      mqttDraftBroker: String(status.broker || ""),
      mqttDraftPort: String(status.port || 1883),
      mqttDraftUsername: String(status.username || ""),
      mqttDraftPassword: "",
      mqttDraftClearPassword: false,
      mqttDraftDirty: false,
    });
  }

  export function syncMqttDraftFromInput(input) {
    const mqttField = input?.dataset?.oqMqttField;
    if (!mqttField) {
      return false;
    }

    updateMqttState({ mqttNotice: "", mqttError: "", mqttDraftDirty: true });
    if (mqttField === "enabled") {
      state.mqttDraftEnabled = Boolean(input.checked);
    } else if (mqttField === "broker") {
      state.mqttDraftBroker = String(input.value || "");
    } else if (mqttField === "port") {
      state.mqttDraftPort = String(input.value || "");
    } else if (mqttField === "username") {
      state.mqttDraftUsername = String(input.value || "");
    } else if (mqttField === "password") {
      state.mqttDraftPassword = String(input.value || "");
    } else if (mqttField === "clear-password") {
      state.mqttDraftClearPassword = Boolean(input.checked);
      if (state.mqttDraftClearPassword) {
        state.mqttDraftPassword = "";
      }
      const passwordInput = input.closest(".oq-helper-modal")?.querySelector('[data-oq-mqtt-field="password"]');
      if (passwordInput) {
        passwordInput.value = state.mqttDraftPassword;
        passwordInput.disabled = state.mqttBusy || state.mqttDraftClearPassword;
      }
    }

    input.closest(".oq-helper-modal")?.querySelectorAll(".oq-helper-modal-success, .oq-helper-modal-note--error").forEach((node) => {
      node.remove();
    });
    return true;
  }

  export function shouldRefreshMqttStatusForCurrentSurface() {
    return state.systemModal === "mqtt" || state.systemModal === "mqtt-sensors" || isIntegrationsSettingsGroupActive();
  }

  export async function refreshMqttStatus(options = {}) {
    if (!shouldRefreshSupplementaryStatus(state.lastMqttStatusRefreshAt, options)) {
      return false;
    }
    state.lastMqttStatusRefreshAt = Date.now();
    try {
      const response = await fetch("/mqtt/status", { cache: "no-store" });
      if (!response.ok) {
        throw new Error(`HTTP ${response.status}`);
      }
      const payload = await response.json();
      const rawInputTopics = payload.input_topics && typeof payload.input_topics === "object"
        ? payload.input_topics
        : {};
      const inputTopics = {};
      Object.entries(rawInputTopics).forEach(([key, value]) => {
        inputTopics[String(key)] = String(value || "");
      });
      const rawInputEnabled = payload.input_enabled && typeof payload.input_enabled === "object"
        ? payload.input_enabled
        : {};
      const inputEnabled = {};
      Object.entries(rawInputEnabled).forEach(([key, value]) => {
        inputEnabled[String(key)] = value !== false && String(value).toLowerCase() !== "false";
      });
      const rawInputRetained = payload.input_retained && typeof payload.input_retained === "object"
        ? payload.input_retained
        : {};
      const inputRetained = {};
      Object.entries(rawInputRetained).forEach(([key, value]) => {
        inputRetained[String(key)] = value === true || String(value).toLowerCase() === "true";
      });
      const rawInputAcceptRetained = payload.input_accept_retained && typeof payload.input_accept_retained === "object"
        ? payload.input_accept_retained
        : {};
      const inputAcceptRetained = {
        cooling_dew_point: false,
        outside_temperature: false,
        room_temperature: false,
        room_setpoint: true,
        heating_supply_target: false,
        heating_enable: true,
        cooling_enable: true,
      };
      Object.entries(rawInputAcceptRetained).forEach(([key, value]) => {
        inputAcceptRetained[String(key)] = value === true || String(value).toLowerCase() === "true";
      });
      const coolingDewPointTopic = String(inputTopics.cooling_dew_point || payload.dew_point_topic || "");
      inputTopics.cooling_dew_point = coolingDewPointTopic;
      const nextStatus = {
        enabled: Boolean(payload.enabled),
        connected: Boolean(payload.connected),
        broker: String(payload.broker || ""),
        port: Number(payload.port || 1883),
        username: String(payload.username || ""),
        password_set: Boolean(payload.password_set),
        dew_point_topic: coolingDewPointTopic,
        input_topics: inputTopics,
        input_enabled: inputEnabled,
        input_retained: inputRetained,
        input_accept_retained: inputAcceptRetained,
        non_retained_stateful_timeout_s: Number(payload.non_retained_stateful_timeout_s || 1800),
        source: String(payload.source || ""),
        csrf_token: String(payload.csrf_token || ""),
      };
      const previousSignature = getMqttStatusSignature();
      const nextSignature = getMqttStatusSignature(nextStatus);
      state.mqttStatus = nextStatus;
      if (previousSignature !== nextSignature) {
        if (!(state.systemModal === "mqtt" && state.mqttDraftDirty)) {
          syncMqttDraftsFromStatus();
        }
        state.mqttNotice = "";
      }
      state.mqttError = "";
      return previousSignature !== nextSignature;
    } catch (error) {
      state.mqttError = `MQTT-status kon niet worden geladen. ${error.message}`;
      return false;
    }
  }

  export async function copyMqttTopic(topicKey = "cooling_dew_point") {
    const topic = getMqttInputTopic(topicKey);
    if (!topic) {
      state.mqttError = "MQTT-topic is nog niet geladen.";
      state.mqttCopiedTopicKey = "";
      render();
      return;
    }
    try {
      const copied = await copyTextToClipboard(topic);
      state.mqttNotice = "";
      state.mqttError = copied ? "" : "Kopiëren is niet gelukt.";
      state.mqttCopiedTopicKey = copied ? topicKey : "";
      if (state.mqttCopiedTopicTimer) {
        window.clearTimeout(state.mqttCopiedTopicTimer);
      }
      if (copied) {
        state.mqttCopiedTopicTimer = window.setTimeout(() => {
          state.mqttCopiedTopicKey = "";
          state.mqttCopiedTopicTimer = null;
          if (state.systemModal === "mqtt-sensors") {
            render();
          }
        }, 1800);
      }
    } catch (error) {
      state.mqttError = `Kopiëren is mislukt. ${error.message}`;
      state.mqttCopiedTopicKey = "";
    }
    render();
  }

  export async function commitMqttInputEnabled(topicKey, enabled) {
    const status = state.mqttStatus || {};
    if (!status.csrf_token) {
      state.mqttError = "MQTT-status wordt nog geladen. Probeer het zo opnieuw.";
      render();
      return;
    }

    state.mqttInputToggleBusyKey = topicKey;
    state.mqttNotice = "";
    state.mqttError = "";
    render();

    try {
      const params = new URLSearchParams();
      params.set("csrf_token", status.csrf_token);
      params.set("input", topicKey);
      params.set("enabled", enabled ? "true" : "false");

      const response = await fetch("/mqtt/input/save", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
        },
        body: params,
      });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok || payload?.ok === false) {
        throw new Error(payload?.error || `HTTP ${response.status}`);
      }
      state.lastMqttStatusRefreshAt = 0;
      await refreshMqttStatus({ force: true });
    } catch (error) {
      state.mqttError = `MQTT-topic kon niet worden opgeslagen. ${error.message}`;
    } finally {
      if (state.mqttInputToggleBusyKey === topicKey) {
        state.mqttInputToggleBusyKey = "";
      }
      render();
    }
  }

  export async function commitMqttInputAcceptRetained(topicKey, acceptRetained) {
    const status = state.mqttStatus || {};
    if (!status.csrf_token) {
      state.mqttError = "MQTT-status wordt nog geladen. Probeer het zo opnieuw.";
      render();
      return;
    }

    state.mqttRetainedToggleBusyKey = topicKey;
    state.mqttNotice = "";
    state.mqttError = "";
    render();

    try {
      const params = new URLSearchParams();
      params.set("csrf_token", status.csrf_token);
      params.set("input", topicKey);
      params.set("accept_retained", acceptRetained ? "true" : "false");

      const response = await fetch("/mqtt/input/retained/save", {
        method: "POST",
        headers: {
          "Content-Type": "application/x-www-form-urlencoded",
        },
        body: params,
      });
      const payload = await response.json().catch(() => ({}));
      if (!response.ok || payload?.ok === false) {
        throw new Error(payload?.error || `HTTP ${response.status}`);
      }
      state.lastMqttStatusRefreshAt = 0;
      await refreshMqttStatus({ force: true });
    } catch (error) {
      state.mqttError = `Retained-instelling kon niet worden opgeslagen. ${error.message}`;
    } finally {
      if (state.mqttRetainedToggleBusyKey === topicKey) {
        state.mqttRetainedToggleBusyKey = "";
      }
      render();
    }
  }

  export async function commitMqttConfig() {
    const status = state.mqttStatus || {};
    const enabled = Boolean(state.mqttDraftEnabled);
    const broker = String(state.mqttDraftBroker || "").trim();
    const portText = String(state.mqttDraftPort || "").trim();
    const port = portText ? Number(portText) : (enabled ? 0 : 1883);
    const removeConfig = !enabled && !broker;
    const username = removeConfig ? "" : String(state.mqttDraftUsername || "").trim();
    const clearPassword = removeConfig || Boolean(state.mqttDraftClearPassword);
    const password = clearPassword ? "" : String(state.mqttDraftPassword || "");

    if (!status.csrf_token) {
      state.mqttError = "MQTT-configuratie laadt nog. Probeer het zo opnieuw.";
      render();
      return;
    }
    if ((enabled || portText) && (!Number.isInteger(port) || port < 1 || port > 65535)) {
      state.mqttError = "Vul een geldige poort in.";
      render();
      return;
    }
    if (enabled && !broker) {
      state.mqttError = "Vul een broker in als je MQTT inschakelt.";
      render();
      return;
    }

    state.mqttBusy = true;
    state.mqttNotice = "";
    state.mqttError = "";
    render();

    try {
      const params = new URLSearchParams();
      params.set("csrf_token", status.csrf_token);
      params.set("enabled", enabled ? "true" : "false");
      params.set("broker", broker);
      params.set("port", String(port));
      params.set("username", username);
      params.set("password", password);
      params.set("clear_password", clearPassword ? "true" : "false");

      const response = await fetch("/mqtt/save", {
        method: "POST",
        headers: { "Content-Type": "application/x-www-form-urlencoded;charset=UTF-8" },
        body: params.toString(),
      });
      const payload = await response.json().catch(() => ({ ok: false, error: "invalid_response" }));
      if (!response.ok || !payload.ok) {
        throw new Error(payload.error || `HTTP ${response.status}`);
      }
      state.mqttDraftDirty = false;
      await refreshMqttStatus({ force: true });
      state.mqttDraftPassword = "";
      state.mqttDraftClearPassword = false;
      state.mqttNotice = enabled
        ? "MQTT-configuratie opgeslagen. De MQTT-verbinding wordt gestart."
        : "MQTT-configuratie opgeslagen.";
      state.mqttError = "";
      render();
    } catch (error) {
      state.mqttError = `Opslaan is mislukt. ${error.message}`;
      render();
    } finally {
      state.mqttBusy = false;
      render();
    }
  }

  const mqttActionHandlers = {
    "open-mqtt-modal": () => {
      state.systemModal = "mqtt";
      syncMqttDraftsFromStatus();
      state.mqttDraftDirty = false;
      state.mqttNotice = "";
      state.mqttError = "";
      render();
      return refreshMqttStatus({ force: true });
    },
    "open-mqtt-sensors-modal": () => {
      state.systemModal = "mqtt-sensors";
      state.mqttNotice = "";
      state.mqttError = "";
      state.mqttCopiedTopicKey = "";
      state.mqttExpandedTopicKey = "";
      state.mqttInputToggleBusyKey = "";
      state.mqttRetainedToggleBusyKey = "";
      render();
      return refreshMqttStatus({ force: true }).then((changed) => {
        if (changed && state.systemModal === "mqtt-sensors") {
          render();
        }
      });
    },
    "toggle-mqtt-sensor-topic": (button) => {
      const topicKey = button.dataset?.oqMqttTopicKey || "cooling_dew_point";
      state.mqttExpandedTopicKey = state.mqttExpandedTopicKey === topicKey ? "" : topicKey;
      state.mqttError = "";
      render();
    },
    "toggle-mqtt-input": (button) => {
      const topicKey = button.dataset?.oqMqttTopicKey || "cooling_dew_point";
      return commitMqttInputEnabled(topicKey, !isMqttInputEnabled(topicKey));
    },
    "toggle-mqtt-retained": (button) => {
      const topicKey = button.dataset?.oqMqttTopicKey || "";
      if (topicKey) {
        return commitMqttInputAcceptRetained(topicKey, !isMqttInputAcceptRetained(topicKey));
      }
    },
    "copy-mqtt-topic": (button) => copyMqttTopic(button.dataset?.oqMqttTopicKey || "cooling_dew_point"),
    "copy-mqtt-dew-topic": (button) => copyMqttTopic(button.dataset?.oqMqttTopicKey || "cooling_dew_point"),
    "save-mqtt-config": () => commitMqttConfig(),
  };

  export function handleMqttAction(action, button) {
    return invokeActionMap(mqttActionHandlers, action, button);
  }
