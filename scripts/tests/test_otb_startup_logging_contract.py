from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
HUB_HEADER = (ROOT / "components" / "opentherm" / "hub.h").read_text()
HUB_CPP = (ROOT / "components" / "opentherm" / "hub.cpp").read_text()
OT_MASTER_CPP = (ROOT / "components" / "opentherm" / "opentherm.cpp").read_text()
OT_NUMBER_CPP = (ROOT / "components" / "opentherm" / "number" / "opentherm_number.cpp").read_text()
OT_SWITCH_CPP = (ROOT / "components" / "opentherm" / "switch" / "opentherm_switch.cpp").read_text()
OT_OUTPUT_CPP = (ROOT / "components" / "opentherm" / "output" / "opentherm_output.cpp").read_text()
OT_SLAVE_CPP = (ROOT / "components" / "openquatt_ot_slave" / "OpenQuattOTSlave.cpp").read_text()
OT_SLAVE_SWITCH_CPP = (ROOT / "components" / "openquatt_ot_slave" / "switch.cpp").read_text()
OT_SLAVE_TRANSPORT_CPP = (ROOT / "components" / "openquatt_ot_slave" / "OpenTherm.cpp").read_text()
OTB_PACKAGE = (ROOT / "openquatt" / "oq_boiler_opentherm.yaml").read_text()
OTB_RUNTIME = (ROOT / "openquatt" / "includes" / "boiler" / "oq_boiler_otb_runtime.h").read_text()
OTB_TELEMETRY = (ROOT / "openquatt" / "includes" / "boiler" / "oq_otb_telemetry.h").read_text()


class OtbStartupLoggingContractTest(unittest.TestCase):
    def test_suppression_state_exists_and_is_scoped(self) -> None:
        self.assertIn("bool no_response_expected_ = false;", HUB_HEADER)
        self.assertIn("void set_no_response_expected(bool expected)", HUB_HEADER)
        self.assertIn("bool no_response_expected() const", HUB_HEADER)

    def test_timeout_suppresses_only_expected_no_frame(self) -> None:
        start = HUB_CPP.index("void OpenthermHub::handle_timeout_error_()")
        end = HUB_CPP.index("void OpenthermHub::handle_timer_error_()", start)
        method = HUB_CPP[start:end]
        # All three transport/timing deviations must keep a WARN path.
        self.assertIn("RMT TX did not complete", method)
        self.assertIn("frame was captured after the receive deadline", method)
        self.assertIn("Timeout while waiting for response from device", method)
        self.assertGreaterEqual(method.count("ESP_LOGW"), 3)
        # Only the expected no-response case is gated on the probe flag.
        self.assertIn("if (!this->no_response_expected_)", method)
        # Suppressed probe timeout stays visible below WARN for diagnostics.
        self.assertIn("ESP_LOGD", method)
        # No early return: transport reset and counters must still run.
        self.assertIn("this->stop_opentherm_();", method)
        self.assertNotIn("return;", method.split("this->stop_opentherm_();")[0].split("handle_timeout_error_")[1])

    def test_suppression_is_never_sticky(self) -> None:
        resume_start = HUB_CPP.index("void OpenthermHub::resume_polling()")
        suspend_start = HUB_CPP.index("void OpenthermHub::suspend_polling()", resume_start)
        write_initial = HUB_CPP.index("void OpenthermHub::write_initial_messages_", suspend_start)
        resume_method = HUB_CPP[resume_start:suspend_start]
        suspend_method = HUB_CPP[suspend_start:write_initial]
        self.assertIn("this->no_response_expected_ = false;", resume_method)
        self.assertIn("this->no_response_expected_ = false;", suspend_method)
        # Priority polling is also used outside the probe and must not imply suppression.
        poll_start = HUB_CPP.index("void OpenthermHub::start_priority_polling(")
        poll_end = HUB_CPP.index("void OpenthermHub::resume_polling()", poll_start)
        poll_method = HUB_CPP[poll_start:poll_end]
        self.assertNotIn("no_response_expected_", poll_method)

    def test_boiler_log_tags(self) -> None:
        self.assertIn('"oq.ot.boiler"', HUB_CPP)
        self.assertIn('"oq.ot.boiler"', OT_MASTER_CPP)
        self.assertIn('"oq.ot.boiler.number"', OT_NUMBER_CPP)
        self.assertIn('"oq.ot.boiler.switch"', OT_SWITCH_CPP)
        self.assertIn('"oq.ot.boiler.output"', OT_OUTPUT_CPP)
        for source in (HUB_CPP, OT_MASTER_CPP):
            self.assertNotIn('TAG = "opentherm"', source)
        self.assertNotIn('"opentherm.number"', OT_NUMBER_CPP)
        self.assertNotIn('"opentherm.switch"', OT_SWITCH_CPP)
        self.assertNotIn('"opentherm.output"', OT_OUTPUT_CPP)

    def test_thermostat_log_tags(self) -> None:
        self.assertIn('"oq.ot.thermostat"', OT_SLAVE_CPP)
        self.assertIn('"oq.ot.thermostat.switch"', OT_SLAVE_SWITCH_CPP)
        self.assertIn('"oq.ot.thermostat.transport"', OT_SLAVE_TRANSPORT_CPP)
        self.assertNotIn('"OpenQuattOTSlave"', OT_SLAVE_CPP)
        self.assertNotIn('"OpenQuattOTSlave.switch"', OT_SLAVE_SWITCH_CPP)
        self.assertNotIn('"OpenThermV2"', OT_SLAVE_TRANSPORT_CPP)

    def test_no_component_namespace_rename(self) -> None:
        self.assertIn("namespace esphome::opentherm", HUB_CPP)
        self.assertIn("namespace OpenQuattOTSlave", OT_SLAVE_CPP)

    def test_telemetry_follows_boiler_tag(self) -> None:
        self.assertIn('strcmp(tag, "oq.ot.boiler")', OTB_TELEMETRY)
        self.assertNotIn('strcmp(tag, "opentherm")', OTB_TELEMETRY)

    def test_boot_probe_sets_and_clears_suppression(self) -> None:
        boot_start = OTB_PACKAGE.index("on_boot:")
        shutdown_start = OTB_PACKAGE.index("on_shutdown:", boot_start)
        boot_block = OTB_PACKAGE[boot_start:shutdown_start]
        self.assertIn("id(oq_otb_hub).set_no_response_expected(true);", boot_block)
        self.assertIn("id(oq_otb_hub).set_no_response_expected(false);", boot_block)
        self.assertIn("id(oq_otb_hub).start_priority_polling(", boot_block)
        self.assertLess(
            boot_block.index("set_no_response_expected(true);"),
            boot_block.index("start_priority_polling("),
        )
        self.assertLess(
            boot_block.index("set_no_response_expected(false);"),
            boot_block.index("resume_polling();"),
        )

    def test_probe_exit_clears_before_normal_polling(self) -> None:
        self.assertIn(
            "id(oq_otb_hub).set_no_response_expected(false);",
            OTB_PACKAGE[OTB_PACKAGE.index("startup_probe_state.end();\n          id(oq_otb_startup_probe_active) = false;"):],
        )
        end_marker = "oq_otb::startup_probe_state.end();\n          id(oq_otb_startup_probe_active) = false;"
        end_pos = OTB_PACKAGE.index(end_marker, OTB_PACKAGE.index("STARTUP_PROBE_RUNNING) return;"))
        window = OTB_PACKAGE[end_pos : end_pos + 600]
        self.assertIn("set_no_response_expected(false);", window)

    def test_connection_change_covers_both_directions(self) -> None:
        self.assertEqual(OTB_RUNTIME.count("set_no_response_expected(true);"), 1)
        self.assertEqual(OTB_RUNTIME.count("set_no_response_expected(false);"), 1)
        ot_branch = OTB_RUNTIME.index("if (opentherm_selected) {")
        r1_branch = OTB_RUNTIME.index("} else {", ot_branch)
        self.assertLess(
            OTB_RUNTIME.index("set_no_response_expected(false);", ot_branch),
            OTB_RUNTIME.index("resume_polling();", ot_branch),
        )
        self.assertLess(
            OTB_RUNTIME.index("set_no_response_expected(true);", r1_branch),
            OTB_RUNTIME.index("start_priority_polling(", r1_branch),
        )

    def test_shutdown_clears_suppression(self) -> None:
        shutdown_start = OTB_PACKAGE.index("on_shutdown:")
        logger_start = OTB_PACKAGE.index("logger:", shutdown_start)
        shutdown_block = OTB_PACKAGE[shutdown_start:logger_start]
        self.assertIn("set_no_response_expected(false);", shutdown_block)

    def test_probe_start_logs_single_info(self) -> None:
        probe_log = 'ESP_LOGI("quatt.boiler", "Verifying boiler OpenTherm connection before enabling R1");'
        boot_start = OTB_PACKAGE.index("on_boot:")
        shutdown_start = OTB_PACKAGE.index("on_shutdown:", boot_start)
        boot_block = OTB_PACKAGE[boot_start:shutdown_start]
        self.assertIn(probe_log, boot_block)
        self.assertEqual(boot_block.count(probe_log), 1)
        self.assertLess(
            boot_block.index("set_no_response_expected(true);"),
            boot_block.index(probe_log),
        )
        self.assertLess(
            boot_block.index(probe_log),
            boot_block.index("start_priority_polling("),
        )
        self.assertIn(probe_log, OTB_RUNTIME)
        self.assertEqual(OTB_RUNTIME.count(probe_log), 1)
        r1_branch = OTB_RUNTIME.index("} else {", OTB_RUNTIME.index("if (opentherm_selected) {"))
        self.assertLess(
            OTB_RUNTIME.index("set_no_response_expected(true);", r1_branch),
            OTB_RUNTIME.index(probe_log, r1_branch),
        )
        self.assertLess(
            OTB_RUNTIME.index(probe_log, r1_branch),
            OTB_RUNTIME.index("start_priority_polling(", r1_branch),
        )

    def test_new_text_prefers_boiler_thermostat_terms(self) -> None:
        probe_log = "Verifying boiler OpenTherm connection before enabling R1"
        self.assertIn(probe_log, OTB_PACKAGE)
        self.assertIn(probe_log, OTB_RUNTIME)
        for forbidden in ("OTB", "OTT", "master", "slave", "Master", "Slave"):
            self.assertNotIn(forbidden, probe_log)


if __name__ == "__main__":
    unittest.main()
