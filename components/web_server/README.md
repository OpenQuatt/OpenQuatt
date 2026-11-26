# ESPHome web_server compatibility override

Source: ESPHome 2026.10.0, `esphome/components/web_server`. Copyright ESPHome.
C++ is GPLv3 (repository LICENSE); Python is MIT (../web_server_base/LICENSE-MIT).
OpenQuatt adapts SSE calls and HTTPD socket accounting to ESPHome 2026.11.1.
The auth and recovery overrides are based on the 2026.10.0 sources.
Copied upstream files retain the existing component and generated index assets;
C++ is formatted with the OpenQuatt formatter.

The functional change covers `DEFER_ACTION` in `web_server.h` and the direct
switch/lock/infrared/radio deferrals in `web_server.cpp`: capture the
recovery epoch when accepting an entity action, then reject the deferred action
if recovery is active or its epoch changed. Middleware alone cannot revoke
actions already queued for the main loop, even after recovery ends.

The version gate lives in the accompanying `web_server_base` override. When
upgrading ESPHome, compare this small functional patch against the new upstream.

Upstream tag: `2026.10.0` (`704551a6c93c29a311802f7ee97d67a9e9bce718`).
Controller callbacks are registered by codegen; SSE generators fill the caller's
`JsonBuilder`, including the ESP-IDF stack arena. The OpenQuatt recovery-epoch
checks and atomic firmware `manifest_revision` remain local patches.
The 8 KiB loop stack and internal heap margin require baseline/candidate HIL
validation before this release upgrade is released.

The upstream component sources are identical between 2026.10.0b1 and
2026.10.0; the existing local patches are retained.

## SSE lifecycle compatibility

ESPHome 2026.10 includes the lifecycle fix from
[ESPHome #17800](https://github.com/esphome/esphome/pull/17800). The temporary
2026.9.0-only `web_server_idf` pin is removed on this upgrade branch so the backend
and caller-owned JSON generator API come from the same reviewed release.
The `/events` URL remains a static literal wrapped in `StringRef::from_lit()`.

Stalled streams close after 20 seconds without send progress. HTTPD owns
shutdown and session validation; queued close work retains the response until
its lifetime pin can be released. Queue failures must retry. These guarantees
still need hardware validation under stalls, disconnect/reconnect and ordinary
controller/API/MQTT load. Do not treat the upstream change as a proven diagnosis
of the previously reported lwIP/heap crashes.

## Four-decimal formatter

The ESPHome 2026.10.0 baseline applies the two-file core change
from [ESPHome #20800](https://github.com/esphome/esphome/pull/20800) after ESPHome
copies sources into each build. Only generated `esphome/core/helpers.cpp` and
`helpers.h` are replaced; the installed ESPHome package is not changed. This
works with direct YAML compilation, the local developer helper and CI, including
the native ESP-IDF toolchain. The exact ESPHome version gate also
covers this backport.

The original finite-value/range checks, rounding and fallback are retained;
precision four uses the existing integer formatter and the factor `10000`.
No task-stack allocation, heap policy, Ki range or control behavior changes.
Unexpected or partially patched helper sources stop generation instead of
silently building without the fix. A complete already-patched pair is accepted.
The hook is installed once, while its activation is reset with ESPHome's
per-config `CORE.data`, so later unrelated configs in the same process are not
patched. Source generation restores the upstream files before each application.

The original ESP32-S3 HIL comparison measured cumulative HTTPD stack watermarks
of 160 versus 416 bytes in two paired OTA boots, with 431 direct Ki GETs per
variant and unchanged output. The upstream PR records the host comparisons,
decoded intermittent stack-overflow-detection backtrace, heap variation and
limitations. This is measured stack headroom, not a proven crash fix or release
qualification; power-off cold starts and full worst-case overlap remain open.

ESPHome 2026.11.0 introduced #20800. OpenQuatt uses the upstream formatter.

## Runtime manifest URL ownership

ESPHome 2026.11.0 owns runtime URLs and keeps the active URL alive until
the manifest worker completes. The build-local patch uses that snapshot to
reject results for a superseded URL. It also retains the shared HTTPS slot
for weather, manifest checks and HTTP OTA. A failed manifest allocation or
worker start releases the slot and allows a retry.
