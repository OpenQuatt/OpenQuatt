# ESPHome web_server compatibility override

Source: ESPHome 2026.9.0, `esphome/components/web_server`. Copyright ESPHome.
C++ is GPLv3 (repository LICENSE); Python is MIT (../web_server_base/LICENSE-MIT).
Copied upstream files retain the existing component and generated index assets;
C++ is formatted with the OpenQuatt formatter.

The functional change covers `DEFER_ACTION` in `web_server.h` and the direct
switch/lock/infrared/radio deferrals in `web_server.cpp`: capture the
recovery epoch when accepting an entity action, then reject the deferred action
if recovery is active or its epoch changed. Middleware alone cannot revoke
actions already queued for the main loop, even after recovery ends.

The version gate lives in the accompanying `web_server_base` override. When
upgrading ESPHome, compare this small functional patch against the new upstream.

## Temporary SSE lifecycle test fix

`openquatt/oq_web_access.yaml` selects only `web_server_idf` from ESPHome
merge commit `9309cf96b9710ad81d3e01a07a6f76637ba117a1`
([ESPHome #17800](https://github.com/esphome/esphome/pull/17800)).
Compared with 2026.9.0, the C++ changes cover the SSE close lifecycle and URL
storage (`std::string` to `StringRef`). The local `/events` string literal has
the required lifetime and is explicitly wrapped in `StringRef` in `web_server.h`
because the 2026.9.0 conversion constructor is explicit.
Its Python configuration is unchanged. Other components and
the local auth/recovery overrides remain on the existing baseline.

Stalled streams close after 20 seconds without send progress. HTTPD owns the
shutdown, verifies session identity, and retains the response while close work
is queued. Queue failures retry; queued work must never release its lifetime
pin merely because a timeout elapsed. ESPHome 2026.9.0 recommends ESP-IDF 5.5.5;
the upstream fix requires separate queue-work validation with older SDKs.

This is a candidate fix for repeated lwIP/heap crashes, not a proven diagnosis.
Test stalled/non-reading clients, abrupt disconnects, repeated reconnects,
multiple streams and Wi-Fi reconnects with normal controller/API/MQTT load.
Compare baseline and candidate internal free/minimum heap, largest block,
PSRAM and task-stack watermarks; check HTTP/SSE recovery and session headroom.
Remove the pin after upgrading to and validating an ESPHome release containing
the fix. Hardware validation is still required before release.
