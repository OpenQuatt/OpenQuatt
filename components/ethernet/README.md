# OpenQuatt Ethernet lifecycle patch

ESP32 sources from ESPHome **2026.9.0** (`esphome/components/ethernet`).
Copyright ESPHome contributors. C/C++ runtime: GPLv3, see the repository
`LICENSE`. Python: MIT, see `../web_server_base/LICENSE-MIT`.
Only the ESP32 implementation used by the WiFi/W5500 profile is included.

- A failed `esp_eth_stop()` leaves the interface enabled; it is not proof
  that the driver stopped.
- An atomic STOP-event barrier blocks PHY writes and restart until the IDF
  event task has processed the stop. The netif glue registers its default
  handlers before the component handler.
- Successful restart clears the previous connection state. Disabled interfaces
  cannot report a stale CONNECTED state while the STOP event is pending.
- OpenQuatt keeps WiFi/provisioning running and uses its existing five-second
  retry interval for stop/wake failures and pending STOP delivery.
- Failed starts attempt driver-stop cleanup. Restart is allowed only after its
  STOP event; an ambiguous cleanup failure requires reboot. IDF changes its FSM
  before fallible start/stop steps and does not roll back those errors.

Recompare against upstream on ESPHome upgrades. The underlying disable error
handling belongs upstream; `is_driver_stopped()` is the local boundary used
by OpenQuatt's W5500 PHY control. A missing STOP event or ambiguous start/stop
failure intentionally prevents PHY writes/restart. HIL must verify actual IDF
event ordering, partial driver failures and repeated interface transitions.
This patch does not establish the cause of the reported lwIP crash.
