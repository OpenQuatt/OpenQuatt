# Ethernet stop boundary

OpenQuatt's W5500 PHY writes require both a successful Ethernet disable and
delivery of the driver's STOP event. A failed stop or a pending acknowledgement
must never permit PHY reset, power-down or restart. WiFi/provisioning continues
while the existing network-manager retry loop waits for that boundary.

The generic Ethernet lifecycle implementation belongs to ESPHome. The lifecycle
fix is proposed in [ESPHome PR #19734](https://github.com/esphome/esphome/pull/19734).
The profile pins a compatibility backport in `openquatt/connection/wifi_eth.yaml`;
OpenQuatt does not vendor an Ethernet component. The backport uses ESPHome
2026.10.0b1 Python/codegen and the upstream lifecycle C++ fix, including
`on_disconnect` before a deferred restart. The [pinned commit](https://github.com/jeroen85/esphome/commit/6d8b057caec23d8c5216460554b921e3aed420ba)
is based on the exact beta tag and preserves its PSRAM RX path, W5500 SPI driver
and PHY changes. Pinning current ESPHome `dev` directly would mix unreviewed
changes with the selected release.

The Ethernet component sources are identical in 2026.10.0b1 and
2026.10.0b2. The reviewed b1-based lifecycle backport is therefore retained
unchanged with the b2 runtime; no moving upstream branch is introduced.

Remove the external pin only when the configured ESPHome release includes the
lifecycle fix and `is_driver_stopped()`, after reviewing the existing component
version gates and validating the WiFi/W5500 profile.

Ambiguous partial driver failures or a missing STOP event deliberately prevent
PHY writes/restart. Cleanup can allow retry after a real STOP acknowledgement;
otherwise Ethernet recovery requires reboot. This does not identify the cause
of the reported lwIP crash.

`scripts/tests/test_ethernet_lifecycle.py` executes production OpenQuatt manager
methods against mocked public Ethernet API states. Generic driver lifecycle
tests belong upstream. The backport includes failure-injection tests for rejected
stop/start calls, delayed or missing STOP delivery, cancelled restart requests
and disconnect-action reentrancy, with automation triggers enabled and disabled.
Those host checks and an independent cold review do not establish actual IDF
event interleavings or PSRAM allocation margin. Hardware validation must still
check actual event ordering, repeated interface transitions and internal heap /
largest-block margin under simultaneous network load.

### DNS after inactive-interface DHCP startup

ESP-IDF can clear global main/backup DNS when Ethernet DHCP starts, even while
WiFi remains the default route. ESPHome's route check does not repair this when
the route itself is unchanged. The manager checks once per second and restores
only missing main/backup entries from the current default interface's cached
DHCP/static DNS. Valid global entries and fallback DNS are preserved.

Selection, route/up checks, reads and writes run in one synchronous
`esp_netif_tcpip_exec()` operation. No interface pointer or DNS snapshot survives
the operation; DHCP renewal and failover are serialized with the repair. The
repair uses no persistent allocation and leaves driver STOP/PHY guards intact.
It is compiled only with `CONFIG_ESP_NETIF_SET_DNS_PER_DEFAULT_NETIF`.

Run `python3 -m unittest discover -s scripts/tests -p 'test_network_dns_recovery.py'`
for the startup-order, renewal, failover, unavailable-state and retry regression
checks. These host checks establish the repair logic; physical network/OTA
validation is still required to confirm the reported device failure mechanism.
