# Recorder component fixture

`test_debug_recorder_component_runtime.py` compiles `component_test.cpp` together
with the complete production `OpenQuattDebugRecorder.cpp`, its original header,
the generated default field list and the real `openquatt_common/PsramBuffer.h`.
It builds Single and Duo executables once each, then runs isolated scenarios.
There is no copied recorder class, extracted method body or replacement buffer.

`recorder_test_platform.h` adapts only ESPHome/IDF boundaries: entity registries,
clock, external allocator, preferences, web request/response and semaphore APIs.
The forwarding headers supply the include paths required by the production
translation unit. `RAMAllocator` uses host malloc with a nonzero fill, records
internal fallback attempts and can fail any external allocation. The semaphore
adapter uses `std::timed_mutex`, including a second-thread contention check;
blocking calls are not replaced with unconditional success.

| Scenario | Boundaries exercised |
| --- | --- |
| `setup` | Each of nine PSRAM failures, complete buffer release, no internal fallback, missing web server/mutex, unavailable status/download, successful default autostart |
| `lifecycle` | Capture only through `loop()`, zero-uptime first sample, stop/start/restart, real lock contention, opt-out/idempotency, save/sync failure, persisted preference on reboot, eight-hour rolling and millis wrap |
| `configuration` | Single/Duo capabilities, deduplication, immutable previous values, missing source state, real string-pool pressure, eviction/compaction and scratch failure |
| `retention` | Six-hour minimum with the real default schema, sixteen-hour ring rollover, timestamp ranges, immutable snapshots, restart/append/retry interleavings, all snapshot/writer allocation failures, overlapping exports, aborted HTTP, empty recording and HH:MM boundaries |
| `http` | Registered production handler, methods/path boundaries, origin/CSRF rejection before mutation, obsolete timed start, deferred restart, persistence failure and range-argument overflow |

The first three behaviours and the retention/export assertions replace the
former configuration/retention source-extraction harnesses. Capacity and column
indices come from the actual default schema, rather than a reconstructed class
with a hardcoded stride. Jitter samples respect the production ten-second
sampling gate; range membership still uses timestamps rather than row counts.

Run from the repository root with the pinned Python environment active:

```sh
python3 scripts/run_tests.py python --pattern 'test_debug_recorder_*.py'
```

These tests establish host component behaviour. They do not reproduce ESP32
PSRAM placement, heap/stack margins, the FreeRTOS scheduler or actual HTTPD
transport timing. Those properties require physical HIL qualification. No
firmware source or test-only production seam is needed for this fixture.
