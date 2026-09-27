# Ethernet stop boundary

OpenQuatt's W5500 PHY writes require both a successful Ethernet disable and
delivery of the driver's STOP event. A failed stop or a pending acknowledgement
must never permit PHY reset, power-down or restart. WiFi/provisioning continues
while the existing network-manager retry loop waits for that boundary.

The generic Ethernet lifecycle implementation belongs to ESPHome. The lifecycle
fix is proposed in [ESPHome PR #19734](https://github.com/esphome/esphome/pull/19734).
The profile pins a compatibility backport in `openquatt/connection/wifi_eth.yaml`;
OpenQuatt does not vendor an Ethernet component. The backport uses ESPHome
2026.9.0 Python/codegen and the upstream lifecycle C++ fix. Pinning current
ESPHome `dev` directly would mix incompatible codegen APIs with 2026.9.0.

Remove the external pin only when the configured ESPHome release includes the
lifecycle fix and `is_driver_stopped()`, after reviewing the existing component
version gates and validating the WiFi/W5500 profile.

Ambiguous partial driver failures or a missing STOP event deliberately prevent
PHY writes/restart. Cleanup can allow retry after a real STOP acknowledgement;
otherwise Ethernet recovery requires reboot. This does not identify the cause
of the reported lwIP crash.

`scripts/tests/test_ethernet_lifecycle.py` executes production OpenQuatt manager
methods against mocked public Ethernet API states. Generic driver lifecycle
tests belong upstream. Hardware validation must still check actual IDF event
ordering and repeated interface transitions.
