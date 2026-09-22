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
