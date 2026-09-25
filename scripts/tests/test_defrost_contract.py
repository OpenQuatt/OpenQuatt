from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[2]
SERVICE = (ROOT / "components/openquatt_odu_defrost/OpenQuattOduDefrost.cpp").read_text()
IO = (ROOT / "openquatt/oq_HP_io.yaml").read_text()
RUNTIME = (ROOT / "openquatt/includes/control/oq_thermal_actuator_runtime.h").read_text()


class DefrostContractTest(unittest.TestCase):
    def test_only_single_writes_without_retry_and_no_multi_write(self):
        # Manual trigger and defrost-mode save are the only writes; no retry, no multi-write.
        self.assertEqual(SERVICE.count("write_single_register("), 2)
        self.assertIn("write_single_register(3999U, 4U)", SERVICE)
        self.assertIn("write_single_register(oq_defrost::MODE_REGISTER,", SERVICE)
        self.assertNotIn("write_multiple_registers", SERVICE)
        callback = SERVICE[SERVICE.index("bool OpenQuattOduDefrost::on_no_response"):]
        self.assertIn("return false;", callback.split("void OpenQuattOduDefrost::on_error")[0])

    def test_defrost_capabilities_follow_validated_odu_variant(self):
        logic = (ROOT / "openquatt/includes/control/oq_defrost_logic.h").read_text()
        header = (ROOT / "components/openquatt_odu_defrost/OpenQuattOduDefrost.h").read_text()
        self.assertIn("variant == oq_odu::Variant::V1", logic)
        self.assertIn("mode == 0 || mode == 1 || mode == 3", logic)
        self.assertIn("has_mode4_defrost", logic)
        self.assertIn("is_supported_defrost_mode(desired, variant_)", SERVICE)
        self.assertIn("owner_->supports_mode(desired)", SERVICE)
        self.assertIn("parameter_block_count_()", SERVICE)
        self.assertIn("set_odu_identity(oq_odu::Variant variant)", header)
        self.assertIn("id(${hp_id}_odu_defrost)->set_odu_identity(detection.variant);", IO)

    def test_mode_save_validates_and_verifies_readback(self):
        header = (ROOT / "components/openquatt_odu_defrost/OpenQuattOduDefrost.h").read_text()
        logic = (ROOT / "openquatt/includes/control/oq_defrost_logic.h").read_text()
        self.assertIn("MODE_REGISTER", logic)
        self.assertIn("is_supported_defrost_mode", logic)
        self.assertIn("mode_save_error", logic)
        self.assertIn("mode_save_guard_refusal", logic)
        self.assertIn("oq_odu::Variant variant", logic)
        self.assertIn("enqueue_save", header)
        self.assertIn("take_save", header)
        self.assertIn("send_mode_once", header)
        self.assertIn("confirm_saved", header)
        # Exactly one register per save, verified by a fresh readback; HTTP 200/ACK alone is no proof.
        self.assertIn('cycle.result = "SAVED"', SERVICE)
        self.assertIn('"STALE"', logic)
        self.assertIn("save_verifying_", SERVICE)
        self.assertIn("parameters_.mode() == save_write_", SERVICE)
        # Stale snapshot must reload, never write; no retry or rollback.
        self.assertIn("expected != current", logic)
        self.assertIn("NO_CHANGE", logic)
        save_fn = logic[logic.index("mode_save_error("):]
        self.assertLess(save_fn.index("expected != current"), save_fn.index("desired == current"))

    def test_save_consumes_before_guard_and_rereads_stopped_hz(self):
        runtime = (ROOT / "openquatt/includes/control/oq_thermal_actuator_runtime.h").read_text()
        update = runtime[runtime.index("void update_defrost_("):runtime.index("openquatt_odu_defrost::Snapshot snapshot{}")]
        # take_save() runs before the guard is built, mirroring take_trigger(),
        # so guard.busy cannot see our own pending save.
        self.assertLess(update.index("take_save("), update.index("oq_defrost::Guard guard{"))
        # Save has its own hard-safety guard: direct flow is a trigger precondition, not a stopped-mode requirement.
        self.assertIn("save_guard.incident = this->defrost_safety_stop_(is_hp1);", update)
        # Require both a fresh zero-Hz sample and incident-manager confirmation that the compressor is not running.
        self.assertIn("!incident.running_confirmed && service->sample_fresh(0, now)", update)

    def test_save_endpoint_requires_auth_origin_csrf_and_valid_modes(self):
        self.assertIn('"save"', SERVICE)
        self.assertIn('req->arg("mode")', SERVICE)
        self.assertIn('req->arg("expected_mode")', SERVICE)
        self.assertIn('{"error":"invalid_mode"}', SERVICE)
        self.assertIn('{"error":"stale"}', SERVICE)
        self.assertIn("enqueue_save", SERVICE)

    def test_post_requires_auth_origin_and_csrf(self):
        auth = SERVICE.index("if (!owner_->authenticated(req))")
        csrf = SERVICE.index('req->arg("csrf_token") != csrf')
        enqueue = SERVICE.index("owner_->enqueue(action)")
        self.assertLess(auth, csrf)
        self.assertLess(csrf, enqueue)
        self.assertIn("!same_origin(req)", SERVICE[auth:enqueue])
        self.assertIn('httpd_resp_set_status(*req, "403 Forbidden")', SERVICE[auth:enqueue])
        self.assertNotIn('req->send(403,', SERVICE)

    def test_csrf_snapshot_owns_its_storage(self):
        header = (ROOT / "components/openquatt_odu_defrost/OpenQuattOduDefrost.h").read_text()
        self.assertIn("std::string csrf() const", header)
        self.assertNotIn("const std::string& csrf()", header)
        self.assertIn("const auto csrf = owner_->csrf();", SERVICE)
        self.assertIn("csrf.empty()", SERVICE)
        self.assertIn("const auto token = csrf();", SERVICE)
        self.assertIn("token.c_str(), token.size()", SERVICE)

    def test_http_does_not_perform_modbus_or_mutate_cycle(self):
        handler = SERVICE[SERVICE.index("class Handler"):SERVICE.index("const char* boolean")]
        for forbidden in ("write_single_register", "read_holding_registers", "cycle."):
            self.assertNotIn(forbidden, handler)
        self.assertIn("portENTER_CRITICAL(&mux_)", SERVICE)

    def test_final_write_gates_cover_both_actuators(self):
        self.assertEqual(IO.count("normal_write_allowed(value, hard_stop)"), 2)
        self.assertIn("cycle.observe_bit(x, millis())", IO)
        self.assertIn("cycle.observe_mode(x, millis())", IO)
        self.assertIn("->offline();", IO)
        self.assertIn("defrost->holding()", RUNTIME)
        self.assertIn("clear_tx_queue_for_address();", SERVICE)
        self.assertIn("hub->tx_buffer_empty()", SERVICE)
        self.assertIn("cycle.mode_seen = cycle.bit_seen = false", SERVICE)
        self.assertIn("stop_pending || defrost->cycle.telemetry_fault", RUNTIME)
        self.assertIn("incident.must_stop || defrost->cycle.telemetry_fault", RUNTIME)

    def test_forced_mode_never_enters_normal_select_options(self):
        options = IO[IO.index('"Standby": 0'):IO.index("write_lambda:", IO.index('"Standby": 0'))]
        self.assertNotIn('"Defrost"', options)
        self.assertNotIn(": 4", options)

    def test_defrost_reservation_keeps_live_polling_enabled(self):
        planner = IO[IO.index("interval:\n"):]
        self.assertIn("id(${hp_id}_odu_eeprom_dump).is_active() &&", planner)
        self.assertIn("!id(${hp_id}_odu_defrost)->reserved()", planner)
        self.assertIn("id(${hp_id}).update();", planner)

    def test_stopped_compressor_is_not_reported_as_an_incident(self):
        guard = RUNTIME[RUNTIME.index("oq_defrost::Guard guard{"):RUNTIME.index("const char* refusal = oq_defrost::refusal(guard)")]
        self.assertNotIn("|| !incident.running_confirmed", guard)
        self.assertIn("incident.running_confirmed && !id(oq_incident_manager).startup_inhibited(hp)", guard)
        self.assertIn(": NAN", guard)

    def test_defrost_keeps_circulation_and_blocks_cm0_stop_pwm(self):
        supervisor = (ROOT / "openquatt/includes/control/oq_supervisory_state_runtime.h").read_text()
        logic = (ROOT / "openquatt/includes/control/oq_defrost_logic.h").read_text()
        # HP1 guard is computed once and reused for Single/Duo; HP2 only exists for Duo.
        self.assertEqual(supervisor.count("id(hp1_odu_defrost).holding()"), 1)
        self.assertEqual(supervisor.count("id(hp2_odu_defrost).holding()"), 1)
        self.assertIn("oq_defrost::hp_active(", supervisor)
        # P1a: CM0 with an active HP must circulate, never keep the stop PWM while the relay is On.
        self.assertIn("oq_defrost::cm0_pump_target(sticky_active, any_hp_active_guard,", supervisor)
        self.assertIn("cm0_pump_target(bool sticky_active, bool hp_active_guard,", logic)
        self.assertNotIn('strcmp(desired_cm, "CM0") == 0 && !any_hp_active_guard', supervisor)
        self.assertIn('hold_cm1_until_hp_idle(strcmp(cur_cm, "CM1") == 0, desired_local,', supervisor)

    def test_manual_trigger_requires_actual_minimum_flow(self):
        logic = (ROOT / "openquatt/includes/control/oq_defrost_logic.h").read_text()
        runtime = (ROOT / "openquatt/includes/control/oq_thermal_actuator_runtime.h").read_text()
        # P1b: manual start needs actual flow, but a stopped mode-edit must not require circulation.
        self.assertIn("minimum_flow_ready(float flow_lph, float minimum_lph)", logic)
        safety = runtime[runtime.index("bool defrost_safety_stop_"):runtime.index("void update_defrost_")]
        update = runtime[runtime.index("void update_defrost_"):runtime.index("bool real_defrost_seen_")]
        self.assertNotIn("minimum_flow_ready", safety)
        self.assertIn("!oq_defrost::minimum_flow_ready(flow_lph, this->last_minimum_flow_lph_)", update)
        self.assertIn("id(flow_rate_selected).has_state()", update)
        self.assertIn("this->last_minimum_flow_lph_ = config.minimum_flow_lph;", runtime)

    def test_stale_telemetry_holds_previous_then_safety_stops(self):
        # P2 design choice: stale (<90 s) holds the previous compressor command when no
        # proven defrost is owned/observed; only prolonged blindness forces a stop.
        logic = (ROOT / "openquatt/includes/control/oq_defrost_logic.h").read_text()
        runtime = (ROOT / "openquatt/includes/control/oq_thermal_actuator_runtime.h").read_text()
        self.assertIn("FRESH_MS = 30000U", logic)
        self.assertIn("now - mode_ms > 90000U || now - bit_ms > 90000U", logic)
        hold = runtime[runtime.index("if (!this->defrost_safety_stop_(is_hp1) &&"):]
        hold = hold[:hold.index("uint32_t& last_safe_write_ms")]
        self.assertIn("!defrost->cycle.fresh(", hold)
        self.assertIn("defrost->holding()", hold)
        self.assertIn("defrost->cycle.observed_active()", hold)
        self.assertIn("return previous;", hold)


if __name__ == "__main__":
    unittest.main()
