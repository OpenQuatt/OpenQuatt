from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
CONTROL_YAML = (ROOT / "openquatt/oq_boiler_control.yaml").read_text()
DISPATCH_YAML = (ROOT / "openquatt/oq_boiler_dispatch.yaml").read_text()
OTB_YAML = (ROOT / "openquatt/oq_boiler_opentherm.yaml").read_text()
CONTROL_RUNTIME = (ROOT / "openquatt/includes/control/oq_boiler_runtime.h").read_text()
DISPATCH_RUNTIME = (ROOT / "openquatt/includes/control/oq_boiler_dispatch_runtime.h").read_text()
OTB_RUNTIME = (ROOT / "openquatt/includes/boiler/oq_boiler_otb_runtime.h").read_text()
BOILER_LOGIC = (ROOT / "openquatt/includes/boiler/oq_boiler_logic.h").read_text()
RELAY_TARGET_LOGIC = (ROOT / "openquatt/includes/boiler/oq_boiler_relay_target_logic.h").read_text()
DISPATCH_LOGIC = (ROOT / "openquatt/includes/control/oq_boiler_dispatch_logic.h").read_text()
DEFROST_ASSIST_LOGIC = (ROOT / "openquatt/includes/control/oq_boiler_defrost_assist_logic.h").read_text()
SUPERVISORY_YAML = (ROOT / "openquatt/oq_supervisory_controlmode.yaml").read_text()
SUPERVISORY_RUNTIME = (ROOT / "openquatt/includes/control/oq_supervisory_state_runtime.h").read_text()
SUBSTITUTIONS = (ROOT / "openquatt/oq_substitutions_common.yaml").read_text()


class BoilerRuntimeContractTest(unittest.TestCase):
    def test_yaml_delegates_to_runtime(self) -> None:
        self.assertEqual(CONTROL_YAML.count("oq_boiler_runtime::runtime().tick("), 1)
        self.assertEqual(DISPATCH_YAML.count("oq_boiler_dispatch_runtime::tick("), 1)
        self.assertEqual(OTB_YAML.count("oq_boiler_otb_runtime::apply_command("), 1)
        self.assertEqual(OTB_YAML.count("oq_boiler_otb_runtime::link_watch("), 1)

    def test_stateful_decisions_live_in_cpp(self) -> None:
        for removed in (
            "static oq_boiler::BlockReason last_block_reason",
            "static oq_boiler_output::Controller output_controller",
            "static bool last_connected",
        ):
            self.assertNotIn(removed, CONTROL_YAML)
        self.assertNotIn("const bool ph_fresh", DISPATCH_YAML)
        self.assertNotIn("id(otb_ch_enable).turn_on()", OTB_YAML)
        self.assertIn("oq_boiler::evaluate(", CONTROL_RUNTIME)
        self.assertIn("oq_boiler_dispatch::dispatch(", DISPATCH_RUNTIME)
        self.assertIn("oq_boiler_transport::evaluate_command_adapter(", OTB_RUNTIME)


    def test_q_only_otb_binding_is_guarded(self) -> None:
        self.assertIn("#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q", OTB_RUNTIME)

    def test_relay_target_control_is_isolated_policy(self) -> None:
        # De hysterese is boiler-specifiek beleid en staat niet als losse
        # constanten verspreid over de keten.
        self.assertIn("struct RelayTargetConfig", RELAY_TARGET_LOGIC)
        self.assertIn("float start_delta_c = 2.0f;", RELAY_TARGET_LOGIC)
        self.assertIn("float stop_delta_c = 0.5f;", RELAY_TARGET_LOGIC)
        self.assertIn("bool relay_target_config_valid(", RELAY_TARGET_LOGIC)
        self.assertIn("RelayTargetConfig relay_target = {};", CONTROL_RUNTIME)
        # De doelregeling zit in de uitgangsadapter, niet in de ketelketen of
        # in een YAML-lambda.
        self.assertIn("oq_boiler::evaluate_relay_target(", CONTROL_RUNTIME)
        self.assertIn("oq_boiler::relay_target_control_applies(", CONTROL_RUNTIME)
        self.assertIn("bool relay_target_control_applies(uint8_t source, bool opentherm_selected)", BOILER_LOGIC)
        self.assertNotIn("relay_target", DISPATCH_LOGIC)
        self.assertNotIn("RelayTargetConfig{", DISPATCH_YAML)
        # De OpenTherm-transport blijft onaangeroerd: die ontvangt het doel via
        # de bus en moduleert zelf.
        self.assertNotIn("relay_target", OTB_RUNTIME)
        self.assertNotIn("relay_target", OTB_YAML)

    def test_satisfied_target_is_not_a_safety_failure(self) -> None:
        self.assertIn("BLOCK_TARGET_SATISFIED = 25", BOILER_LOGIC)
        self.assertIn("return \"requested boiler target temperature satisfied\";", BOILER_LOGIC)
        self.assertIn("BLOCK_TARGET_HOLD_OFF = 26", BOILER_LOGIC)
        self.assertIn("return \"boiler target control holding off inside target band\";", BOILER_LOGIC)
        # Een bereikt doel en een relais dat binnen de band uit blijft zijn beide
        # normale gevolgen van de R1-doelregeling en worden daarom niet als
        # blokkade of fout gerapporteerd. Een niet-beoordeelbare doelregeling
        # blijft wél een blokkade.
        self.assertIn(
            "decision.blocked = decision.demand_present && !decision.output_active && !relay_target_normal_stop;",
            BOILER_LOGIC,
        )
        self.assertIn(
            "const bool relay_target_failsafe = relay_target_withholds_output && !relay_target_normal_stop;",
            BOILER_LOGIC,
        )

    def test_phase_one_keeps_existing_hp_defrost_tunables(self) -> None:
        # FASE 1 raakt het bestaande defrostgedrag van de warmtepomp niet: de
        # vermogensderating en de Duo-compensatie blijven op dezelfde waarden.
        for substitution in (
            'oq_defrost_power_factor: "0.764"',
            'oq_defrost_comp_min_f: "6"',
            'oq_defrost_comp_boost_steps: "1"',
        ):
            self.assertIn(substitution, SUBSTITUTIONS)

    def test_defrost_assist_is_opt_in(self) -> None:
        # De ketelhulp tijdens ontdooien staat standaard uit, zodat bestaand
        # gedrag zonder expliciete keuze ongewijzigd blijft.
        start = SUPERVISORY_YAML.index("id: oq_defrost_boiler_support_enabled")
        end = SUPERVISORY_YAML.index("id: oq_boiler_fault_fallback_enabled", start)
        switch = SUPERVISORY_YAML[start:end]
        self.assertIn('name: "Boiler support during defrost"', switch)
        # v0.59.0 bevatte de functie onder een andere naam (#961, teruggedraaid in
        # #983). De oude naam mag niet terugkomen: een opgeslagen ON zou dan
        # worden hersteld.
        self.assertNotIn("oq_boiler_defrost_" + "assist_enabled", SUPERVISORY_YAML)
        self.assertNotIn("Boiler assist " + "during defrost", SUPERVISORY_YAML)
        self.assertIn("restore_mode: RESTORE_DEFAULT_OFF", switch)
        self.assertNotIn("RESTORE_DEFAULT_ON", switch)
        # De hulp is een eigen toestemming: de aanwezige warmtebron is vereist,
        # de algemene ketelhulp niet.
        self.assertIn(
            "{now_ms, id(oq_aux_heat_source_present).state && id(oq_defrost_boiler_support_enabled).state,",
            SUPERVISORY_RUNTIME,
        )
        self.assertIn(
            "oq_boiler::assist_permission_for_source(command.source, id(oq_boiler_assist_enabled).state,",
            CONTROL_RUNTIME,
        )
        # Zonder warmtebron wordt de toestemming ingetrokken, net als de andere.
        self.assertIn("- switch.turn_off: oq_defrost_boiler_support_enabled", SUPERVISORY_YAML)
        # De normale ketelhulp blijft afhankelijk van haar eigen schakelaar.
        self.assertIn(
            "const bool boiler_assist_available = boiler_assist_enabled && boiler_transport_available &&\n"
            "                                               "
            "(cold_start_assist_requested || boiler_strategy_command_available);",
            SUPERVISORY_RUNTIME,
        )
        self.assertIn('oq_boiler_defrost_assist_max_s: "900"', SUBSTITUTIONS)
        self.assertIn("${oq_boiler_defrost_assist_max_s},", SUPERVISORY_YAML)

    def test_defrost_assist_observation_stays_in_supervisory(self) -> None:
        # De defrostwaarneming blijft in de supervisory. De ketelketen krijgt
        # alleen CM3-eigenaarschap en de vast te houden aanvoertemperatuur.
        self.assertIn("oq_boiler_defrost_assist::update(", SUPERVISORY_RUNTIME)
        self.assertIn("bool supply_hold_assist = false;", DISPATCH_LOGIC)
        self.assertIn("float supply_hold_target_c = NAN;", DISPATCH_LOGIC)
        self.assertIn("in.supply_hold_assist = id(oq_defrost_assist_active);", DISPATCH_RUNTIME)
        for chain_source in (DISPATCH_RUNTIME, CONTROL_RUNTIME, OTB_RUNTIME):
            self.assertNotIn("4_way_valve", chain_source)
            self.assertNotIn("_defrost).state", chain_source)
        # De hulp vraagt nooit meer dan de referentie en is in tijd begrensd.
        self.assertIn(
            "state.target_c = state.reference_supply_c + usable_margin(config.target_margin_c);",
            DEFROST_ASSIST_LOGIC,
        )
        self.assertIn("uint32_t maximum_ms = 900000UL;", DEFROST_ASSIST_LOGIC)
        self.assertIn('oq_boiler_defrost_assist_margin_c: "3.0"', SUBSTITUTIONS)
        self.assertIn("${oq_boiler_defrost_assist_margin_c},", SUPERVISORY_YAML)
        # Het doel blijft onder de zachte band van de watertemperatuurgrens.
        self.assertIn("supply_hold_target_ceiling(in.maximum_water_temperature_c)", DISPATCH_LOGIC)
        self.assertIn("- oq_thermal_limits::Config{}.soft_band_c;", DISPATCH_LOGIC)
        # Geen referentie uit de terugval op de warmtepompuitgang, en een
        # defrost-eigen CM3 gaat nooit over in normale ketelhulp.
        self.assertIn(
            "!id(oq_water_supply_temp_fallback_runtime_active), id(water_supply_temp_selected).state, boiler_heat_active,",
            SUPERVISORY_RUNTIME,
        )
        # Ketelwarmte van normale hulp, koude start, fallback of een eerdere
        # sessie wordt nooit de referentie voor het volgende doel.
        self.assertIn(
            "const bool boiler_heat_active = this->defrost_assist_first_tick_ || strcmp(cur_cm, \"CM3\") == 0 ||\n"
            "                                    strcmp(cur_cm, \"CM4\") == 0 || id(oq_boiler_output_request) ||\n"
            "                                    id(boiler_relay).state;",
            SUPERVISORY_RUNTIME,
        )
        # Alleen een actuele melding promoveert vanuit CM2; de vrijgavetijd
        # start de ketel nooit meer na afloop van de defrost.
        self.assertIn("((cm_now == 2 && any_hp_defrost_reported) || defrost_assist_owned ||", SUPERVISORY_RUNTIME)
        self.assertIn("if (!input.heating_owned || !input.supply_reference_valid || state.boiler_heat_recent) {",
                      DEFROST_ASSIST_LOGIC)
        self.assertIn("} else if (defrost_assist_owned) {", SUPERVISORY_RUNTIME)
        self.assertIn("id(oq_defrost_assist_active) && current_cm_code == 3 ? 2 : current_cm_code);",
                      SUPERVISORY_RUNTIME)
        self.assertIn("bool session_closed = false;", DEFROST_ASSIST_LOGIC)
        # De sessie omzeilt de normale CM2/CM3-timers en heeft daarom eigen
        # anti-pendelgrenzen.
        for substitution, default in (
            ('oq_boiler_defrost_assist_confirm_s: "10"', "uint32_t confirm_ms = 10000UL;"),
            ('oq_boiler_defrost_assist_min_run_s: "60"', "uint32_t minimum_ms = 60000UL;"),
            ('oq_boiler_defrost_assist_rearm_s: "120"', "uint32_t rearm_ms = 120000UL;"),
            ('oq_boiler_defrost_assist_release_s: "120"', "uint32_t release_ms = 120000UL;"),
            ('oq_boiler_defrost_assist_reference_settle_s: "300"', "uint32_t reference_settle_ms = 300000UL;"),
        ):
            self.assertIn(substitution, SUBSTITUTIONS)
            self.assertIn(default, DEFROST_ASSIST_LOGIC)
            self.assertIn("${" + substitution.split(":")[0] + "},", SUPERVISORY_YAML)
        # De eigenaarsvlag overleeft CM3 nooit.
        self.assertIn("if (desired != 3) id(oq_defrost_assist_active) = false;", SUPERVISORY_RUNTIME)

    def test_defrost_assist_confirms_the_flame_in_supervisory(self) -> None:
        # Een doel alleen laat een warmtebron niet branden. De sessie hoogt het
        # doel op tot de bron brandt; de tijden en stap zijn substituties.
        for substitution, default in (
            ('oq_boiler_defrost_assist_ignition_wait_s: "45"', "uint32_t ignition_wait_ms = 45000UL;"),
            ('oq_boiler_defrost_assist_raise_step_c: "2.0"', "float raise_step_c = 2.0f;"),
            ('oq_boiler_defrost_assist_raise_interval_s: "30"', "uint32_t raise_interval_ms = 30000UL;"),
            ('oq_boiler_defrost_assist_flame_loss_confirm_s: "20"', "uint32_t flame_loss_confirm_ms = 20000UL;"),
            ('oq_boiler_defrost_assist_raise_max_c: "8.0"', "float max_raise_c = 8.0f;"),
        ):
            self.assertIn(substitution, SUBSTITUTIONS)
            self.assertIn(default, DEFROST_ASSIST_LOGIC)
            self.assertIn("${" + substitution.split(":")[0] + "},", SUPERVISORY_YAML)
        self.assertIn("${oq_otb_field_timeout_s},", SUPERVISORY_YAML)
        # De terugkoppeling wordt in de supervisory gelezen. De ketelketen krijgt
        # nog steeds alleen een doeltemperatuur en weet niets van de vlam.
        self.assertIn("id(otb_flame_on).state", SUPERVISORY_RUNTIME)
        self.assertIn("id(otb_dhw_active).state", SUPERVISORY_RUNTIME)
        for chain_source in (DISPATCH_RUNTIME, DISPATCH_LOGIC):
            self.assertNotIn("flame", chain_source)
            self.assertNotIn("raise_c", chain_source)
            self.assertNotIn("flame_confirmed", chain_source)
            self.assertNotIn("otb_flame_on", chain_source)
        # Zonder verse status is er geen terugkoppeling, en alleen een werkelijk
        # doorgezette opdracht telt als wachten op de vlam.
        self.assertIn("oq_otb::telemetry_state.field_is_fresh(oq_otb::FIELD_STATUS, now_ms,", SUPERVISORY_RUNTIME)
        self.assertIn(
            'id(oq_defrost_assist_active) && strcmp(cur_cm, "CM3") == 0 && id(oq_otb_ch_enable).state;',
            SUPERVISORY_RUNTIME,
        )
        # Een storing van de warmtebron is geen reden om het doel te verhogen.
        self.assertIn("!(id(otb_fault_indication).has_state() && id(otb_fault_indication).state)", SUPERVISORY_RUNTIME)
        # Het beleid kent dezelfde grens als de dispatcher, die blijft begrenzen.
        self.assertIn(
            "oq_boiler_dispatch::supply_hold_target_ceiling(id(max_water_temp_limit_c).state)", SUPERVISORY_RUNTIME
        )
        self.assertIn("supply_hold_target_ceiling(in.maximum_water_temperature_c)", DISPATCH_LOGIC)


if __name__ == "__main__":
    unittest.main()
