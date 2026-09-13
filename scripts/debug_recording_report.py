#!/usr/bin/env python3
"""Render a device debug-recording export (.oqdebug.json) as readable markdown.

The recording format is a delta/forward-filled series, so this script
reconstructs the full state per sample before reporting.
"""

from __future__ import annotations

import argparse
import json
import sys
from datetime import datetime, timezone
from pathlib import Path


def load_recording(path: Path) -> dict:
    try:
        data = json.loads(path.read_text("utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise SystemExit(f"cannot read recording: {exc}")
    if not isinstance(data, dict):
        raise SystemExit("recording root is not a JSON object")
    return data


def build_state(data: dict) -> list[tuple[int, dict]]:
    """Return [(t_seconds, state_dict)] with full forward-filled state per sample.

    Values may be scalar, string, or nested dict (deltas merge into the
    previous dict for the same column).
    """
    initial = data.get("initial") or []
    state = {}
    for index, value in initial:
        state[index] = value

    rows: list[tuple[int, dict]] = [(0, dict(state))]
    for sample in data.get("samples") or []:
        timestamp = int(sample[0])
        for index, value in sample[1]:
            previous = state.get(index)
            if isinstance(previous, dict) and isinstance(value, dict):
                merged = dict(previous)
                merged.update(value)
                state[index] = merged
            else:
                state[index] = value
        # A sample may carry the t=0 row again; treat it as a duplicate of the
        # initial state so the timeline does not repeat the first row.
        if timestamp == rows[-1][0]:
            rows[-1] = (timestamp, dict(state))
        else:
            rows.append((timestamp, dict(state)))
    return rows


def format_timestamp(value: object) -> str:
    if not isinstance(value, (int, float)):
        return "—"
    try:
        return datetime.fromtimestamp(value / 1000.0, tz=timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
    except (OverflowError, OSError, ValueError):
        return str(value)


def human_duration(total_seconds: float | None) -> str:
    if total_seconds is None or total_seconds < 0:
        return "—"
    seconds = int(round(total_seconds))
    if seconds < 60:
        return f"{seconds} s"
    minutes, remainder = divmod(seconds, 60)
    if minutes < 60:
        return f"{minutes} min {remainder:02d} s"
    hours, minutes = divmod(minutes, 60)
    return f"{hours} h {minutes:02d} min {remainder:02d} s"


def clock_label(total_seconds: float) -> str:
    seconds = int(round(total_seconds))
    if seconds < 0:
        return "—"
    hours, remainder = divmod(seconds, 3600)
    minutes, secs = divmod(remainder, 60)
    return f"{hours:02d}:{minutes:02d}:{secs:02d}"


# Column name -> (section order, label used in report). Missing columns are
# silently skipped so the report stays valid across topologies/modes.
# Power House response profiles map to demand rise/fall ramps (Opbouw/Afbouw);
# values match the web UI (web/js/src/settings/heating.js).
RESPONSE_PROFILE_TIMES_MIN = {
    "Calm": (12, 5),
    "Balanced": (8, 3),
    "Responsive": (5, 2),
}
COLUMN_GROUPS = {
    "system": {
        "projectVersionText": "Software",
        "releaseChannelText": "Release channel",
        "installationTopology": "Topology",
        "hardwareProfileText": "Hardware profile",
        "hardwareRevisionText": "Hardware revision",
        "connectionText": "Connection",
    },
    "hp": {
        "hp1Generation": "HP1 generation",
        "hp1GenerationVariant": "HP1 variant",
        "hp1CustomerModelCode": "HP1 model code",
        "hp2Generation": "HP2 generation",
        "hp2GenerationVariant": "HP2 variant",
        "hp2CustomerModelCode": "HP2 model code",
    },
    "constraints": {
        "strategy": "Strategy",
        "controlModeLabel": "Control mode",
        "phResponseProfile": "Power House response profile",
        "curveControlProfile": "Heating curve profile",
        "phComfortBelow": "Comfort below setpoint",
        "phComfortAbove": "Comfort above setpoint",
        "phDemandRiseTime": "Opbouwtijd (demand rise)",
        "phDemandFallTime": "Afbouwtijd (demand fall)",
        "demandSource": "Demand source",
        "heatingEnableSource": "Heating enable source",
        "heatingEnableSelected": "Heating enable selected",
        "heatingEnableValid": "Heating enable valid",
        "externalHeatDemandSource": "External heat demand source",
        "externalHeatDemandSelected": "External heat demand",
        "powerHouseDemandSource": "Power House demand source",
        "lowLoadDynamicThresholds": "Low-load dynamic thresholds",
        "lowLoadDynamicOffFactor": "Low-load off factor",
        "lowLoadDynamicOnFactor": "Low-load on factor",
        "lowLoadMinimumHysteresis": "Low-load minimum hysteresis",
        "silentModeOverride": "Silent mode override",
        "silentActive": "Silent active",
        "manualCoolingEnable": "Manual cooling enable",
    },
    "curve": {
        "curveM20": "Curve @ -20 °C",
        "curveM10": "Curve @ -10 °C",
        "curve0": "Curve @ 0 °C",
        "curve5": "Curve @ 5 °C",
        "curve10": "Curve @ 10 °C",
        "curve15": "Curve @ 15 °C",
        "heatingCurvePidKp": "Heating curve PID Kp",
        "heatingCurvePidKi": "Heating curve PID Ki",
        "heatingCurvePidKd": "Heating curve PID Kd",
        "flowControlMode": "Flow control mode",
        "flowKp": "Flow PID Kp",
        "flowKi": "Flow PID Ki",
        "flowSetpoint": "Flow setpoint",
    },
    "boiler": {
        "otbMaxCapacity": "OTB max capacity",
        "otbMinModulation": "OTB min modulation",
        "boilerCommandTargetTemperature": "Boiler target temperature",
        "boilerBlockReason": "Boiler block reason",
    },
}

# Dynamic columns shown in the event timeline.
EVENT_COLUMNS = {
    "controlModeLabel": "mode",
    "strategyPhaseCode": "phase",
    "strategyRequestActive": "reqActive",
    "hp1Compressor": "physLvl",
    "hp1Freq": "freqHz",
    "hp1Power": "pInW",
    "hp1Heat": "heatW",
    "strategyRequestedPower": "reqW",
    "requestReason": "reason",
    "roomTemp": "Troom",
    "roomSetpoint": "Tset",
    "supplyTemp": "Tsup",
    "outsideTempSelected": "Tout",
    "hp1Defrost": "defrost",
    "phouseReq": "phouseReq",
    "hpCapacity": "hpCap",
    "hp1WaterIn": "Twin",
    "hp1WaterOut": "Twout",
}

# Runs detected from this level being > 0.
def compression_level(state: dict, index: object) -> int:
    value = state.get(index)
    if isinstance(value, (int, float)):
        return int(value)
    return 0


def render_recording_section(data: dict) -> list[str]:
    lines = ["## Recording", ""]
    lines.append(f"- Format: `{data.get('format', '—')}` (schema v{data.get('schema_version', '—')}, "
                 f"encoding `{data.get('encoding', '—')}`)")
    lines.append(f"- Kind: `{data.get('kind', '—')}`")
    lines.append(f"- Exported: {format_timestamp(data.get('exported_at_ms'))}")
    source = data.get("source") or {}
    if isinstance(source, dict):
        lines.append(f"- Source: device `{source.get('device', '—')}`, storage `{source.get('storage', '—')}`")
    recording = data.get("recording") or {}
    if isinstance(recording, dict):
        lines.append(f"- Recording: `{recording.get('recording_id', '—')}`")
        lines.append(f"- Started: {format_timestamp(recording.get('started_at_ms'))}")
        lines.append(f"- Ended: {format_timestamp(recording.get('ended_at_ms'))}")
        lines.append(f"- Duration: {human_duration(recording.get('duration_s'))}")
        lines.append(f"- Interval: {recording.get('interval_s', '—')} s")
        lines.append(f"- Samples: {recording.get('sample_count', '—')} / "
                     f"{recording.get('sample_capacity', '—')} capacity")
        for key in ("active", "frozen", "rolling", "mode"):
            if recording.get(key) is not None:
                lines.append(f"- {key}: {recording.get(key)}")
        missing = recording.get("missing_field_count")
        if missing:
            lines.append(f"- Missing fields: {missing}")
        overflow = recording.get("string_overflow")
        if overflow:
            lines.append(f"- String overflow count: {overflow}")
    columns = data.get("columns") or []
    lines.append(f"- Columns: {len(columns)}")
    lines.append("")
    return lines


def render_section(columns: list[str], index_of: dict[str, int], state: dict, group: str) -> list[str]:
    labels = COLUMN_GROUPS[group]
    rows: list[str] = []
    for name, label in labels.items():
        index = index_of.get(name)
        if index is None or index not in state:
            continue
        value = state[index]
        if value is None or (isinstance(value, float) and value != value):  # NaN
            value = "—"
        rows.append(f"| {label} | `{value}` |")
    return rows


def render_named_sections(columns: list[str], index_of: dict[str, int], state: dict) -> list[str]:
    titles = {
        "system": "System",
        "hp": "Heat pump",
        "constraints": "Constraints & settings",
        "curve": "Heating curve strategy",
        "boiler": "Boiler (OpenTherm bridge)",
    }
    lines: list[str] = []
    for group, title in titles.items():
        rows = render_section(columns, index_of, state, group)
        # Add Power House demand ramp times derived from the response profile.
        if group == "constraints":
            profile_index = index_of.get("phResponseProfile")
            curve_profile_index = index_of.get("curveControlProfile")
            profile = state.get(profile_index) if profile_index is not None else None
            if profile is None and curve_profile_index is not None:
                profile = state.get(curve_profile_index)
            ramp = RESPONSE_PROFILE_TIMES_MIN.get(str(profile))
            if ramp:
                rows.append(f"| Opbouwtijd (demand rise, from profile) | `{ramp[0]} min` |")
                rows.append(f"| Afbouwtijd (demand fall, from profile) | `{ramp[1]} min` |")
        if not rows:
            continue
        lines.append(f"## {title}")
        lines.append("")
        lines.extend(rows)
        lines.append("")
    return lines


def find_runs(rows: list[tuple[int, dict]], level_index: int) -> list[dict]:
    runs: list[dict] = []
    active = None
    for timestamp, state in rows:
        level = compression_level(state, level_index)
        if level > 0 and active is None:
            active = {"start": timestamp, "level": level}
        elif level <= 0 and active is not None:
            active["end"] = timestamp
            runs.append(active)
            active = None
    if active is not None:
        active["end"] = rows[-1][0]
        runs.append(active)
    return runs


def render_runs(rows: list[tuple[int, dict]], index_of: dict[str, int], runs: list[dict],
                level_index: int, freq_index: int, power_index: int, heat_index: int,
                req_index: int) -> list[str]:
    if not runs:
        return []
    lines = ["## Compressor runs", ""]
    if len(runs) == 1 and len(rows) <= 2:
        lines.append("_No complete run captured in this recording window._")
        lines.append("")
        return lines
    lines.append("| Run | Start | End | Duration | Levels | Freq min–max (avg) Hz | "
                 "Elec power avg W | Heat power avg W | Request power at start W |")
    lines.append("|---|---|---|---|---|---|---|---|---|")
    for number, run in enumerate(runs, start=1):
        start = run["start"]
        end = run["end"]
        # Exclude the boundary sample where the level has already dropped to 0.
        window = [(t, s) for t, s in rows if start <= t < end]
        freqs = [state[freq_index] for _, state in window
                 if freq_index is not None and isinstance(state.get(freq_index), (int, float))]
        freqs = [f for f in freqs if f > 0]
        powers = [state[power_index] for _, state in window
                  if power_index is not None and isinstance(state.get(power_index), (int, float))]
        heats = [state[heat_index] for _, state in window
                 if heat_index is not None and isinstance(state.get(heat_index), (int, float))]
        levels = sorted({int(state[level_index]) for _, state in window
                         if level_index is not None and state.get(level_index) is not None
                         and int(state[level_index]) > 0})
        start_req = None
        for _, state in window:
            if req_index is not None and state.get(req_index) is not None:
                start_req = state[req_index]
                if start_req:
                    break
        freq_text = "—"
        if freqs:
            freq_text = f"{min(freqs)}–{max(freqs)} ({sum(freqs) / len(freqs):.1f})"
        lines.append(
            f"| {number} | {clock_label(start)} | {clock_label(end)} | {human_duration(end - start)} "
            f"| {','.join(str(x) for x in levels) if levels else '—'} "
            f"| {freq_text} | {f'{sum(powers) / len(powers):.0f}' if powers else '—'} "
            f"| {f'{sum(heats) / len(heats):.0f}' if heats else '—'} "
            f"| {f'{start_req:.0f}' if isinstance(start_req, float) and start_req > 0 else start_req if start_req is not None else '—'} |")
    lines.append("")
    return lines


def render_event_timeline(rows: list[tuple[int, dict]], index_of: dict[str, int]) -> list[str]:
    compact = ["controlModeLabel", "strategyPhaseCode", "strategyRequestActive", "hp1Compressor", "hp1Freq",
               "hp1Power", "hp1Heat", "strategyRequestedPower", "requestReason", "roomTemp", "roomSetpoint",
               "supplyTemp", "outsideTempSelected", "hp1Defrost"]
    headers = ["t", "mode", "phase", "reqActive", "physLvl", "freqHz", "pInW", "heatW", "reqW", "reason",
               "Troom", "Tset", "Tsup", "Tout", "defrost"]
    lines = ["## Event timeline", ""]
    lines.append("Every row where mode, phase, compressor level/frequency, demand, temperature or reason changed. "
                 "Blank cells mean unchanged since the previous row.")
    lines.append("")
    lines.append("| " + " | ".join(headers) + " |")
    lines.append("|" + "|".join(["---"] * len(headers)) + "|")

    def render(value, name: str, digits: int) -> str:
        if value is None or (isinstance(value, float) and value != value):
            return ""
        if isinstance(value, bool):
            return "on" if value else "off"
        if isinstance(value, (int, float)):
            if name in ("hp1Freq", "hp1Power", "hp1Heat", "strategyRequestedPower"):
                return f"{value:.0f}"
            if name in ("hp1Compressor", "strategyPhaseCode"):
                return f"{value:.0f}"
            return f"{value:.1f}"
        return str(value)

    previous: dict[str, str] = {}
    first = True
    for timestamp, state in rows:
        rendered = {name: render(state.get(index_of.get(name)), name, 0) for name in compact}
        if first:
            previous = {name: "" for name in compact}
            first = False
        changed = [name for name in compact if rendered[name] != previous[name]]
        if not changed and timestamp != rows[-1][0]:
            continue
        cells = [clock_label(timestamp)]
        for name in compact:
            previous_value = previous[name]
            current_value = rendered[name]
            if name in changed:
                cells.append(current_value)
                previous[name] = current_value
            else:
                cells.append("")
        lines.append("| " + " | ".join(cells) + " |")
    lines.append("")
    return lines


def render_full_timeline(rows: list[tuple[int, dict]], index_of: dict[str, int]) -> list[str]:
    headers = ["t", "mode", "physLvl", "freqHz", "pInW", "heatW", "reqW", "Troom", "Tset", "Tsup", "Tout"]
    lines = ["## Full sample timeline", ""]
    lines.append("| " + " | ".join(headers) + " |")
    lines.append("|" + "|".join(["---"] * len(headers)) + "|")
    for timestamp, state in rows:
        def get(name, digits=0):
            index = index_of.get(name)
            value = state.get(index) if index is not None else None
            if not isinstance(value, (int, float)) or value != value:
                return ""
            return f"{value:.{digits}f}" if digits else f"{value:.0f}"

        lines.append(
            f"| {clock_label(timestamp)} | {state.get(index_of['controlModeLabel']) if index_of.get('controlModeLabel') is not None else ''} "
            f"| {get('hp1Compressor')} | {get('hp1Freq')} | {get('hp1Power')} | {get('hp1Heat')} "
            f"| {get('strategyRequestedPower')} | {get('roomTemp', 1)} | {get('roomSetpoint', 1)} "
            f"| {get('supplyTemp', 1)} | {get('outsideTempSelected', 1)} |")
    lines.append("")
    return lines


def render_report(data: dict, full_rows: list[tuple[int, dict]], show_full: bool) -> str:
    columns = data.get("columns") or []
    index_of = {name: index for index, name in enumerate(columns)}
    rows = full_rows
    if not rows:
        last_state = {}
        first_state = {}
    else:
        last_state = rows[-1][1]
        first_state = rows[0][1]

    lines = ["# OpenQuatt debug recording report", ""]
    lines.extend(render_recording_section(data))
    # Constraints/static rows are taken from the state at the start of the
    # window (configuration is stable; mode/strategy reflect what was active),
    # so the report reads well without a decoder.
    lines.extend(render_named_sections(columns, index_of, first_state or last_state))

    hp1_level = index_of.get("hp1Compressor")
    runs = find_runs(rows, hp1_level) if hp1_level is not None else []
    lines.extend(render_runs(rows, index_of, runs, hp1_level,
                             index_of.get("hp1Freq"), index_of.get("hp1Power"),
                             index_of.get("hp1Heat"), index_of.get("strategyRequestedPower")))

    if not runs:
        lines.extend(render_full_timeline(rows, index_of))
    else:
        lines.extend(render_event_timeline(rows, index_of))
        if show_full:
            lines.extend(render_full_timeline(rows, index_of))
    return "\n".join(lines) + "\n"


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.strip().splitlines()[0])
    parser.add_argument("recording", type=Path, help="path to a .oqdebug.json export")
    parser.add_argument("-o", "--output", type=Path, help="write markdown to this file instead of stdout")
    parser.add_argument("--full", action="store_true", help="always dump the full per-sample timeline")
    args = parser.parse_args(argv)

    data = load_recording(args.recording)
    rows = build_state(data)
    report = render_report(data, rows, show_full=args.full)

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(report, "utf-8")
        print(f"wrote {args.output}")
    else:
        sys.stdout.write(report)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())