import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;

const { DEBUG_RECORDING_KEYS, ENTITY_DEFS } = await import("../js/src/core/config.js");
const [requestControl, supervisory, powerHouse, recorderSource] = await Promise.all([
  readFile(new URL("../../oq_thermal_request_control.yaml", import.meta.url), "utf8"),
  readFile(new URL("../../oq_supervisory_controlmode.yaml", import.meta.url), "utf8"),
  readFile(new URL("../../oq_power_house_strategy.yaml", import.meta.url), "utf8"),
  readFile(new URL("../../../components/openquatt_debug_recorder/OpenQuattDebugRecorder.cpp", import.meta.url), "utf8"),
]);

const CHAIN_KEYS = [
  "hp1RequestedControlLevel",
  "hp1AppliedControlLevel",
  "hp1TableFrequency",
  "hp2RequestedControlLevel",
  "hp2AppliedControlLevel",
  "hp2TableFrequency",
  "phFastIntentCode",
  "lowLoadLatch",
  "lowLoadPminW",
  "lowLoadOffW",
  "lowLoadOnW",
  "debugStaticSnapshot",
];

test("V2-ketenvelden zijn compacte numerieke kolommen met delta-encoding", () => {
  const widths = { binary_sensor: 1, switch: 1, text_sensor: 2, select: 2, sensor: 4, number: 4 };
  for (const key of CHAIN_KEYS) {
    assert.ok(ENTITY_DEFS[key], `entitydefinitie ontbreekt voor ${key}`);
    assert.ok(key.length < 40, `debugsleutel past niet in DebugField.key: ${key}`);
    assert.ok(ENTITY_DEFS[key].name.length < 48, `entitynaam past niet in DebugField.name: ${key}`);
  }
  assert.equal(ENTITY_DEFS.lowLoadLatch.domain, "binary_sensor");
  assert.equal(ENTITY_DEFS.debugStaticSnapshot.domain, "text_sensor");
  for (const key of CHAIN_KEYS.filter((k) => k !== "lowLoadLatch" && k !== "debugStaticSnapshot")) {
    assert.equal(ENTITY_DEFS[key].domain, "sensor");
  }
  // 10 sensors (4B) + 1 binary (1B) + 1 text (2B) = 43B extra op de packed row.
  const extra = CHAIN_KEYS.reduce((total, key) => total + widths[ENTITY_DEFS[key].domain], 0);
  assert.equal(extra, 43);
  // Statische snapshot telt niet als statuswijziging (net als lowLoadDynamicThresholds).
  assert.match(recorderSource, /std::strcmp\(field\.key, "debugStaticSnapshot"\) != 0/);
});

test("startsnapshot bevat tabellen, hash en instellingen eenmalig in initial", () => {
  for (const key of CHAIN_KEYS) {
    assert.ok(DEBUG_RECORDING_KEYS.includes(key), `debugset mist ${key}`);
  }
  assert.deepEqual(DEBUG_RECORDING_KEYS.slice(-CHAIN_KEYS.length), CHAIN_KEYS);
  assert.match(powerHouse, /id: oq_debug_static_snapshot/);
  assert.match(powerHouse, /name: "Debug static snapshot"/);
  assert.match(powerHouse, /hp1/);
  assert.match(powerHouse, /house/);
  assert.match(powerHouse, /minOffS/);
  assert.match(powerHouse, /extStaleS/);
  assert.match(powerHouse, /table_hash/);
  assert.match(powerHouse, /2166136261U/);
});

test("wijzigingssnapshot verschijnt alleen bij daadwerkelijke wijziging", () => {
  // Snapshot leest live tabellen/instellingen zonder tijd- of randombron;
  // de bestaande delta-encoding (alleen gewijzigde [index, waarde] in samples)
  // zorgt dat initial de startwaarde draagt en samples alleen deltas.
  assert.doesNotMatch(powerHouse, /oq_debug_static_snapshot[\s\S]{0,2000}millis\(\)/);
  assert.match(recorderSource, /if \(value == read_value_\(previous, field\)\) continue/);
  assert.match(recorderSource, /"initial":\[/);
  assert.match(recorderSource, /"samples":\[/);
});

test("ontbrekende of ongeldige runtime-tabel degradeert veilig zonder verzonnen Hz", () => {
  assert.match(requestControl, /if \(physical == 0\) return 0\.0f/);
  assert.match(requestControl, /if \(physical < 0 \|\| !snapshot\.heating\.valid\) return NAN/);
  assert.match(requestControl, /frequency_for_physical_level\(snapshot\.heating, physical\)/);
  assert.match(requestControl, /return hz > 0 \? \(float\) hz : NAN/);
  assert.match(powerHouse, /valid/);
  assert.match(powerHouse, /if \(!table\.valid\) return 0U/);
  assert.doesNotMatch(requestControl, /hp1_table_frequency_sensor[\s\S]{0,400}900\.0f/);
  assert.doesNotMatch(requestControl, /hp1_table_frequency_sensor[\s\S]{0,400}1300\.0f/);
});

test("V2-mapping gebruikt de heating-tabel voor fysieke F-levels", () => {
  assert.match(requestControl, /decode_runtime_frequency_snapshot\(id\(hp1_runtime_frequency_snapshot_storage\)\)/);
  assert.match(requestControl, /snapshot\.heating\.valid/);
  assert.match(requestControl, /frequency_for_physical_level/);
  assert.match(powerHouse, /decode_runtime_frequency_snapshot\(id\(hp1_runtime_frequency_snapshot_storage\)\)/);
  assert.match(powerHouse, /table\.hz\[i\]/);
  // Fysiek F-level zelf blijft via de bestaande HPx compressor level-kolom
  // beschikbaar; gemeten Hz via HPx - Compressor frequency (geen doublure).
  assert.ok(DEBUG_RECORDING_KEYS.includes("hp1Compressor"));
  assert.ok(DEBUG_RECORDING_KEYS.includes("hp1Freq"));
  assert.ok(!CHAIN_KEYS.includes("hp1Compressor"));
});

test("Duo legt HP2 vast; Single degradeert HP2 veilig naar null", () => {
  assert.match(requestControl, /#if OQ_TOPOLOGY_DUO/);
  assert.match(requestControl, /id\(\$\{secondary_last_applied_level_id\}\)/);
  assert.match(requestControl, /id\(\$\{secondary_runtime_frequency_snapshot_storage_id\}\)/);
  assert.match(requestControl, /id\(\$\{secondary_last_commanded_physical_level_id\}\)/);
  assert.match(powerHouse, /hp2/);
  assert.match(powerHouse, /"null"/);
  assert.match(powerHouse, /#if OQ_TOPOLOGY_DUO/);
  for (const key of ["hp2RequestedControlLevel", "hp2AppliedControlLevel", "hp2TableFrequency"]) {
    assert.ok(ENTITY_DEFS[key].optional !== false, `${key} moet missing-safe zijn`);
    assert.ok(DEBUG_RECORDING_KEYS.includes(key));
  }
  assert.ok(ENTITY_DEFS.debugStaticSnapshot.optional !== false);
});
