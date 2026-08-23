#!/usr/bin/env python3
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
SOURCE = (
    ROOT / "components/openquatt_log_history/OpenQuattLogHistory.cpp"
).read_text(encoding="utf-8")
HEADER = (
    ROOT / "components/openquatt_log_history/OpenQuattLogHistory.h"
).read_text(encoding="utf-8")
SNAPSHOT = (
    ROOT / "components/openquatt_log_history/OpenQuattCrashSnapshot.h"
).read_text(encoding="utf-8")
YAML = (ROOT / "openquatt/oq_web_access.yaml").read_text(encoding="utf-8")


def body(signature: str, next_signature: str) -> str:
    start = SOURCE.index(signature)
    end = SOURCE.index(next_signature, start)
    return SOURCE[start:end]


persist = body(
    "bool OpenQuattLogHistory::persist_crash_snapshot_",
    "bool OpenQuattLogHistory::has_pending_crash",
)
assert persist.index("crash_pref_.save") < persist.index("global_preferences->sync")
assert persist.index("global_preferences->sync") < persist.index("crash_pref_.load")
assert persist.index("crash_pref_.load") < persist.index("memcmp")

capture = body(
    "void OpenQuattLogHistory::capture_pending_crash_report_",
    "void OpenQuattLogHistory::retry_pending_crash_persist_",
)
assert capture.index("crash_handler_log") < capture.index("finish(candidate)")
assert capture.index("crash_snapshot_reuse_durable") < capture.index(
    "persist_crash_snapshot_"
)
assert capture.index("persist_crash_snapshot_") < capture.index("if (!persisted)")
assert capture.index("if (!persisted)") < capture.rindex("crash_handler_clear")

retry = body(
    "void OpenQuattLogHistory::retry_pending_crash_persist_",
    "void OpenQuattLogHistory::on_log_",
)
assert retry.index("persist_crash_snapshot_") < retry.index("if (!persisted)")
assert retry.index("if (!persisted)") < retry.index("crash_handler_clear")

setup = body("void OpenQuattLogHistory::setup()", "void OpenQuattLogHistory::loop()")
assert setup.index("history_mutex_ = xSemaphoreCreateMutex") < setup.index(
    "add_log_callback"
)
assert setup.index("entries_.allocate_external") < setup.index("add_log_callback")
assert setup.index("add_log_callback") < setup.index("capture_pending_crash_report_")
assert setup.index("capture_pending_crash_report_") < setup.index(
    "global_web_server_base == nullptr"
)

fingerprint = body(
    "bool OpenQuattLogHistory::fingerprint_crash_candidate_",
    "void OpenQuattLogHistory::consume_crash_log_line_",
)
assert "current_build" not in fingerprint
assert "reset_reason" not in fingerprint
assert "CRASH_SNAPSHOT_ESPHOME_FOREIGN_BUILD" not in fingerprint

consume = body(
    "void OpenQuattLogHistory::consume_crash_log_line_",
    "void OpenQuattLogHistory::capture_pending_crash_report_",
)
assert consume.index("split_log_fields_") < consume.index("copy_sanitized_log_line_")
assert consume.index("copy_sanitized_log_line_") < consume.index("consume(sanitized)")

assert "RawCrashData" not in SOURCE
assert "crash_handler_log()" in SOURCE
assert "crash_handler_clear()" in SOURCE
assert "crash_candidate_" in HEADER
assert "copy_pending_crash" in HEADER
assert "acknowledge_pending_crash" in HEADER
assert "discard_pending_crash" in HEADER
assert "marker_fingerprint" in SNAPSHOT
for key in (
    "build_source_repository",
    "build_source_commit",
    "build_target",
    "build_epoch",
    "firmware_version",
    "release_channel",
    "hardware_profile",
    "topology",
    "connection",
):
    assert f"{key}:" in YAML

print("Crash snapshot lifecycle contract passed.")
