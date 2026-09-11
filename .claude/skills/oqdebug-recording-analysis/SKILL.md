---
name: oqdebug-recording-analysis
description: Use when asked to inspect, decode, or analyze an OpenQuatt debug recording (`.oqdebug.json`, "debug recording", "opname", export of the device debug buffer) to explain heating behavior — short/rapid compressor cycling ("kort cyclen"), delivered thermal power far above requested power, compressor level/frequency in use, run/stop durations, and why the compressor stops before the room comfort band. Covers the Power House strategy, the heating curve, heat intent, dispatch, and the Modbus compressor-level select.
---

# OpenQuatt debug recording analysis

Analyze `.oqdebug.json` exports (device PSRAM debug buffer) to diagnose heating
behavior: cycle timing, requested vs delivered power, compressor modulation,
and why the unit stops.

## 1. Decode the recording

Run the repo report script first; it turns a `.oqdebug.json` into a readable
markdown summary (recording metadata, system, constraints including the
Opbouw/Afbouw demand ramps derived from the response profile, per-run
statistics and an event timeline):

```bash
python3 scripts/debug_recording_report.py openquatt-debug-recording-*.oqdebug.json > report.md
```

Read `report.md`, then confirm details against the raw columns below when you
need precision or columns the script does not print.

Format: `encoding: device-psram-delta-json-v1`. Top-level keys:
`format`, `schema_version`, `kind`, `encoding`, `exported_at_ms`, `source`,
`recording`, `columns`, `units`, `initial`, `samples`, `events`.

- `recording.startup_at_ms`/`started_at_ms`, `interval_s`, `sample_count`.
- `columns`: list of column name strings (use index into it).
- `units`: **sparse** list of `[column_index, unit_string]` pairs. Never zip it
  directly with `columns` — build `dict(units)` and look up by index.
- `initial`: list of `[column_index, value]` pairs (state at t=0).
- `samples`: list of `[timestamp_seconds, [[column_index, value], ...]]`
  delta rows. Values are absolute changes to apply to the running state; do not
  reset per row. Deltas are stored per-column only when they change.

Decode with a small Python helper (working pattern below runs from the repo
root; this is what the report script uses internally):

```python
import json
data = json.load(open(sys.argv[1]))
cols = data['columns']
units = dict(data['units'])
cur = dict(data['initial'])
rows = []
for s in data['samples']:
    t = s[0]; cur = dict(cur)
    for i, v in s[1]:
        if isinstance(cur.get(i), dict) and isinstance(v, dict):
            m = dict(cur[i]); m.update(v); cur[i] = m
        else:
            cur[i] = v
    rows.append((t, dict(cur)))
```

Always report recording context first: duration, sample count, interval,
`projectVersionText`, `installationTopology`, `hardwareProfileText`, and
`strategy` (column 13). These decide which code the behavior maps to.

## 2. Key columns (indices into `columns`)

Timing / levels:
- 33 `hp1Compressor` — physical1 level currently applied (0 = off)
- 34 `hp1Freq` — compressor frequency in Hz (0 = off, coasting values while spooling down)
- 36 `hp1Power` — electrical input W
- 37 `hp1Heat` — thermal heat output W
- 127–130 `hp1CompressorStarts{2h,6h,24h,72h}` and 121 `compressorCyclingWarning2h`

Strategy (Power House / heating curve):
- 87 `strategyRequestedPower` — requested thermal W handed to the actuator
- 86 `strategyPhaseCode` — 1 = heat, 0 = idle
- 90 `strategyRequestActive`, 84 `requestReason` — why the current request exists (e.g. `ph_idle`, `ph_single_topology`, `inactive_cm`, `inactive_cm_hold`)
- 93 `curveControlProfile` — response profile as recorded (`Calm`/`Balanced`/`Responsive`/`Custom`)
- 88 `strategySupplyTarget` — NAN for Power House (power-based, no supply target)
- 107 `phouseHouse` — modelled house heat loss W (0 at mild outdoor temps)
- 108 `phouseReq` — Power House internal demand W
- 114 `lowLoadDynamicThresholds` — string like `pmin=4958W off=1600W on=2200W` (minimum viable output and low-load cycling thresholds)
- 103 `curveRestartInhibit`, 149 `curveRestartBlockedByRoom` (heating-curve restart lockout)

Environment / comfort:
- 17 `outsideTempSelected`, 18 `roomTemp`, 19 `roomSetpoint`, 20 `supplyTemp`
- Power House comfort bands: default below = 0.1°C, above = 0.3°C
  (`openquatt/oq_power_house_strategy.yaml`, ids `ph_comfort_band_below_c` /
  `ph_comfort_band_above_c`). The room intent threshold is
  `setpoint − comfort_below`.

## 3. Behavior→code map

Each recorded behavior maps to a header that is unit-tested by a matching
`scripts/tests/test_*_contract.py`. Read the header, not just the row values:

| Observation | Source |
|---|---|
| Power House demand (feedforward + reaction, rise/fall rate limit) | `openquatt/includes/control/oq_power_house_demand_logic.h` |
| Dispatch/level selection, idle-vs-run cost, reason codes | `openquatt/includes/control/oq_power_house_dispatch_logic.h` |
| Runtime wiring, `fast_floor_w_`, cadence, demand max | `openquatt/includes/control/oq_power_house_runtime.h` |
| Heat intent (room demand / setpoint raise → forced restart) | `openquatt/includes/control/oq_heat_intent_logic.h` and `..._runtime.h` |
| Compressor candidate / minimum-off limits | `openquatt/includes/control/oq_hp_candidate_logic.h` |
| Model level → physical frequency | `openquatt/includes/performance/hp_perf_frequency.h` (V2 levels: {20,26,30,48,55,61,72,80,85,90} Hz) |
| Physical level select (Modbus reg 1999) | `openquatt/includes/odu/oq_odu_compressor_levels.h`, `openquatt/oq_HP_io.yaml` |
| Actuator minimum runtime / off confirmation | `openquatt/includes/control/oq_thermal_actuator_runtime.h` |
| Heating-curve restart lockout (8 min) and hysteresis | `openquatt/includes/control/oq_heating_curve_logic.h` |

## 4. Analysis workflow

1. **Identify run/stop transitions.** Walk `hp1Compressor` and `hp1Freq`;
   note t of every 0↔1(≥1) edge, run duration, and gap between runs.
2. **Correlate demand.** For each run, compare `strategyRequestedPower`
   (and `phouseReq`) vs `hp1Heat` at steady state. A large gap means the unit
   is modulating at its physical minimum while the strategy asks for far less.
3. **Identify stop reason.** When `strategyPhaseCode` drops to 0 and
   `strategyRequestedPower` → 0, note `requestReason` (`inactive_cm*`) and
   the `lowLoadDynamicThresholds` off/on values. Distinguish:
   - demand naturally decayed to 0 (mild weather, tiny house load) → short-ish run;
   - overheat/water-limit stop → check `strategyWaterTripActive`; 
   - minimum-runtime/lockout hold → `off_confirm_ms`, restart inhibit.
4. **Explain restart.** Check heat intent: with Power House, a room below
   `setpoint − comfort_below` triggers the `fast_floor_w_` mechanism
   (`oq_power_house_runtime.h` line 200) which raises the request to the
   minimum viable thermal W (`pmin` in `lowLoadDynamicThresholds`) to justify
   a run at level 1. This shows up as a sharp `phouseReq`/`strategyRequestedPower`
   jump to ≈ `pmin` followed by a slow, rate-limited decay.
5. **Report the compressor level.** `hp1Compressor` is the physical F-level
   applied via the `hp1_compressor_level` select (Modbus reg 1999). The
   frequency column `hp1Freq` is the **measured** ODU frequency (reg 2103), not
   the demand. Cross-check with
   `oq_perf::model_frequency_hz` / `V2_HEATING_MODEL_FREQUENCIES_HZ` and the
   ODU runtime frequency table:
   - the *model* floor is 20 Hz (level 1 anchor), independent of the unit;
   - the *physical* frequency at any level is decided by the ODU's runtime
     table and its own stepping — there is **no 48 Hz constant in OpenQuatt
     code**, the only `48` is the model level-4 anchor;
   - if the recording shows a higher measured frequency than the web
     Frequentietabel editor reports for that F-level (e.g. measured 48 Hz at
     F1 while the UI table says F1 = 20 Hz), the ODU is not running by its
     reported table — state the measured values and the discrepancy rather than
     asserting a fixed minimum.
6. **Check cycle-limit flags.** Quote `hp1CompressorStarts2h` (10 = capped)
   and `compressorCyclingWarning2h` when the user complains about frequent
   start/stop.

## 5. Common root causes and how to phrase them

- **Delivered ≫ requested**: the unit's minimum physical modulation (the ODU's
  actual lowest F-level, ≈ `pmin`, e.g. ~4900–6000 W at 48 Hz measured on the
  AMH6 under test) exceeds the strategy demand at mild outdoor temp. Excess
  energy heats the buffer (watch `supplyTemp` rising through the run) until
  `strategyRequestedPower` decays to 0 and the actuator stops.
- **Short cycling**: demand model wants near-zero power but intent keeps
  re-requesting; fast_floor forces a minimum run, then the low-load stop
  (`off` threshold) ends it. Revisit after a change to `ph_demand_fall_time_min`
  (clamped to max 10 min in `oq_power_house_demand_logic.h`).
- **Compressor never leaves minimum**: normalized demand `raw_demand` rarely
  exceeds a few steps at mild temps; this is expected, not a fault.

Do not change firmware/control code as part of this analysis unless explicitly
asked. If the user requests a code change, validate the smallest affected
component (contract tests in `scripts/tests/`, and run
`npm run check:cpp-format` for any C/C++ edit).