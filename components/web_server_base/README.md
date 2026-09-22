# OpenQuatt web-auth override

Based on [ESPHome 2026.9.0](https://github.com/esphome/esphome/tree/2026.9.0/esphome/components/web_server_base).
Copyright (c) 2019 ESPHome. C++: GPLv3 (../../LICENSE); Python: MIT (LICENSE-MIT).
Modified by OpenQuatt on 2026-09-22. Only ESP32 is supported.

Loaded by `openquatt/oq_web_access.yaml`. Configuration rejects other ESPHome
versions so dependency upgrades require a compatibility review.

The base owns its credential strings and updates/authenticates them under one
mutex. Runtime callers use `set_auth_credentials()` to replace the complete pair.
The ordinary access helper permits open mode; the admin helper requires login.
The middleware always wraps normal handlers; captive portal keeps its explicit
`add_handler_without_auth()` exception.

OpenQuattWebAuth locks its state during recovery and login changes and returns
string copies. Lock order: component state → base credentials. Locks are released
before handler callbacks and socket writes.

Phase 1 of #421. Existing recovery behavior is unchanged. Limited recovery,
SSE revocation, HA/Wi-Fi reset and hardware memory/timing validation remain pending.

Checks: `test_web_auth_middleware.py`, `test_esphome_compatibility_contract.py`,
`npm run check:cpp-format` and the Q Duo Wi-Fi firmware build.
