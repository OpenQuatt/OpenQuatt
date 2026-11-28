import { state } from "../core/state.js";
import { renderOqIcon } from "../core/config.js";
import { escapeHtml } from "../core/html.js";
import { renderModalShell } from "../core/modal-shell.js";
import { getBasePath } from "../core/url-path.js";
import { createCancellationController, fetchWithTimeout } from "../core/browser-utils.js";
import { formatNumber, t } from "../i18n/index.js";
import { getSelectEntityOptions } from "./field-models.js";
import { renderIntegrationConfigSection, renderIntegrationStatus } from "./controls.js";

const safe = escapeHtml;
const finite = Number.isFinite;
const measure = (value, unit) => formatNumber(value, { maximumFractionDigits: 1 }) + " " + unit;
const supportedBy = key => getSelectEntityOptions(state.entities[key]).includes("BLE");

export function renderBleSettings() {
  if (!["roomTempSource", "coolingDewPointSource"].some(supportedBy)) return "";
  return renderIntegrationConfigSection({
    title: t("ble.title"),
    copy: t("ble.integrationCopy"),
    panelTitle: t("ble.enable"),
    statusMarkup: `<oq-ble-settings data-context="summary">${renderIntegrationStatus(reception(snapshot), receptionDetail(snapshot))}</oq-ble-settings>`,
    action: "open-bluetooth-modal",
  });
}

export function renderBleModal() {
  return renderModalShell({
    id: "system",
    titleId: "oq-bluetooth-modal-title",
    kicker: t("backupSections.integrations"),
    title: t("ble.title"),
    copy: t("ble.sourceHint"),
    className: "oq-helper-modal--bluetooth oq-helper-modal--scrollable",
    closeAction: "close-system-modal",
    closeLabel: t("ble.close"),
    bodyMarkup: '<oq-ble-settings data-context="modal" class="oq-ble"></oq-ble-settings>',
  });
}

export function renderBleSource(role, effective) {
  if (!supportedBy(role === "room" ? "roomTempSource" : "coolingDewPointSource")) return "";
  return `<oq-ble-settings data-context="${role}" data-effective="${Boolean(effective)}"></oq-ble-settings>`;
}

export function bleSensorName(sensor) {
  return sensor.name || sensor.advertised_name || t("ble.sensorFallback", { suffix: sensor.mac.slice(-5) });
}

export function bleSourceReading(status, role) {
  if (!status) return { detail: t("ble.loading") };
  const mac = role === "room" ? status.room_mac : status.dew_max_mac;
  const sensor = status.sensors.find(item => item.mac === mac);
  const name = sensor ? bleSensorName(sensor) : "";
  if (!status.enabled) return { detail: t("ble.off") };
  if (!status.ready) return { detail: t("ble.notReady") };
  if (role === "room") {
    return finite(sensor?.temperature_c)
      ? { value: measure(sensor.temperature_c, "°C"), detail: name, age: sensor.age_s }
      : { detail: sensor ? [name, unavailableReading(sensor)].join(" · ") : t("ble.unassigned") };
  }
  if (!status.dew_required) return { detail: t("ble.none") };
  const coverage = t("ble.dewCoverage", { valid: status.valid_count, count: status.required_count });
  if (!(status.dew_valid && finite(status.dew_point_c))) {
    return { detail: [t("ble.dewUnavailable"), coverage].join(" · ") };
  }
  const detail = status.valid_count < status.required_count ? [name, coverage].filter(Boolean).join(" · ") : name;
  return { value: measure(status.dew_point_c, "°C"), detail, age: sensor?.pair_age_s };
}

function formatReadingAge(age) {
  const seconds = Math.floor(age);
  const minutes = Math.floor(seconds / 60);
  if (minutes >= 60) return `${Math.floor(minutes / 60)} h ${minutes % 60} min`;
  if (!minutes) return `${seconds} s`;
  const remaining = seconds % 60;
  return remaining ? `${minutes} min ${remaining} s` : `${minutes} min`;
}

function unavailableReading(sensor) {
  return finite(sensor.age_s)
    ? [t("ble.stale"), t("ble.lastReading", { age: formatReadingAge(sensor.age_s) })].join(" · ")
    : t("ble.never");
}

export function bleSensorReading(sensor) {
  if (!finite(sensor.temperature_c)) return unavailableReading(sensor);
  const parts = [measure(sensor.temperature_c, "°C")];
  if (finite(sensor.humidity_pct)) parts.push(measure(sensor.humidity_pct, "%"));
  if (finite(sensor.dew_point_c)) {
    parts.push(t("ble.dewValue", { value: measure(sensor.dew_point_c, "°C") }));
  }
  if (finite(sensor.age_s)) parts.push(t("ble.age", { age: formatReadingAge(sensor.age_s) }));
  if (sensor.use_dew_point && !finite(sensor.dew_point_c)) {
    const pairExpired = finite(sensor.pair_age_s) && !finite(sensor.humidity_pct);
    parts.push(t(pairExpired ? "ble.staleRh" : "ble.noDewReading"));
  }
  return parts.join(" · ");
}

export function bleChangeConfirmed(status, params, target) {
  if (status.pending || status.revision < target) return false;
  if (params.active != null) return status.scanning === (params.active === "1");
  if (params.action === "enable") return status.enabled === (params.enabled === "1");
  if (params.action === "room") return (status.room_mac || "") === params.mac;
  const sensor = status.sensors.find(row => row.mac === params.mac);
  if (params.action === "remove") return !sensor;
  return params.action === "upsert" && sensor?.name === params.name &&
    sensor.protocol === params.protocol && sensor.use_dew_point === (params.use_dew_point === "1");
}

function button(key, action, mac = "", disabled = false, primary = false, accessibleName = "", icon = "") {
  return `<button type="button" data-ble="${action}" data-mac="${safe(mac)}"
    data-oq-ble-control="${action}:${safe(mac)}"
    ${accessibleName ? `aria-label="${safe(accessibleName)}"` : ""}
    class="oq-helper-button${primary ? " oq-helper-button--primary" : " oq-helper-button--ghost"}${icon ? " oq-ble-icon-button" : ""}"
    ${disabled ? "disabled" : ""}>${icon ? renderOqIcon(icon) : safe(t(key))}</button>`;
}

const measurementColumns = ["ble.temperatureShort", "ble.humidityShort", "ble.dewPoint", "ble.lastMeasurement"];

const assignmentIcon = icon => renderOqIcon(icon, "oq-ble-assignment-icon");
function assignmentMarker(icon, label) {
  const name = safe(label);
  return ` <span role="img" aria-label="${name}" title="${name}">${assignmentIcon(icon)}</span>`;
}

function savedMeasurementValues(sensor) {
  const age = finite(sensor.age_s) ? t("ble.age", { age: formatReadingAge(sensor.age_s) }) : "";
  return [
    finite(sensor.temperature_c) ? measure(sensor.temperature_c, "°C") : "",
    finite(sensor.humidity_pct) ? measure(sensor.humidity_pct, "%") : "",
    finite(sensor.dew_point_c) ? measure(sensor.dew_point_c, "°C") : "",
    finite(sensor.temperature_c) ? age : [t(age ? "ble.expired" : "ble.never"), age].filter(Boolean).join(" · "),
  ];
}

function savedReadings(sensor, readings, roomAssigned) {
  const values = readings ? savedMeasurementValues(sensor) : [];
  return measurementColumns.map((key, index) => {
    let marker = "";
    if (key === "ble.temperatureShort" && roomAssigned) marker = assignmentMarker("house", t("ble.roomSelect"));
    if (key === "ble.dewPoint" && sensor.use_dew_point) marker = assignmentMarker("droplet", t("ble.dewAssigned"));
    return `<span class="oq-ble-cell" data-ble-cell${values[index] || marker ? "" : " data-empty"}>
      <span class="oq-ble-cell-label">${safe(t(key))}</span><span>${safe(values[index] || "—")}</span>${marker}</span>`;
  }).join("");
}

function row(sensor, saved, status, locked, readings) {
  const name = bleSensorName(sensor);
  return `<li class="oq-ble-row${saved ? " oq-ble-row--saved" : ""}">
    <strong class="oq-ble-row-label">${safe(name)}</strong>
    ${saved
      ? `<div class="oq-ble-metrics" data-ble-saved-reading="${safe(sensor.mac)}">${savedReadings(sensor, readings, sensor.mac === status.room_mac)}</div>`
      : `<span class="oq-ble-row-reading" data-ble-reading="${safe(sensor.mac)}">${readings ? safe(bleSensorReading(sensor)) : ""}</span>`}
    ${button(saved ? null : "ble.add", saved ? "edit" : "add", sensor.mac,
      locked || (!saved && status.sensors.length >= status.max_sensors), !saved,
      t(saved ? "ble.editNamed" : "ble.addNamed", { name }), saved ? "more-horizontal" : "")}
  </li>`;
}

function reception(status) {
  if (offline) return t("ble.unknown");
  if (!status) return t("ble.loading");
  if (!status.enabled) return t("ble.off");
  return status.ready ? t("ble.active") : t("ble.notReady");
}

function receptionDetail(status) {
  if (offline) return t("ble.error");
  if (!status) return "";
  if (!status.enabled) return t("ble.enableHint");
  const count = status.sensors.length;
  return count ? t(count === 1 ? "ble.sensorCountOne" : "ble.sensorCount", { count }) : t("ble.empty");
}

function modalReadings(status) {
  const room = status.enabled && status.ready && status.sensors.find(sensor => sensor.mac === status.room_mac);
  const reading = bleSourceReading(status, "dew");
  const dewSensor = reading.value && status.sensors.find(sensor => sensor.mac === status.dew_max_mac);
  return {
    fresh: status.enabled && status.ready && status.sensors.length ? t("ble.freshCount", {
      valid: status.sensors.filter(sensor => finite(sensor.temperature_c)).length, count: status.sensors.length,
    }) : "",
    room: finite(room?.temperature_c) ? measure(room.temperature_c, "°C") : "—",
    roomStatus: room && !finite(room.temperature_c) ? unavailableReading(room) : "",
    dew: reading.value || "",
    coverage: reading.value ? [dewSensor && bleSensorName(dewSensor),
      t("ble.dewCoverageShort", { valid: status.valid_count, count: status.required_count })].filter(Boolean).join(" · ") : reading.detail,
  };
}

function roomOptionReading(status, sensor) {
  if (!status.enabled || !status.ready) return reception(status);
  if (finite(sensor.temperature_c)) return measure(sensor.temperature_c, "°C");
  return t(finite(sensor.age_s) ? "ble.expired" : "ble.never");
}

function renderRoomPicker(status, locked, readings) {
  return `<section class="oq-ble-room-picker" aria-labelledby="oq-ble-room-picker-title">
    <div class="oq-ble-section-head"><h3 id="oq-ble-room-picker-title">${safe(t("ble.roomSelect"))}</h3>
      ${button("ble.back", "room-back")}</div>
    <ul>${status.sensors.map(sensor => {
      const selected = sensor.mac === status.room_mac;
      return `<li><button type="button" class="oq-helper-button oq-helper-button--ghost oq-ble-room-option"
        data-ble="room" data-mac="${safe(sensor.mac)}" data-oq-ble-control="room:${safe(sensor.mac)}"
        ${selected ? 'aria-current="true"' : ""} ${locked ? "disabled" : ""}>
        <span><strong>${safe(bleSensorName(sensor))}</strong>${selected ? `<small>${safe(t("ble.currentRoom"))}</small>` : ""}</span>
        <small data-ble-room-reading="${safe(sensor.mac)}">${readings ? safe(roomOptionReading(status, sensor)) : ""}</small>
      </button></li>`;
    }).join("")}</ul>
    ${status.room_mac ? `<div class="oq-ble-room-actions">${button("ble.clearRoom", "room", "", locked, false, t("ble.clearRoomLabel"))}</div>` : ""}
  </section>`;
}

function searchMessage(status, searched, stopped, active, foundCount) {
  if (active === "0") return t("ble.stopping");
  if (active === "1" || status.scanning) {
    return foundCount ? t("ble.searchingFound", { count: foundCount }) : t("ble.searching");
  }
  if (searched && stopped && status.ready) return t("ble.stopped");
  if (foundCount) return t(foundCount === 1 ? "ble.foundOne" : "ble.foundCount", { count: foundCount });
  return searched && status.ready ? t("ble.noNew") : "";
}

export function renderBleReadings(status, locked, searched, readings = true, searchStopped = false, roomPickerOpen = false, activeScanChange) {
  const found = status.enabled ? status.discovered.filter(sensor => !status.sensors.some(saved => saved.mac === sensor.mac)) : [];
  const room = status.sensors.find(sensor => sensor.mac === status.room_mac);
  const search = searchMessage(status, searched, searchStopped, activeScanChange, found.length);
  const live = readings ? modalReadings(status) : {};
  return `<div class="oq-ble-summary">
      <div class="oq-ble-reception">
        <h3>${safe(t("ble.enable"))}</h3>
        <label class="oq-settings-mqtt-toggle"><input type="checkbox" name="bluetooth-reception" data-ble="enable" data-oq-ble-control="enable"
          aria-label="${safe(t("ble.enable"))}" ${status.enabled ? "checked" : ""} ${locked ? "disabled" : ""}>
          <span>${safe(reception(status))}</span></label>
        <small data-ble-live="fresh">${safe(live.fresh || "")}</small>
      </div>
      <div class="oq-ble-room">
        <h3>${assignmentIcon("house")} ${safe(t("ble.room"))}</h3>
        <strong data-ble-live="room">${safe(live.room || "")}</strong>
        <small>${safe(room ? bleSensorName(room) : t("ble.unassigned"))}</small>
        <small data-ble-live="roomStatus">${safe(live.roomStatus || "")}</small>
        ${roomPickerOpen ? "" : button(room ? "ble.changeRoom" : "ble.chooseRoom", "room-picker", "",
          locked || !status.sensors.length, false, t(room ? "ble.changeRoomLabel" : "ble.chooseRoomLabel"))}
      </div>
      <div class="oq-ble-dew">
        <h3>${assignmentIcon("droplet")} ${safe(t("ble.dew"))}</h3>
        <strong data-ble-live="dew">${safe(live.dew || "")}</strong>
        <small data-ble-live="coverage">${safe(live.coverage || "")}</small>
      </div>
      ${roomPickerOpen ? renderRoomPicker(status, locked, readings) : ""}
    </div>
    <section class="oq-ble-section">
      <div class="oq-ble-section-head"><h3>${safe(t("ble.sensors"))} <small>${safe(t("ble.savedCount", { count: status.sensors.length }))}</small></h3>
        ${button(status.scanning ? "ble.stop" : searched ? "ble.scanAgain" : "ble.scan", "scan", "", locked || !status.ready, !found.length)}</div>
      <div class="oq-ble-table">
        ${status.sensors.length ? `<div class="oq-ble-table-head" aria-hidden="true"><span>${safe(t("ble.sensor"))}</span>
          ${measurementColumns.map(key => `<span>${safe(t(key))}</span>`).join("")}<span></span></div>` : ""}
        ${search ? `<div class="oq-ble-discovered">
          <p role="status" data-ble-search-status>${safe(search)}</p>
          ${found.length ? `<ul>${found.map(sensor => row(sensor, false, status, locked, readings)).join("")}</ul>` : ""}
        </div>` : ""}
        ${status.sensors.length ? `<ul>${status.sensors.map(sensor => row(sensor, true, status, locked, readings)).join("")}</ul>`
          : `<p class="oq-ble-empty">${safe(t("ble.empty"))}</p>`}
      </div>
      ${status.sensors.length >= status.max_sensors ? `<p>${safe(t("ble.limit"))}</p>` : ""}
    </section>`;
}

const errors = { forbidden: "ble.auth", invalid_configuration: "ble.invalid", stale_configuration: "ble.conflict",
  configuration_busy: "ble.busy", ble_unavailable: "ble.unavailable" };

function request(endpoint, body, signal) {
  return fetchWithTimeout(getBasePath() + "/openquatt/ble/" + endpoint, {
    method: body ? "POST" : "GET", credentials: "same-origin", cache: "no-store", signal,
    ...(body ? { body: new URLSearchParams(body) } : {}),
  }, 8000, "", async response => {
    const result = await response.json().catch(() => ({}));
    if (!response.ok) throw Object.assign(new Error(), {
      labelKey: response.status === 401 || response.status === 403 ? "ble.auth" : errors[result.error],
    });
    return result;
  });
}

// Integrations, sensor selection and the dialog share one short-lived status request.
const hosts = new Set();
let snapshot = null;
let expected = null;
let writing = null;
let notice = "";
let noticeExpires = 0;
let offline = "";
let searched = false;
let searchStopped = false;
let editor = null;
let lastRead = 0;
let modalOpen = false;
let timer, controller, readTask;

function refreshViews() {
  for (const view of hosts) view.paint();
}

function delayUntilNextRead() {
  if (expected || snapshot?.pending) return 400;
  if (offline) return 8000;
  if (!modalOpen) return 15000;
  return snapshot?.scanning ? 1000 : 5000;
}

function queuePoll(ms = 0) {
  clearTimeout(timer);
  if (hosts.size && !document.hidden) timer = setTimeout(poll, ms);
}

function visibilityChanged() {
  if (document.hidden) {
    clearTimeout(timer);
    controller?.abort();
  } else {
    queuePoll();
  }
}

async function loadStatus(signal) {
  try {
    const value = await request("status", null, signal);
    if (signal.aborted) return;
    if (!Array.isArray(value.sensors) || !Array.isArray(value.discovered) ||
        !Number.isSafeInteger(value.revision)) throw new Error();
    snapshot = value;
    offline = "";
    if (expected && bleChangeConfirmed(value, expected.params, expected.target)) {
      if (expected.params.active != null) {
        searched = true;
        searchStopped = expected.params.active === "0";
      } else {
        notice = "ble.done";
        noticeExpires = Date.now() + 3500;
        if (editor === expected.editor) closeEditor();
      }
      expected.onConfirmed?.();
      expected = null;
    } else if (expected && value.storage_error) {
      expected = null;
      notice = "ble.storage";
    }
  } catch (error) {
    if (!signal.aborted) offline = error.labelKey || "ble.error";
  }
}

async function poll() {
  if (!hosts.size || document.hidden || writing || readTask) return;
  controller = createCancellationController();
  readTask = loadStatus(controller.signal);
  await readTask;
  readTask = null;
  controller = null;
  if (expected && Date.now() >= expected.deadline) {
    expected = null;
    notice = "ble.unconfirmed";
  }
  lastRead = Date.now();
  if (notice === "ble.done" && lastRead >= noticeExpires) notice = "";
  refreshViews();
  queuePoll(delayUntilNextRead());
}

async function write(endpoint, params, revision = snapshot?.revision, onConfirmed) {
  if (!snapshot?.csrf_token || offline || writing || expected || snapshot.pending) return;
  const token = snapshot.csrf_token;
  writing = params;
  notice = "";
  refreshViews();
  // Cancel the old GET and confirm the POST with a fresh GET immediately.
  controller?.abort();
  await readTask;
  try {
    const ack = await request(endpoint, { ...params, csrf_token: token, revision });
    if (ack.ok !== true || !Number.isSafeInteger(ack.revision)) throw new Error();
    expected = { params, target: ack.revision, deadline: Date.now() + 15000, editor, onConfirmed };
  } catch (error) {
    notice = error.labelKey || "ble.unconfirmed";
  } finally {
    writing = null;
    refreshViews();
    queuePoll();
  }
}

function closeEditor() {
  if (!editor) return;
  const mac = editor.sensor.mac;
  editor = null;
  queueMicrotask(() => {
    const view = [...hosts].find(host => host.dataset.context === "modal");
    if (!view) return;
    const control = [...view.querySelectorAll('[data-ble="edit"], [data-ble="add"]')]
      .find(button => button.dataset.mac === mac && !button.disabled);
    (control || view.querySelector('[data-ble="scan"]:not(:disabled)') || view.dialog?.querySelector(".oq-helper-modal-close"))
      ?.focus({ preventScroll: true });
  });
}

function renderEditor(draft) {
  const sensor = draft.sensor;
  const details = [sensor.mac, sensor.protocol, finite(sensor.rssi_dbm) ? measure(sensor.rssi_dbm, "dBm") : ""].filter(Boolean);
  return `<section class="oq-ble-section oq-ble-editor">
    <div class="oq-ble-section-head"><h3>${safe(t(draft.saved ? "ble.editSensor" : "ble.addSensor"))}</h3>${button("ble.back", "cancel")}</div>
    <label class="oq-ble-field">${safe(t("ble.name"))}<input class="oq-helper-input" name="bluetooth-sensor-name" type="text" maxlength="32" value="${safe(draft.name)}"></label>
    <label class="oq-settings-mqtt-toggle"><input name="bluetooth-required-dew-point" type="checkbox" ${draft.dew ? "checked" : ""}
      ${!draft.dew && !finite(sensor.dew_point_c) ? "disabled" : ""}><span>${safe(t("ble.dewRequired"))}</span></label>
    ${!finite(sensor.dew_point_c) ? `<p>${safe(t(finite(sensor.pair_age_s) ? "ble.staleRh" : "ble.noRh"))}</p>` : ""}
    <details><summary>${safe(t("ble.details"))}</summary><p>${safe(details.join(" · "))}</p>
      ${sensor.advertised_name ? `<p>${safe(t("ble.advertisedName", { name: sensor.advertised_name }))}</p>` : ""}</details>
    <div class="oq-helper-modal-actions">${button("common.save", "save", "", false, true)}
      ${draft.saved ? button("ble.remove", "remove") : ""}</div>
    <div data-ble-confirm ${draft.remove ? "" : "hidden"}><p>${safe(t("ble.confirm"))}</p>
      ${button("ble.remove", "confirm")}${button("common.cancel", "cancel-remove")}</div>
  </section>`;
}

class BleSettings extends (globalThis.HTMLElement || class {}) {
  connectedCallback() {
    const open = this.dataset.context === "modal" && !modalOpen;
    if (!hosts.size) document.addEventListener("visibilitychange", visibilityChanged);
    hosts.add(this);
    if (this.dataset.context === "modal") {
      modalOpen = true;
      this.dialog = this.closest('[role="dialog"]');
      this.keyboard = event => this.handleDialogKey(event);
      this.dialog?.addEventListener("keydown", this.keyboard);
      this.onclick = event => this.clickAction(event);
      this.oninput = event => {
        if (editor && event.target.name === "bluetooth-sensor-name") editor.name = event.target.value;
      };
      this.onchange = event => this.changeAction(event);
    }
    this.paint();
    queuePoll(open ? 0 : Math.max(0, lastRead + delayUntilNextRead() - Date.now()));
  }

  disconnectedCallback() {
    hosts.delete(this);
    this.dialog?.removeEventListener("keydown", this.keyboard);
    queueMicrotask(() => {
      modalOpen = [...hosts].some(view => view.dataset.context === "modal");
      if (!modalOpen) {
        editor = null;
        if (!snapshot?.scanning) searched = false;
      }
      if (hosts.size) return;
      clearTimeout(timer);
      controller?.abort();
      document.removeEventListener("visibilitychange", visibilityChanged);
    });
  }

  paint(focus) {
    const context = this.dataset.context;
    if (context === "summary") {
      this.querySelector(".oq-settings-quickstart-status-value").textContent = reception(snapshot);
      this.querySelector(".oq-settings-quickstart-status-copy").textContent = receptionDetail(snapshot);
      return;
    }
    if (context !== "modal") {
      const reading = offline ? { detail: t("ble.unknown") } : bleSourceReading(snapshot, context);
      const effective = Boolean(reading.value && this.dataset.effective === "true");
      const detail = [reading.detail, finite(reading.age) && t("ble.age", { age: formatReadingAge(reading.age) })].filter(Boolean).join(" · ");
      this.innerHTML = `<div class="oq-settings-source-row${effective ? " is-effective" : ""}" data-source-kind="ble"
        data-source-state="${reading.value ? "valid" : "missing"}" ${effective ? 'data-source-effective="true"' : ""}>
        <div class="oq-ble-source-label"><span>${safe(t("ble.source"))}</span><small>${safe(detail)}</small></div><strong>${safe(reading.value || "—")}</strong>
      </div>`;
      return;
    }
    if (!this.querySelector("[data-ble-body]")) {
      this.innerHTML = '<div data-ble-body></div><div data-ble-editor hidden></div><p role="status" data-ble-message></p>';
    }
    const body = this.querySelector("[data-ble-body]");
    if (focus) this.focusControl = focus;
    const locked = Boolean(writing || expected || snapshot?.pending || !snapshot?.csrf_token || offline);
    const activeChange = writing || expected?.params;
    const content = snapshot ? renderBleReadings(snapshot, locked, searched, false, searchStopped, this.roomPickerOpen, activeChange?.active)
      : offline ? "" : safe(t("ble.loading"));
    if (this.previous !== content) {
      const active = this.getRootNode().activeElement;
      const identity = body.contains(active) && active?.dataset.oqBleControl;
      body.innerHTML = content;
      this.previous = content;
      if (identity && !this.focusControl) this.focusControl = identity;
    }
    if (this.focusControl) {
      const control = [...body.querySelectorAll("[data-oq-ble-control]")]
        .find(el => el.dataset.oqBleControl === this.focusControl && !el.disabled);
      const active = this.getRootNode().activeElement;
      if (control) {
        if (focus || !this.dialog?.contains(active) || active === this.dialog || active === control) {
          control.focus({ preventScroll: true });
        }
        this.focusControl = null;
      } else if (!locked) {
        this.focusControl = null;
      }
    }
    if (snapshot) {
      const live = modalReadings(snapshot);
      for (const element of body.querySelectorAll("[data-ble-live]")) {
        const value = live[element.dataset.bleLive];
        if (element.textContent !== value) element.textContent = value;
      }
      for (const element of body.querySelectorAll("[data-ble-saved-reading]")) {
        const sensor = snapshot.sensors.find(row => row.mac === element.dataset.bleSavedReading);
        if (!sensor) continue;
        const values = savedMeasurementValues(sensor);
        [...element.querySelectorAll("[data-ble-cell]")].forEach((cell, index) => {
          const reading = cell.children[1];
          const value = values[index] || "—";
          cell.toggleAttribute("data-empty", !values[index] && !reading.nextElementSibling);
          if (reading.textContent !== value) reading.textContent = value;
        });
      }
      for (const element of body.querySelectorAll("[data-ble-reading]")) {
        const sensor = snapshot.discovered.find(row => row.mac === element.dataset.bleReading);
        if (!sensor) continue;
        const value = bleSensorReading(sensor);
        if (element.textContent !== value) element.textContent = value;
      }
      for (const element of body.querySelectorAll("[data-ble-room-reading]")) {
        const sensor = snapshot.sensors.find(row => row.mac === element.dataset.bleRoomReading);
        if (!sensor) continue;
        const value = roomOptionReading(snapshot, sensor);
        if (element.textContent !== value) element.textContent = value;
      }
    }
    const editorArea = this.querySelector("[data-ble-editor]");
    body.hidden = Boolean(editor);
    editorArea.hidden = !editor;
    if (editor && this.shownEditor !== editor) {
      editorArea.innerHTML = renderEditor(editor);
      this.shownEditor = editor;
    } else if (!editor) {
      this.shownEditor = null;
      editorArea.replaceChildren();
    }
    const conflict = Boolean(editor && snapshot && !writing && !expected && snapshot.revision !== editor.revision);
    for (const control of editorArea.querySelectorAll('[data-ble="save"], [data-ble="confirm"]')) {
      control.disabled = locked || conflict;
    }
    const message = activeChange?.active != null ? ""
      : activeChange || snapshot?.pending ? "ble.pending"
      : offline || notice || (conflict ? "ble.conflict" : snapshot?.storage_error ? "ble.storage" : "");
    const region = this.querySelector("[data-ble-message]");
    const messageText = message ? t(message) : "";
    if (region.textContent !== messageText) region.textContent = messageText;
    const retry = this.querySelector('[data-ble="retry"]');
    const canRetry = offline || notice === "ble.unconfirmed" || notice === "ble.conflict";
    if (canRetry && !retry) region.insertAdjacentHTML("afterend", button("ble.retry", "retry"));
    else if (!canRetry && retry) retry.remove();
  }

  changeAction(event) {
    const el = event.target;
    if (el.name === "bluetooth-required-dew-point" && editor) editor.dew = el.checked;
    if (el.dataset.ble === "enable") write("save", { action: "enable", enabled: el.checked ? "1" : "0" });
  }

  closeRoomPicker() {
    this.roomPickerOpen = false;
    this.focusControl = "room-picker:";
  }

  clickAction(event) {
    const el = event.target.closest("[data-ble]");
    if (!el || el.disabled) return;
    const action = el.dataset.ble;
    if (action === "retry") {
      notice = "";
      queuePoll();
    }
    if (action === "scan") write("scan", { active: snapshot.scanning ? "0" : "1" });
    if (action === "room-picker") {
      this.roomPickerOpen = true;
      this.paint("room-back:");
    }
    if (action === "room-back") {
      this.closeRoomPicker();
      this.paint("room-picker:");
    }
    if (action === "room") {
      const mac = el.dataset.mac;
      if (mac === (snapshot.room_mac || "")) {
        this.closeRoomPicker();
        this.paint("room-picker:");
      } else {
        write("save", { action: "room", mac }, snapshot.revision, () => {
          if (this.isConnected && this.roomPickerOpen) this.closeRoomPicker();
        });
      }
    }
    if (action === "add" || action === "edit") {
      const saved = action === "edit";
      const sensor = (saved ? snapshot.sensors : snapshot.discovered).find(item => item.mac === el.dataset.mac);
      if (!sensor) return;
      this.roomPickerOpen = false;
      editor = { sensor, saved, revision: snapshot.revision, name: bleSensorName(sensor), dew: Boolean(sensor.use_dew_point) };
      notice = "";
      this.paint();
      this.querySelector('input[name="bluetooth-sensor-name"]').focus({ preventScroll: true });
    }
    if (action === "cancel") {
      closeEditor();
      this.paint();
    }
    if (action === "remove" || action === "cancel-remove") {
      editor.remove = action === "remove";
      const confirm = this.querySelector("[data-ble-confirm]");
      confirm.hidden = !editor.remove;
      (editor.remove ? confirm.querySelector('[data-ble="confirm"]') : this.querySelector('[data-ble="remove"]')).focus();
    }
    if (action === "confirm") write("save", { action: "remove", mac: editor.sensor.mac }, editor.revision);
    if (action === "save") {
      const name = editor.name.trim();
      if (!name || new TextEncoder().encode(name).length > 32) {
        notice = "ble.bytes";
        this.paint();
        return;
      }
      write("save", { action: "upsert", mac: editor.sensor.mac, protocol: editor.sensor.protocol, name,
        use_dew_point: editor.dew ? "1" : "0" }, editor.revision);
    }
  }

  handleDialogKey(event) {
    if (event.key === "Escape") {
      event.preventDefault();
      if (editor) {
        closeEditor();
        this.paint();
      } else if (this.roomPickerOpen) {
        this.closeRoomPicker();
        this.paint("room-picker:");
      } else {
        this.dialog.querySelector(".oq-helper-modal-close").click();
      }
    }
    if (event.key !== "Tab") return;
    const focusable = [...this.dialog.querySelectorAll("button, input, select, summary, a[href]")]
      .filter(el => !el.disabled && el.getClientRects().length);
    const target = event.shiftKey ? focusable.at(-1) : focusable[0];
    const boundary = event.shiftKey ? focusable[0] : focusable.at(-1);
    if (target && (document.activeElement === boundary || !focusable.includes(document.activeElement))) {
      event.preventDefault();
      target.focus();
    }
  }
}

if (globalThis.customElements && !customElements.get("oq-ble-settings")) {
  customElements.define("oq-ble-settings", BleSettings);
}
