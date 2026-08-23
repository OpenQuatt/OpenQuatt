# Hardware-in-the-loop tests

`crash_telemetry_q_duo_wifi.yaml` is deliberately unsafe test firmware. It is
not part of `build_targets.yaml` and must never be used as a release target.
Its disabled-by-default diagnostic buttons provide three controlled cases:

- `abort()` for an abort/panic record;
- an invalid pointer write for a real Xtensa fault with raw cause and
  `EXCVADDR`;
- a non-yielding loop for a task-watchdog crash where the active ESP-IDF
  watchdog configuration supports it.

Build it explicitly:

```sh
python3 scripts/dev.py validate --config-only --config tests/hil/crash_telemetry_q_duo_wifi.yaml
esphome compile tests/hil/crash_telemetry_q_duo_wifi.yaml
```

For each case, enable usage statistics, record the current build identity,
press exactly one crash button, and wait for the controller to reboot. Verify
that the local crash report is preserved and that one QoS 1 retained payload
appears on `openquatt/devices/<installation-id>/crash`. Repeat with the broker
offline, with a reboot before PUBACK, and with opt-out while offline. A normal
software reboot and a brownout without an ESPHome crash record must not create
a crash event.

For build-A/build-B validation, trigger a crash with A, boot B before the event
is published, and verify that `captured_build_id` still identifies A. Rebuild A
from its captured GitHub commit and build metadata; only run `addr2line` after
the reconstruction tool confirms that the ELF SHA256 exactly matches
`captured_build_id`.
