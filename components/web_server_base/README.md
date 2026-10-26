# OpenQuatt web-auth override

Based on [ESPHome 2026.10.0](https://github.com/esphome/esphome/tree/2026.10.0/esphome/components/web_server_base).
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

Upstream tag: `2026.10.0` (`704551a6c93c29a311802f7ee97d67a9e9bce718`).
The upstream base sources are unchanged from 2026.9.0; the owned credential,
mutex and recovery patches are retained. Upstream `web_server_idf` now respects
the explicit 4096-byte HTTP request header limit in `openquatt/base/common.yaml`,
so the late codegen workaround in `openquatt_web_auth` is no longer required.

The upstream component sources are identical between 2026.10.0b1 and
2026.10.0; the existing local patches are retained.

The build-local four-decimal formatter backport from ESPHome #20800 remains
active on 2026.10.0. The installed final helper sources are checked by the regression
test before use; see ../web_server/README.md for scope and removal criteria.

ESPHome 2026.10.0 includes the 4512-byte HTTPD task stack from
[ESPHome #21038](https://github.com/esphome/esphome/pull/21038). The local
`core_httpd_stack.py` backport and its hook have been removed. The native source
is byte-identical to the generated b6 source with the former backport applied.
Other task stacks are unchanged; a successful build is not runtime memory approval.

ESPHome 2026.10.0 includes the state-SSE JSON lifetime change from
[ESPHome #21109](https://github.com/esphome/esphome/pull/21109), so the local
`core_sse_json.py` backport and its hook have been removed. The upstream
implementation releases the JSON arena before socket I/O and guards generation
against recursive writes on the same session. Task-stack capacities and tail
allocation policy remain unchanged. The remaining build-local formatter hook preserves
that upstream SSE code; the regression test checks their composition against
the installed final sources. The earlier b4 hardware measurements remain historical
evidence and do not replace runtime validation of the final release.
