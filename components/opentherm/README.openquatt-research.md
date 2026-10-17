# OpenTherm RX decoder candidate override

This directory vendors the complete ESPHome 2026.8.0 `opentherm` component for
an isolated OpenQuatt RMT receive candidate. Apart from repository formatting,
files not listed below retain the upstream ESPHome 2026.8.0 behavior:

- `__init__.py`: include the ESP-IDF RMT driver;
- `hub.cpp`: service completed RMT frames from normal and synchronous loops;
- `opentherm.cpp` and `opentherm.h`: ESP32 RMT capture, bounded hand-off,
  timeout arbitration and diagnostics;
- `opentherm_rmt_decoder.h`: the host-testable Manchester decoder.

The full component, including the `output` platform, is deliberately present:
ESPHome local external components replace rather than overlay the built-in
component.

Runtime route changes can stop a conversation between Manchester half-bits.
ESPHome 2026.8 now restores the initialized idle-high output level in
`OpenTherm::stop()`; this candidate preserves that upstream behavior around its
additional RMT cancellation. Disabling the hub therefore cannot leave the
digital master output at an asserted half-bit. This is physical fail-silent
behavior; it does not claim that the boiler acknowledged a final CH-off frame.

The [candidate history and transport test reports](https://github.com/OpenQuatt/OpenQuatt/pull/375#issuecomment-6232030897)
are recorded in PR #375. They do not qualify the current firmware. Remaining
qualification limits include real-boiler HIL, prolonged simultaneous OTT+OTB
testing and the rare master-TX/bus/simulator-RX timing event. Prefer an upstream
ESPHome fix followed by removal of this override after compatibility validation.
