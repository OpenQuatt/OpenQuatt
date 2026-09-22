# OpenQuatt web_server_base compatibility override

Based on ESPHome **2026.9.0**, `esphome/components/web_server_base/`:
https://github.com/esphome/esphome/tree/2026.9.0/esphome/components/web_server_base

Copyright (c) 2019 ESPHome. C++ runtime: GPLv3 (see ../../LICENSE).
Python: MIT (see LICENSE-MIT). Modified by OpenQuatt on 2026-09-22.

This local external component is loaded by `openquatt/oq_web_access.yaml` for
all OpenQuatt ESP32 targets. Validation rejects other ESPHome versions and
platforms so upgrades require an explicit compatibility review.

Changes relative to upstream:

- Register auth middleware even when credentials have not been configured yet.
- Own credential strings and serialize authentication and updates with a mutex.
- Expose an atomic credential-pair setter for runtime updates.
- Distinguish ordinary open-mode access from an authenticated administrator.

In upstream 2026.9.0, `Credentials::is_set()` tests for a non-null pointer,
not for a nonempty username. The current OpenQuatt bootstrap supplies a pointer;
this change removes registration-order dependence rather than claiming a
reproduced bypass in every current OpenQuatt boot.

The generated startup setters remain available. Runtime callers must use
`set_auth_credentials()` to avoid exposing a mixed username/password pair.
The auth lock is released before calling the wrapped handler: handlers may
change credentials themselves, and holding it across callbacks would deadlock.
Handlers intentionally registered with `add_handler_without_auth()` (the
existing captive portal) retain their separate access policy.

OpenQuattWebAuth serializes recovery/expiry and HTTP login mutations with a
component-state lock and returns string snapshots. The lock order is component
state, then base credentials; no callback holds these in reverse order. Response
snapshots are prepared while locked, and the state lock is released before socket
writes. ODU token accessors return copies as well, avoiding references to temporary
snapshots.

Scope: phase 1 of https://github.com/OpenQuatt/OpenQuatt/issues/421.
This does not implement limited physical recovery, revoke existing SSE sessions,
or cancel already-authorized queued entity actions. Those lifecycle boundaries
must be implemented and tested with the recovery guard before enabling the new
recovery capability. The existing five-second recovery behavior is unchanged.

Validation:

```sh
python3 -m unittest discover -s scripts/tests -p 'test_web_auth_middleware.py'
python3 scripts/dev.py validate --config-only --config configs/heatpump_controller_q/duo_wifi.yaml
npm run check:cpp-format
```

Host tests compile the actual middleware header and implementation against a
small HTTP/ESPHome adapter, including threaded credential replacement. They do
not replace firmware compilation or hardware heap/stack and HTTP validation.
