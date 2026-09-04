import assert from "node:assert/strict";
import test from "node:test";

import {
  buildSettingsBackupMqttConfig,
  collectUnknownSettingsBackupItems,
  isSettingsBackupMqttSourceSelection,
  normalizeSettingsBackupMqttConfig,
  normalizeSettingsBackupOduProfiles,
  settingsBackupMqttNeedsPassword,
} from "../js/src/core/settings-backup-domain.js";

test("MQTT backup keeps configuration but excludes runtime and secret fields", () => {
  const backup = buildSettingsBackupMqttConfig({
    enabled: true,
    broker: "mqtt.local",
    port: 1883,
    username: "openquatt",
    password_set: true,
    csrf_token: "secret-token",
    connected: true,
    input_enabled: { cooling_dew_point: false },
    input_accept_retained: { room_setpoint: false },
  });

  assert.equal(backup.password_was_set, true);
  assert.equal(backup.input_enabled.cooling_dew_point, false);
  assert.equal(backup.input_enabled.room_temperature, true);
  assert.equal(backup.input_accept_retained.room_setpoint, false);
  assert.equal(Object.hasOwn(backup, "csrf_token"), false);
  assert.equal(Object.hasOwn(backup, "connected"), false);
  assert.equal(Object.hasOwn(backup, "password"), false);
});

test("bodemplaatbackup bewaart alleen geldige generatiegebonden profielen", () => {
  const profiles = normalizeSettingsBackupOduProfiles({
    hp1: {
      variant: 2,
      control_board_item: 0x0E37,
      mode: 3,
      start_temperature_c: 4,
      stop_delta_c: 3,
      auto_reapply: true,
    },
  });
  assert.deepEqual(profiles.hp1, {
    variant: 2,
    control_board_item: 0x0E37,
    mode: 3,
    start_temperature_c: 4,
    stop_delta_c: 3,
    auto_reapply: true,
  });
  assert.throws(() => normalizeSettingsBackupOduProfiles({ hp1: { mode: 4 } }), /bodemplaat/i);
  assert.throws(
    () => normalizeSettingsBackupOduProfiles({ hp1: { ...profiles.hp1, auto_reapply: "true" } }),
    /bodemplaat/i,
  );
});

test("MQTT restore validation requires a valid endpoint", () => {
  assert.throws(() => normalizeSettingsBackupMqttConfig({ enabled: true, broker: "", port: 1883 }), /broker/i);
  assert.throws(() => normalizeSettingsBackupMqttConfig({ enabled: false, broker: "", port: 70000 }), /poort/i);

  const mqtt = normalizeSettingsBackupMqttConfig({
    enabled: true,
    broker: "mqtt.local",
    port: 1883,
    username: "openquatt",
    password_was_set: true,
  });
  assert.equal(settingsBackupMqttNeedsPassword(mqtt), true);
  assert.equal(settingsBackupMqttNeedsPassword({ enabled: false, password_was_set: true }), true);
  assert.equal(settingsBackupMqttNeedsPassword({ enabled: true, password_was_set: false }), false);
});

test("MQTT-dependent source selections are identified before restore", () => {
  assert.equal(isSettingsBackupMqttSourceSelection("roomTempSource", "MQTT"), true);
  assert.equal(isSettingsBackupMqttSourceSelection("coolingDewPointSource", "Dew point (MQTT)"), true);
  assert.equal(isSettingsBackupMqttSourceSelection("heatingEnableSource", "MQTT + Manual"), true);
  assert.equal(isSettingsBackupMqttSourceSelection("outsideTempSource", "Auto"), false);
  assert.equal(isSettingsBackupMqttSourceSelection("flowSource", "MQTT"), false);
});

test("unknown settings are retained as individual restore result items", () => {
  const unknown = collectUnknownSettingsBackupItems({
    system: { known: true, removedSetting: true },
    legacy: { first: 1, second: 2 },
  }, [{ id: "system", keys: ["known"] }]);

  assert.deepEqual(unknown, [
    { section: "system", key: "removedSetting" },
    { section: "legacy", key: "first" },
    { section: "legacy", key: "second" },
  ]);
});
