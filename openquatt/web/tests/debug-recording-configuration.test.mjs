import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { DEBUG_CONFIGURATION_KEYS, DEBUG_RECORDING_KEYS, ENTITY_DEFS } from "../js/src/core/config.js";

const source = await readFile(new URL("../../../components/openquatt_debug_recorder/OpenQuattDebugRecorder.cpp", import.meta.url), "utf8");
const header = await readFile(new URL("../../../components/openquatt_debug_recorder/OpenQuattDebugRecorder.h", import.meta.url), "utf8");
const powerHouse = await readFile(new URL("../../oq_power_house_strategy.yaml", import.meta.url), "utf8");

test("Toby-context bevat drie instelwaarden, setpoint, comfortband en cyclusdiagnostiek", () => {
  for (const key of ["phRunExtension", "phRunExtensionStopMargin", "phRunExtensionRestartCooldown"]) {
    assert.ok(DEBUG_CONFIGURATION_KEYS.includes(key), key);
  }
  assert.ok(DEBUG_RECORDING_KEYS.includes("roomSetpoint"));
  for (const key of ["phRunExtensionStatus", "phRunExtensionBasePower", "phRunExtensionFloorPower",
    "phRunExtensionState", "phComfortMemory", "phRunExtensionComfortStop", "phRunExtensionWarmRestart"]) {
    assert.ok(DEBUG_RECORDING_KEYS.includes(key), key);
    assert.ok(powerHouse.includes(`name: "${ENTITY_DEFS[key].name}"`), key);
  }
  assert.ok(DEBUG_RECORDING_KEYS.includes("debugStaticSnapshot"));
  assert.match(powerHouse, /cbBelow/);
  assert.match(powerHouse, /cbAbove/);
  assert.match(powerHouse, /run_extension_comfort_stop_c\(\)/);
  assert.match(powerHouse, /run_extension_warm_restart_c\(\)/);
});

test("configuratieallowlist bevat alleen begrensde veilige number/select/switch-velden", () => {
  assert.equal(DEBUG_CONFIGURATION_KEYS.length, 16);
  assert.equal(new Set(DEBUG_CONFIGURATION_KEYS).size, DEBUG_CONFIGURATION_KEYS.length);
  for (const key of DEBUG_CONFIGURATION_KEYS) {
    assert.ok(["number", "select", "switch"].includes(ENTITY_DEFS[key].domain), key);
    assert.doesNotMatch(key, /apiInput|password|token|url|installationId/i);
    assert.ok(Buffer.byteLength(key) < 40);
    assert.ok(Buffer.byteLength(ENTITY_DEFS[key].name) < 48);
  }
  assert.match(header, /configuration_fields_\.is_external\(\)/);
  assert.match(header, /configuration_scratch_\.is_external\(\)/);
  assert.match(source, /configuration_fields_\.allocate_external\(CONFIGURATION_FIELD_CAPACITY\)/);
  assert.match(source, /configuration_scratch_\.allocate_external\(CONFIGURATION_SCRATCH_BYTES\)/);
});

test("snapshot blijft een stringreferentie in zowel firmware- als aangepaste schema's", () => {
  assert.equal((source.match(/add_system_field\("configurationSnapshot"/g) || []).length, 2);
  assert.match(source, /string_type_\(field\.type\) && value != MISSING_VALUE/);
  assert.match(source, /if \(!string_type_\(field\.type\)\)/);
  assert.match(source, /case FieldType::SELECT:\s*case FieldType::CONFIGURATION_SNAPSHOT:/);
  assert.match(source, /configuration_snapshot_overflow/);
  assert.match(source, /#if !OQ_TOPOLOGY_DUO/);
});
