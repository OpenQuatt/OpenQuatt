# System Overview

This document explains the current OpenQuatt architecture as implemented in the YAML codebase.

## Table of Contents

- [1. Top-Level Composition](#1-top-level-composition)
- [2. Ownership Model](#2-ownership-model)
- [3. Core Runtime Loops](#3-core-runtime-loops)
- [4. Data Pipeline](#4-data-pipeline)
- [5. Heating Strategy Mechanics](#5-heating-strategy-mechanics)
- [6. Thermal Request Control Mechanics](#6-thermal-request-control-mechanics)
- [7. Flow Control Mechanics](#7-flow-control-mechanics)
- [8. Safety Model](#8-safety-model)
- [9. Hardware Profiles and Pin Strategy](#9-hardware-profiles-and-pin-strategy)
- [10. UI and Observability Organization](#10-ui-and-observability-organization)
- [11. Engineering Notes](#11-engineering-notes)

## 1. Top-Level Composition

OpenQuatt is driven from explicit matrix entrypoints under `configs/`:

- `configs/waveshare/single_wifi.yaml`
- `configs/waveshare/duo_wifi.yaml`
- `configs/heatpump_listener/single_wifi.yaml`
- `configs/heatpump_listener/duo_wifi.yaml`
- `configs/heatpump_controller_q/single_wifi.yaml`
- `configs/heatpump_controller_q/duo_wifi.yaml`

Each entrypoint includes:

- global project/board/framework config from `openquatt/base/common.yaml`
- one connection package from `openquatt/connection/`
- package includes via `openquatt/oq_packages_common.yaml`

Shared runtime services are loaded from `openquatt/oq_common.yaml`, including:

- logging, API, and OTA
- HTTP client and Modbus transport

Package include order is intentional:

1. `oq_common`
2. `oq_supervisory_controlmode`
3. `oq_commissioning`
4. `oq_thermal_limits`
5. `oq_strategy_manager`
6. `oq_cooling_strategy`
7. `oq_heating_curve_strategy`
8. `oq_power_house_strategy`
9. `oq_thermal_request_control`
10. `oq_thermal_actuator`
11. `oq_flow_control`
12. `oq_flow_autotune`
13. `oq_boiler_control`
14. `oq_energy`
15. `oq_cic`
16. `oq_ha_inputs`
17. `oq_local_sensors`
18. `oq_sensor_sources`
19. `oq_webserver`
20. `oq_HP_io` (HP1 always; HP2 only on Duo)
21. `openquatt_incident_manager` (heat-pump incident lifecycle and availability)

This order mirrors data dependencies and ownership boundaries.
Hardware profiles add the matching room/setpoint/heating-enable source selectors. The Heatpump Controller Q profile also includes `oq_ot_slave`; it uses the ESP-IDF RMT-based OpenTherm runtime and is only supported on the Q profile.

## 2. Ownership Model

OpenQuatt follows strict subsystem ownership:

- **Control Mode state machine**: `oq_supervisory_controlmode`
- **Commissioning / service tasks**: `oq_commissioning`
- **Shared heating strategy interface (`oq_heat_mode_code`, `oq_strategy_*`)**: `oq_strategy_manager`
- **Heating-curve demand and compressor requests**: `oq_heating_curve_strategy`
- **Power House demand and compressor requests**: `oq_power_house_strategy`
- **Cooling demand and compressor requests**: `oq_cooling_strategy`
- **Thermal request control**: `oq_thermal_request_control`
- **Safe HP mode/level writes**: `oq_thermal_actuator`
- **Pump iPWM regulation**: `oq_flow_control`
- **Boiler relay control**: `oq_boiler_control`
- **Heat-pump incident lifecycle and availability**: `openquatt_incident_manager`
- **External feed ingest**: `oq_cic`
- **External HA proxy ingest**: `oq_ha_inputs`
- **Local DS18B20 ingest**: `oq_local_sensors`
- **Source selection and selected-source synthesis**: `oq_sensor_sources`
- **Shared runtime services and service entities**: `oq_common`

This prevents hidden control coupling and keeps debugging deterministic.

## 3. Core Runtime Loops

| Subsystem | Interval | Purpose |
|---|---:|---|
| Supervisory | `${oq_supervisory_loop_s}` (default 5s) | Mode decisions, flow interlock, frost logic, power-cap safety net |
| Strategy manager | `${oq_strategy_loop_s}` (default 5s) | Active strategy selection plus shared `oq_strategy_*` interface state |
| Heating curve | `${oq_strategy_loop_s}` plus `${oq_heat_loop_tick_s}` | Curve target generation, PID demand, and curve compressor requests |
| Power House | `${oq_heat_loop_tick_s}` with effective cadence `${oq_ph_demand_loop_s}` | Power model, demand regulation, first-start intent, and Power House compressor requests |
| Cooling | `${oq_heat_loop_tick_s}` | Cooling target, PI demand, and cooling compressor requests |
| Thermal request control | Tick `${oq_heat_loop_tick_s}` (default 5s), effective cadence `${oq_heat_loop_curve_s}` (Curve) / `${oq_heat_loop_powerhouse_s}` (Power House), with immediate evaluation on mode or strategy-request changes | Shared request control, guards, and actuator input |
| Flow control | `${oq_flow_loop_s}` (default 5s) | Pump iPWM control (AUTO/MANUAL/FROST/CM100 autotune override) |
| Boiler control | `${oq_boiler_loop_s}` (default 5s) | CM3 assist, CM4 fault fallback and CM100 boiler test under shared safety guards |
| HP incident manager | component loop plus fresh HP observations | Debounce, incident lifecycle, HP availability, start/stop confirmation and CM4 eligibility |
| CIC polling tick | `${cic_poll_tick_ms}` (default 5s) | Poll scheduler, stale detection, feed invalidation |

### 3.1 Boot and first-run timing

Configured startup delays are relative to the ESPHome scheduler becoming active. Network association, restored-state callbacks and physical bus responses make their observed wall-clock time variable.

| Subsystem | First configured activity after scheduler start | Steady cadence / gate |
|---|---:|---|
| HP1 Modbus | `${oq_modbus_startup_delay_ms}` (default 0ms) | Base poll every `${oq_modbus_update_interval_s}` (default 5s), commands throttled by `${oq_modbus_command_throttle_ms}` (default 500ms) |
| HP2 Modbus (Duo) | 2500ms | Same base cadence; the offset intentionally stages HP1 and HP2 traffic |
| OpenTherm thermostat slave (OTT) | Component setup | Runtime validation every 2s |
| OpenTherm boiler master (OTB) | Enabled at late boot for normal OpenTherm control, or temporarily for the bounded R1 startup verification | R1 remains unavailable until the safe `STATUS(CH=off)` probe completes; a matching response latches a connection mismatch and keeps both R1 and OpenTherm CH off. Normal link/freshness checks start after 2s and run every `${oq_otb_link_watch_s}` (default 1s) |
| CIC | First scheduler tick after `${cic_poll_tick_ms}` (default 5s) | Fetching only runs when CIC polling is enabled |
| MQTT usage statistics | 90s after setup-complete, opt-in, broker configuration and network gates are all satisfied | Publishes every 1h; a boot-time network loss restarts the 90s delay |
| Firmware manifest | `${oq_firmware_initial_check_delay_s}` (default 300s) of continuously available network without an active OTA, sampled every 5s | Automatic checks every `${oq_firmware_periodic_check_interval}` (default 4h); manual checks and real runtime channel/target changes remain immediate |

These offsets spread network and bus work; they are not readiness guarantees. A successful Modbus or OpenTherm exchange can only occur once the corresponding external equipment is connected and responsive.

## 4. Data Pipeline

### 4.1 Input layer

- HP telemetry and status from `oq_HP_io` (Modbus registers)
- CIC cloud feed from `oq_cic`
- Home Assistant proxy inputs from `oq_ha_inputs`
- Local DS18B20 from `oq_local_sensors`

### 4.2 Source abstraction layer

`oq_sensor_sources` produces selected control inputs:

- `water_supply_temp_selected`
- `flow_rate_selected`
- `outside_temp_selected`
- `room_temp_selected`
- `room_setpoint_selected`

Runtime selectors decide per signal whether selected values come from local, CIC, or HA-input sources.

`water_supply_temp_selected` first uses the configured source and then a fresh heat-pump outlet fallback. If neither is
available, it briefly holds the last value from that exact source: 15 seconds for Local/CIC sensor dropouts and 300
seconds for HA input so a Home Assistant restart does not interrupt control. Any source change clears the held value;
after the applicable timeout the signal returns to the existing `NaN` fail-safe path.

### 4.3 Demand layer

Strategy packages compute:

- `oq_demand_raw` (`0..20`) for the selected heating strategy
- `oq_cooling_demand_raw` (`0..20`) for cooling
- explicit `oq_strategy_*` status for downstream diagnostics and supervisory logic

### 4.4 Thermal request layer

`oq_thermal_request_control` computes:

- `oq_demand_filtered`
- HP level requests and applied levels
- shared `oq_P_hp_cap_w` and `oq_P_deficit_w` diagnostics for strategy paths that use them

### 4.5 Supervisory and safety layer

`oq_supervisory_controlmode` resolves:

- `oq_control_mode` / `oq_control_mode_code`
- low-flow fault timing and state
- power cap factor (`oq_power_cap_f`)
- silent window state
- CM4 boiler-only fallback after confirmed heat-pump unavailability and safe stop

The incident manager consumes raw heat-pump telemetry separately for HP1 and
HP2. It classifies register bits as status, protection, warning or fault, and
derives effects such as display-only, capacity limit, start block, stop request
and CM4 eligibility. Each incident retains active/latched state, first and last
occurrence, recovery condition, user action and affected HP.

### 4.6 Actuation layer

- Compressor level writes via HP select entities
- Pump iPWM writes to HP1 (and HP2 on Duo control paths)
- Boiler relay writes via GPIO output

### 4.7 Telemetry and energy layer

`oq_energy` derives:

- electrical energy daily/total
- heat pump thermal energy daily/total
- boiler thermal energy daily/total
- system thermal energy daily/total
- daily heat pump COP metric

### 4.8 Service and diagnostics layer

`oq_common` and `oq_thermal_request_control` provide:

- firmware update entities, runtime update-channel select, and manual check trigger
- runtime logger level controls
- runtime balancing service entities from thermal request control (`Runtime lead HP`, runtime counter reset)

## 5. Heating Strategy Mechanics

### Power House mode

Computes requested power from:

- outdoor temperature model (`Tc`, `T0`, `Pr`)
- room error below the cold comfort edge and warm-side pullback above room setpoint
- response profile plus rise/fall times scaled to rated house power (`Pr`)

Then maps requested power to demand scale `0..20`.

Power House stability guards in supervisory:

- short start confirmation before a new Power House heating request may leave idle
- dynamic low-load thresholds from performance map level-1 thermal power (`pmin/off/on`)
- last-known-good dynamic thresholds across short telemetry dropouts
- internal fallback low-load OFF/ON thresholds when dynamic input is unavailable
- low-load heat-request latch (OFF/ON hysteresis on `P_req`)
- temporary CM2 re-entry block after CM2 idle-exit trip
- CM2 startup-grace and high-load guard on idle-exit path
- shared water-temperature limiter on effective `P_req` using `water_supply_temp_selected`
- per-compressor minimum runtime once a compressor has started (user-tunable, lower bound `300 s`)

### Water Temperature Control mode

Uses:

- heating-curve interpolation to derive supply target
- PID climate loop to track supply temperature
- PID output mapped to demand `0..20`
- coarse curve phase (`HEAT`/`COAST`/`OFF`) plus detailed operating regime (`RECOVERY`/`MAINTAIN`)

When PID SP/PV is invalid, demand falls back to 0 and integral is reset.

Heating-curve stability guards around zero-demand edge:

- profile-based outside-temperature smoothing and target quantization
- start/stop gating with OFF-confirmation and low-PID requirement
- near-target `COAST` phase and low-load operating regime (instead of immediate drop to `0`)
- room-temperature coupling trims supply target when room drifts warm
- target clamp at `Maximum water temperature`
- explicit per-HP slew-rate limiting with slower up and faster down behavior
- in heating-curve mode: single-HP-first allocation with dual-enable hysteresis and sequential HP step changes

## 6. Thermal Request Control Mechanics

`oq_thermal_request_control` enforces, in order:

1. demand normalization and clamp
2. power cap clamp (`oq_power_cap_f`)
3. Control Mode gating (CM2/CM3 only; CM4 always requests zero HP output)
4. strategy-specific level logic
5. allowed-level switch constraints
6. min-runtime stop blocking (all strategies)
7. write-on-change application and runtime counters

Power House uses `Power House demand rise time` as its single upward rate limiter. A validated room-demand or
thermostat-raise first start may temporarily request the lowest viable capacity; normal watt regulation resumes after
the compressor starts.

Power House duo request selection works in simple steps:

- compare the best valid single-HP and dual-HP candidates separately
- prefer the topology with the lower electrical input by default
- allow a less-efficient topology only when it has a clear heat-match advantage
- keep the current combination unless a switch gives clear heat or power benefit
- after a recent single<->duo change, keep the current topology a bit longer if the alternative gives only a small advantage
- if two single-HP options are equally good, choose the runtime lead HP
- defrost derating now follows the real `4-Way valve` phase, and extra compensation is only added if the chosen combination would otherwise still underdeliver

## 7. Flow Control Mechanics

Flow control execution priority:

1. CM0 early return
2. autotune override (CM100 commissioning task only)
3. manual/frost fixed iPWM
4. AUTO PI path

Key behaviors:

- startup hold phase with integrator freeze
- setpoint ramping
- asymmetric action limits
- validity-based failsafe (`iPWM=850`)
- automatic selection between the normal flow setpoint and the cooling flow setpoint in CM5
- separate stable-flow tracking for normal and cooling `last_good_pwm`

## 8. Safety Model

Safety is distributed but coordinated:

- flow safety and CM gating in supervisory
- incident debounce prevents a short communication dip from stopping a running HP or triggering CM4
- confirmed HP faults can block a new start, request a stop and mark only the affected HP unavailable
- Duo fallback requires that no HP remains available and that every unavailable HP has an explicitly allowed fallback cause
- CM4 entry requires confirmed HP stop plus valid flow, supply temperature, boiler permission and temperature guards
- compressor-zero enforcement outside CM2/CM3 in thermal request control
- shared water-temperature limiter/trip across strategy manager, thermal request control, and boiler control
- stale feed invalidation in CIC ingest
- conservative fallback on invalid numeric inputs

CM3 is normal boiler assistance; CM4 is boiler-only fault fallback.
Changing between those roles must not toggle the physical relay or the
OpenTherm CH-enable output while the output safety guards remain unchanged.

## 9. Hardware Profiles and Pin Strategy

Hardware profile substitutions are split into dedicated files:

- `openquatt/profiles/waveshare.yaml` ([Waveshare ESP32-S3-Relay-1CH](https://www.waveshare.com/esp32-s3-relay-1ch.htm))
- `openquatt/profiles/heatpump_listener.yaml` ([Electropaultje Heatpump Listener](https://electropaultje.nl/product/heatpump-listener/))
- `openquatt/profiles/heatpump_controller_q.yaml` ([Electropaultje Heatpump Controller Q-edition](https://electropaultje.nl/product/heatpump-controller-q-edition/))

Shared non-hardware constants are in `openquatt/oq_substitutions_common.yaml`.

Compile-time profile selection is done by choosing a matrix entrypoint from `build_targets.yaml`. Ethernet targets are enabled for the Heatpump Controller Q as separate Ethernet-only builds.
OpenTherm thermostat support is part of the Heatpump Controller Q profile only. Waveshare and Heatpump Listener builds expose CIC, Home Assistant, and MQTT source paths instead.

### 9.1 Memory and flash expectations

All active hardware profiles configure PSRAM with `ignore_not_found: false`. OpenQuatt therefore treats PSRAM as required hardware for supported profiles, not as an optional acceleration path. Missing PSRAM should surface as a hardware or profile mismatch instead of silently degrading Trends and LogHistory behavior.

#### Runtime heap discipline

PSRAM capacity does not replace internal DRAM headroom. Wi-Fi, lwIP, TLS, ESP-MQTT, DMA and several FreeRTOS operations still require internal memory, sometimes as one contiguous allocation. Large long-lived state, histories, payloads and response scratch buffers should therefore use explicit PSRAM ownership where their execution context permits it. ISR/DMA data, cache-disabled paths and platform-restricted network task stacks must remain in compatible internal memory.

Heap diagnostics have distinct meanings:

- current internal free heap describes one moment;
- minimum internal free heap is the cumulative low watermark since boot;
- largest internal free block shows whether a contiguous allocation can still succeed;
- fragmentation is derived from the gap between total free internal heap and that largest block;
- task-stack high-watermarks show the unused stack at the worst observed point.

A low minimum watermark is not by itself a leak, but it proves that the firmware entered that allocation state at least once. Static linker RAM totals also cannot reveal transient task stacks, TLS buffers or overlapping network allocations.

Memory-sensitive changes require an identical baseline/candidate comparison with cold-boot checkpoints and realistic combined load. Measurements should cover the applicable HA, web, API, MQTT, Modbus, OpenTherm and OTA/flash paths without aggressive polling that distorts the heap. After a temporary operation, current heap and largest-block values should recover without a continuing downward trend.

OpenQuatt treats allocation failures, unexplained regressions or a minimum/largest-block result without measured margin for the worst simultaneous allocation as release-blocking. Healthy HIL baselines should be maintained per hardware profile and converted into explicit profile budgets; one global heap number is not sufficient. Prefer targeted PSRAM placement, bounded reusable buffers and persistent workers over a global malloc-threshold change. When strict PSRAM allocation fails, the feature must degrade explicitly and safety-relevant control must fail closed.

Persistent histories use separate, sector-aligned regions inside `openquatt_data`. The shared layout is defined in
`components/openquatt_common/OpenQuattFlashLayout.h` so one archive cannot silently grow into another:

| Archive | Maximum reserved space | Contents |
|---|---:|---|
| Trends | 360 KiB | 720 hourly blocks, up to 30 days |
| Energy day totals | 1024 KiB | Long-term daily records |
| Energy hour detail | 368 KiB | Up to 365 retained day records |
| Decision log | 128 KiB | Up to 5120 exact events, limited to 7 days |

The decision log keeps exact events in PSRAM and normally writes new records to flash in hourly batches.
Safety-relevant incident, HP-availability, mode and fallback transitions request an earlier coalesced flash write.
Each compact flash
record is 24 bytes and retains its timestamp, type, source, reason, mode, transition values and duration. The smallest
`openquatt_data` partition is 1920 KiB; all maximum archive regions together use 1880 KiB and therefore leave 40 KiB
unassigned. The 16 MB table leaves 4136 KiB unassigned. The decision-log region is checked at compile time against the
smallest partition and at runtime against the partition actually present.


## 10. UI and Observability Organization

The OpenQuatt SPA owns the layout of built-in entities. Built-in packages deliberately do not attach ESPHome
`sorting_group_id` or `sorting_weight` metadata: ESPHome stores one dynamic map entry for every expanded entity
assignment, while the OpenQuatt SPA does not consume that metadata.

`oq_webserver.yaml` therefore defines no ESPHome sorting groups. The native ESPHome fallback remains functional but
uses its default order. Home Assistant entity mapping is unaffected.

## 11. Engineering Notes

- Keep ownership boundaries intact when refactoring.
- Preserve entity IDs unless migration is documented.
- Keep loop intervals and hysteresis semantics stable unless retuning intentionally.
- Always run config + compile validation after structural changes.
