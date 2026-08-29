import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";

globalThis.__OQ_PREVIEW__ = false;

const { DEBUG_RECORDING_KEYS, ENTITY_DEFS } = await import("../js/src/core/config.js");
const [header, source, psramBuffer] = await Promise.all([
  readFile(new URL("../../../components/openquatt_debug_recorder/OpenQuattDebugRecorder.h", import.meta.url), "utf8"),
  readFile(new URL("../../../components/openquatt_debug_recorder/OpenQuattDebugRecorder.cpp", import.meta.url), "utf8"),
  readFile(new URL("../../../components/openquatt_common/PsramBuffer.h", import.meta.url), "utf8"),
]);

test("packed rows vergroten de retentie van de volledige dev-debugset", () => {
  const widths = { binary_sensor: 1, switch: 1, text_sensor: 2, select: 2, sensor: 4, number: 4 };
  const rowBytes = 8 + (5 * 4) + DEBUG_RECORDING_KEYS.reduce(
    (total, key) => total + widths[ENTITY_DEFS[key].domain],
    0,
  );
  const capacity = Math.floor((1024 * 1024) / rowBytes);

  assert.equal(rowBytes, 602);
  assert.equal(capacity, 1741);
  assert.ok((capacity - 1) * 10 >= 4.8 * 60 * 60);
  assert.match(header, /PsramBuffer<uint8_t> samples_/);
  assert.match(source, /value_size_for_type_/);
  assert.match(source, /sample_row_bytes/);
});

test("schema-activatie is transactioneel en laat de actieve opname ongemoeid bij fouten", () => {
  assert.match(header, /PsramBuffer<DebugField> pending_fields_/);
  assert.match(source, /this->fields_\.swap\(this->pending_fields_\)/);
  assert.match(source, /void OpenQuattDebugRecorder::abort_pending_configuration_/);
  assert.match(source, /this->abort_pending_configuration_\(\);/);
  assert.match(psramBuffer, /void swap\(PsramBuffer& other\) noexcept/);
});

test("exports zijn immutable, gesynchroniseerd en falen zonder interne-heapfallback", () => {
  assert.match(source, /xSemaphoreCreateMutexStatic\(&this->state_mutex_storage_\)/);
  assert.match(source, /bool OpenQuattDebugRecorder::capture_snapshot_/);
  assert.match(source, /snapshot->samples\.data\(\)/);
  assert.match(source, /"snapshot_unavailable"/);
  assert.match(header, /mutable bool export_in_progress_/);
  assert.match(source, /const bool can_start = this->available_\(\) && !this->export_in_progress_/);
  assert.match(source, /this->write_recording_export_\(req\)/);
  assert.doesNotMatch(source, /allocate_fallback|heap_caps_malloc\([^\n]*MALLOC_CAP_8BIT/);
});

test("stringopslag recycleert alleen niet-meer-gerefereerde waarden", () => {
  assert.match(header, /uint32_t ref_count/);
  assert.match(header, /string_buckets_/);
  assert.match(header, /string_compaction_order_/);
  assert.match(source, /void OpenQuattDebugRecorder::release_sample_strings_/);
  assert.match(source, /entry\.ref_count == 0/);
  assert.doesNotMatch(source, /std::array<uint16_t, STRING_ENTRY_CAPACITY>/);
  const compaction = source.slice(
    source.indexOf("bool OpenQuattDebugRecorder::compact_strings_()"),
    source.indexOf("uint32_t OpenQuattDebugRecorder::intern_string_"),
  );
  assert.doesNotMatch(compaction, /string_overflow_ = false/);
});

test("mutaties zijn beschermd en status bevat operationele geheugensignalen", () => {
  assert.match(source, /passes_same_origin_/);
  assert.match(source, /passes_csrf_/);
  assert.match(source, /SYSTEM_LARGEST_FREE_HEAP_BLOCK/);
  assert.match(source, /heap_caps_get_largest_free_block\(MALLOC_CAP_INTERNAL\)/);
  assert.match(source, /pending_missing_field_count/);
  assert.match(source, /event_count/);
});

test("rolling totalen laten de overgang vóór de retentiewindow los", () => {
  assert.match(source, /next_oldest/);
  assert.match(source, /write_sample_header_\(next_oldest, sample_offset_\(next_oldest\), 0, 0\)/);
});
