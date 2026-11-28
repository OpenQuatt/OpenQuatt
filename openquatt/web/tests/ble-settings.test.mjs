import assert from "node:assert/strict";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;
globalThis.window = { localStorage: { getItem: () => null } };
const { state } = await import("../js/src/core/state.js");
const { bleChangeConfirmed, bleSensorName, bleSensorReading, bleSourceReading, renderBleModal, renderBleReadings, renderBleSettings } = await import("../js/src/settings/ble.js");

test("only firmware advertising BLE gets the management section", () => {
  state.entities = { roomTempSource: { option: ["MQTT"] } };
  assert.equal(renderBleSettings(), "");
  state.entities.coolingDewPointSource = { options: ["Auto", "BLE"] };
  assert.match(renderBleSettings(), /class="oq-settings-section"/);
  assert.match(renderBleSettings(), /class="oq-settings-quickstart-status-row oq-settings-mqtt-status-row"/);
  assert.match(renderBleSettings(), /data-oq-action="open-bluetooth-modal">Aanpassen/);
  assert.doesNotMatch(renderBleSettings(), /data-context="modal"/);
  assert.match(renderBleModal(), /role="dialog".*aria-modal="true"/);
  assert.match(renderBleModal(), /class="[^"]*oq-helper-modal--scrollable[^"]*"/);
  assert.match(renderBleModal(), /data-context="modal"/);
});

test("zero, stale and missing readings are distinct", () => {
  assert.match(bleSensorReading({ temperature_c: 0, humidity_pct: 0, age_s: 0 }), /0 °C · 0 % · 0 s geleden/);
  assert.equal(bleSensorReading({ age_s: 312, pair_age_s: 312, use_dew_point: true }),
    "Geen actuele meting · Laatste nieuwe meting 5 min 12 s geleden");
  assert.equal(bleSensorReading({ temperature_c: 18.5, age_s: 63, pair_age_s: 312, use_dew_point: true }),
    "18,5 °C · 1 min 3 s geleden · Dauwpuntmeting verlopen");
  assert.equal(bleSensorReading({ temperature_c: 18.5, humidity_pct: 0, age_s: 8, pair_age_s: 8, use_dew_point: true }),
    "18,5 °C · 0 % · 8 s geleden · Geen geldig dauwpunt");
  assert.match(bleSensorReading({}), /Nog geen meting/);
});

test("degraded dew coverage, disabled reception and untrusted names have distinct safe output", () => {
  const sensor = { mac: "DA:11:22:33:44:01", name: '<img src=x onerror="bad()">', protocol: "bthome_v2",
    use_dew_point: true, dew_point_c: 6 };
  const stale = { mac: "DA:11:22:33:44:02", name: "Zolder", use_dew_point: true, dew_point_c: null, pair_age_s: 312 };
  const status = { enabled: true, ready: true, room_mac: sensor.mac, dew_required: true, dew_valid: true,
    dew_max_mac: sensor.mac, dew_point_c: 6, valid_count: 1, required_count: 2, max_sensors: 8,
    sensors: [sensor, stale], discovered: [] };
  const partial = renderBleReadings(status, false, false);
  assert.match(partial, /data-ble-live="dew">6 °C/);
  assert.match(partial, /1 van 2 meetparen geldig/);
  assert.doesNotMatch(partial, /<img/);
  assert.match(partial, /&lt;img/);
  assert.match(renderBleReadings({ ...status, enabled: false }, false, false), /Ontvangst uit/);
  assert.match(renderBleReadings({ ...status, ready: false }, false, false), /Ontvanger niet gereed/);
  assert.doesNotMatch(renderBleReadings({ ...status, dew_required: false }, false, false), /1 van 2/);
});

test("the room picker opens above the flat measurement list and keeps related actions local", () => {
  const room = { mac: "DA:11:22:33:44:01", name: "Werkkamer", use_dew_point: true, temperature_c: 19.4, age_s: 5 };
  const other = { mac: "DA:11:22:33:44:02", name: "Garage", temperature_c: null, age_s: 312 };
  const status = { enabled: true, ready: true, room_mac: room.mac, sensors: [other, room],
    discovered: [], max_sensors: 8, dew_required: true, dew_valid: false, valid_count: 0, required_count: 1 };
  const html = renderBleReadings(status, false, false);
  assert.match(html, /data-ble="room-picker"/);
  assert.doesNotMatch(html, /data-ble="room-back"|<select/);
  assert.match(html, /aria-label="Werkkamer bewerken"/);
  assert.match(html, /Meting verlopen · 5 min 12 s geleden/);
  assert.match(html, /Actuele metingen · 1 van 2/);
  assert.match(html, /data-ble-live="room">19,4 °C/);
  const open = renderBleReadings(status, false, false, true, false, true);
  assert.doesNotMatch(open, /data-ble="room-picker"|oq-ble-role|<select/);
  assert.match(open, /data-ble="room-back"[^>]*>Terug/);
  assert.match(open, /data-ble="room" data-mac=""[^>]*>Wis keuze/);
  assert.match(open, /data-mac="DA:11:22:33:44:01"[^>]*aria-current="true"[^>]*>\s*<span><strong>Werkkamer<\/strong><small>Huidige keuze/);
  assert.ok(open.indexOf("Huidige keuze") < open.indexOf("Sensoren zoeken"));
  const locked = renderBleReadings(status, true, false, true, false, true);
  assert.match(locked, /data-ble="room" data-mac="DA:11:22:33:44:01"[^>]*disabled/);
  assert.doesNotMatch(locked, /data-ble="room-back"[^>]*disabled/);
  const none = renderBleReadings({ ...status, room_mac: "" }, false, false, true, false, true);
  assert.doesNotMatch(none, /Wis keuze|aria-current="true"/);
});

test("assignment markers follow the configured roles and survive missing dew readings", () => {
  const room = { mac: "DA:11:22:33:44:01", name: "Werkkamer", temperature_c: 19.4, dew_point_c: 6 };
  const dew = { mac: "DA:11:22:33:44:02", name: "Zolder", use_dew_point: true, dew_point_c: null };
  const status = { enabled: true, ready: true, room_mac: room.mac, sensors: [room, dew], discovered: [] };
  const html = renderBleReadings(status, false, false);
  assert.match(html, /Temp\.<\/span><span>19,4 °C<\/span> <span role="img" aria-label="Sensor voor kamertemperatuur"/);
  assert.doesNotMatch(html, /(?:Werkkamer|Zolder) <span role="img"/);
  assert.match(html, /Dauwpunt<\/span><span>6 °C<\/span><\/span>/);
  for (const readings of [true, false]) {
    assert.match(renderBleReadings(status, false, false, readings),
      /data-ble-cell>\s*<span class="oq-ble-cell-label">Dauwpunt<\/span><span>—<\/span> <span role="img" aria-label="Toegewezen aan dauwpuntbewaking"/);
  }
  const unavailable = renderBleReadings({ ...status, sensors: [{ ...room, temperature_c: null }, dew] }, false, false);
  assert.match(unavailable,
    /data-ble-cell>\s*<span class="oq-ble-cell-label">Temp\.<\/span><span>—<\/span> <span role="img" aria-label="Sensor voor kamertemperatuur"/);
});

test("search has one status inside the measurement list and adds only new discoveries", () => {
  const stored = { mac: "DA:11:22:33:44:01", name: "Werkkamer" };
  const discovered = { mac: "DA:11:22:33:44:02", name: "Garage" };
  const status = { enabled: true, ready: true, sensors: [stored], discovered: [stored, discovered], max_sensors: 8 };
  const markup = renderBleReadings({ ...status, scanning: true }, false, true);
  assert.equal(markup.match(/role="status"/g)?.length, 1);
  assert.match(markup, /Zoeken naar sensoren… 1 gevonden/);
  assert.equal(markup.match(/data-ble="add"/g)?.length, 1);
  assert.ok(markup.indexOf("data-ble-search-status") < markup.indexOf('data-ble="add"'));
  assert.ok(markup.indexOf('data-ble="add"') < markup.indexOf('data-ble="edit"'));
  assert.match(renderBleReadings(status, false, true), /1 nieuwe sensor gevonden\./);
  assert.match(renderBleReadings(status, false, true, true, true), /Zoeken gestopt\./);
  assert.match(renderBleReadings({ ...status, discovered: [] }, false, true), /Geen nieuwe sensoren gevonden\./);
  assert.match(renderBleReadings(status, true, false, false, false, false, "0"), /Zoeken wordt gestopt…/);
});

test("discovery names are escaped and a local alias stays authoritative", () => {
  const sensor = { mac: "DA:11:22:33:44:01", advertised_name: "<script>", protocol: "bthome_v2" };
  assert.equal(bleSensorName(sensor), "<script>");
  assert.equal(bleSensorName({ ...sensor, name: "Werkkamer" }), "Werkkamer");
  assert.equal(bleSensorName({ ...sensor, advertised_name: "" }), "Sensor …44:01");
  const status = { enabled: true, ready: true, sensors: [], discovered: [sensor], max_sensors: 8 };
  assert.match(renderBleReadings(status, false, true), /&lt;script&gt;/);
  assert.doesNotMatch(renderBleReadings(status, false, true), /<script>/);
});

test("available measurements retain the highest dew point with partial coverage", () => {
  const room = { mac: "DA:11:22:33:44:01", name: "Werkkamer", temperature_c: 19.4, age_s: 5 };
  const dew = { mac: "DA:11:22:33:44:02", name: "Bijkeuken", dew_point_c: 6, pair_age_s: 12 };
  const status = { enabled: true, ready: true, sensors: [room, dew], room_mac: room.mac, dew_required: true,
    dew_valid: true, dew_max_mac: dew.mac, dew_point_c: 6, valid_count: 2, required_count: 2 };
  assert.deepEqual(bleSourceReading(status, "room"), { value: "19,4 °C", detail: "Werkkamer", age: 5 });
  assert.deepEqual(bleSourceReading(status, "dew"), { value: "6 °C", detail: "Bijkeuken", age: 12 });
  assert.deepEqual(bleSourceReading({ ...status, sensors: [{ ...room, temperature_c: null, age_s: 312 }] }, "room"), {
    detail: "Werkkamer · Geen actuele meting · Laatste nieuwe meting 5 min 12 s geleden",
  });
  assert.deepEqual(bleSourceReading({ ...status, valid_count: 1 }, "dew"), {
    value: "6 °C", detail: "Bijkeuken · 1 van 2 toegewezen sensoren leveren een geldige meting", age: 12,
  });
  const unavailable = bleSourceReading({ ...status, dew_valid: false, dew_point_c: null, valid_count: 0 }, "dew");
  assert.equal(unavailable.value, undefined);
  assert.match(unavailable.detail, /Dauwpunt via Bluetooth niet beschikbaar · 0 van 2/);
});

test("save confirmation requires the stored configuration at the expected revision", () => {
  const sensor = { mac: "DA:11:22:33:44:01", name: "Kamer", protocol: "bthome_v2", use_dew_point: true };
  const params = { ...sensor, use_dew_point: "1", action: "upsert" };
  const status = { revision: 4, sensors: [sensor] };
  assert.equal(bleChangeConfirmed(status, params, 4), true);
  assert.equal(bleChangeConfirmed({ ...status, pending: true }, params, 4), false);
  assert.equal(bleChangeConfirmed(status, params, 5), false);
  assert.equal(bleChangeConfirmed(status, { ...params, use_dew_point: "0" }, 4), false);
  assert.equal(bleChangeConfirmed(status, { action: "room", mac: sensor.mac }, 4), false);
  assert.equal(bleChangeConfirmed({ ...status, room_mac: sensor.mac }, { action: "room", mac: sensor.mac }, 4), true);
  assert.equal(bleChangeConfirmed({ ...status, room_mac: sensor.mac }, { action: "room", mac: "" }, 4), false);
  assert.equal(bleChangeConfirmed({ ...status, room_mac: "" }, { action: "room", mac: "" }, 4), true);
  assert.equal(bleChangeConfirmed(status, { action: "remove", mac: sensor.mac }, 4), false);
  assert.equal(bleChangeConfirmed({ ...status, scanning: false }, { active: "1" }, 4), false);
});
