from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
SELECT_OT = (ROOT / "openquatt/oq_sensor_source_selects_opentherm.yaml").read_text()
SELECT_NO_OT = (ROOT / "openquatt/oq_sensor_source_selects_no_opentherm.yaml").read_text()
SCHEDULE_YAML = (ROOT / "openquatt/oq_schedule.yaml").read_text()
SOURCE_YAML = (ROOT / "openquatt/oq_sensor_sources.yaml").read_text()
SOURCE_LOGIC = (ROOT / "openquatt/includes/control/oq_input_source_logic.h").read_text()
SOURCE_RUNTIME = (ROOT / "openquatt/includes/control/oq_sensor_source_runtime.h").read_text()
SCHEDULE_RUNTIME = (ROOT / "openquatt/includes/control/oq_schedule_runtime.h").read_text()


def select_options(source: str, entity_id: str) -> list[str]:
    lines = source.splitlines()
    entity_index = next(index for index, line in enumerate(lines) if line.strip() == f"id: {entity_id}")
    options_index = next(
        index for index in range(entity_index + 1, len(lines)) if lines[index].strip() == "options:"
    )
    options: list[str] = []
    for line in lines[options_index + 1 :]:
        stripped = line.strip()
        if stripped.startswith("initial_option:"):
            break
        if stripped.startswith("- "):
            options.append(stripped[2:])
    return options


def entity_block(source: str, entity_id: str) -> str:
    start = source.index(f"    id: {entity_id}")
    end = source.find("\n  - platform:", start + 1)
    return source[start:] if end < 0 else source[start:end]


class CoolingScheduleContractTest(unittest.TestCase):
    def test_cooling_source_indices_are_upgrade_safe_on_every_profile(self) -> None:
        legacy_prefix = [
            "CIC",
            "HA input",
            "API input",
            "MQTT",
            "CIC or HA input",
            "Disabled",
            "OT thermostat",
        ]
        expected = [*legacy_prefix, "Schedule"]
        self.assertEqual(select_options(SELECT_OT, "cooling_enable_source"), expected)
        self.assertEqual(select_options(SELECT_NO_OT, "cooling_enable_source"), expected)
        self.assertEqual(expected[6], "OT thermostat")
        self.assertEqual(expected[7], "Schedule")

        # This feature is cooling-only; never expose the shared enum through
        # the heating selector or change its persisted option indices.
        self.assertNotIn("Schedule", select_options(SELECT_OT, "heating_enable_source"))
        self.assertNotIn("Schedule", select_options(SELECT_NO_OT, "heating_enable_source"))

    def test_schedule_times_restore_but_default_to_a_disabled_window(self) -> None:
        for entity_id in ("oq_cooling_schedule_start_time", "oq_cooling_schedule_end_time"):
            block = entity_block(SCHEDULE_YAML, entity_id)
            self.assertIn("type: time", block)
            self.assertIn("optimistic: true", block)
            self.assertIn("restore_value: true", block)
            self.assertIn('initial_value: "00:00:00"', block)

    def test_runtime_checks_clock_and_entity_state_before_reading_times(self) -> None:
        now = SCHEDULE_RUNTIME.index("const auto now = id(oq_time).now();")
        clock_guard = SCHEDULE_RUNTIME.index("if (!now.is_valid())")
        entity_guard = SCHEDULE_RUNTIME.index("if (!id(oq_cooling_schedule_start_time).has_state()")
        start_read = SCHEDULE_RUNTIME.index("id(oq_cooling_schedule_start_time).state_as_esptime()")
        end_read = SCHEDULE_RUNTIME.index("id(oq_cooling_schedule_end_time).state_as_esptime()")
        self.assertLess(now, clock_guard)
        self.assertLess(clock_guard, entity_guard)
        self.assertLess(entity_guard, start_read)
        self.assertLess(entity_guard, end_read)
        self.assertIn("evaluate(false, false, false", SCHEDULE_RUNTIME)
        self.assertIn("evaluate(true, false, false", SCHEDULE_RUNTIME)

    def test_schedule_is_wired_only_as_a_cooling_enable_source(self) -> None:
        self.assertIn("oq_schedule: !include oq_schedule.yaml", SOURCE_YAML)
        self.assertIn('#include "oq_schedule_runtime.h"', SOURCE_RUNTIME)
        self.assertIn('if (option == "Schedule") return oq_input_source::Source::SCHEDULE;', SOURCE_RUNTIME)
        self.assertIn("const auto schedule = oq_schedule::cooling_window();", SOURCE_RUNTIME)
        self.assertIn("{schedule.active, schedule.valid}", SOURCE_RUNTIME)
        self.assertIn("SCHEDULE", SOURCE_LOGIC)
        self.assertIn("case Source::SCHEDULE:", SOURCE_LOGIC)
        self.assertIn("return valid_binary(sources.schedule);", SOURCE_LOGIC)


if __name__ == "__main__":
    unittest.main()
