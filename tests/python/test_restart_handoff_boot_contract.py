from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPONENT = ROOT / "components" / "openquatt_incident_manager"
CODEGEN = (COMPONENT / "__init__.py").read_text()
HANDOFF = (COMPONENT / "OpenQuattRestartHandoff.cpp").read_text()
MANAGER = (COMPONENT / "OpenQuattIncidentManager.cpp").read_text()


def function_body(source: str, signature: str) -> str:
    opening = source.index("{", source.index(signature))
    depth = 0
    for index in range(opening, len(source)):
        depth += (source[index] == "{") - (source[index] == "}")
        if depth == 0:
            return source[opening + 1:index]
    raise AssertionError(f"Unclosed function: {signature}")


class RestartHandoffBootContractTest(unittest.TestCase):
    def test_failed_consume_cannot_manufacture_a_software_reset(self) -> None:
        codegen = CODEGEN[CODEGEN.index("async def to_code("):CODEGEN.index("async def _runtime_to_code(")]
        initialize = function_body(HANDOFF, "bool initialize_restart_handoff(")
        self.assertIn("cg.add(initialize)", codegen)
        for text in (codegen, initialize):
            for reset in ("arch_restart(", "safe_reboot(", "App.reboot("):
                self.assertNotIn(reset, text)

    def test_storage_retries_use_one_original_boot_context(self) -> None:
        initialize = function_body(HANDOFF, "bool initialize_restart_handoff(")
        self.assertEqual(initialize.count("capture_boot_context("), 1)
        self.assertLess(initialize.index("capture_boot_context("), initialize.index("for ("))
        self.assertIn("attempt < 3U", initialize)
        self.assertIn("consume_record(storage, context,", initialize)
        rejected = function_body(initialize, "if (!consumed)")
        self.assertIn("return false;", rejected)
        self.assertLess(initialize.index("storage_ready = false;"), initialize.index("capture_boot_context("))
        self.assertLess(initialize.index("if (!consumed)"), initialize.index("storage_ready = true;"))

    def test_unavailable_storage_keeps_control_and_manual_restart_inhibited(self) -> None:
        restart = function_body(MANAGER, "void OpenQuattIncidentManager::perform_restart_(")
        rejected = function_body(restart, "if (!restart_handoff_storage_ready())")
        self.assertIn("return;", rejected)
        self.assertLess(restart.index("if (!restart_handoff_storage_ready())"), restart.index("App.safe_reboot();"))
        outputs = function_body(MANAGER, "oq_incidents::DerivedOutputs OpenQuattIncidentManager::outputs_for_slot_(")
        self.assertIn("!restart_handoff_storage_ready()", outputs)
        failed_storage_gate = outputs[outputs.index("if (this->restarting_"):]
        self.assertIn("outputs.available_for_start = false;", failed_storage_gate)
        self.assertIn("outputs.must_stop = true;", failed_storage_gate)

    def test_incomplete_consume_marker_cannot_turn_into_positive_restart_intent(self) -> None:
        initialize = function_body(HANDOFF, "bool initialize_restart_handoff(")
        self.assertIn("RTC_NOINIT_ATTR volatile uint32_t consume_incomplete;", HANDOFF)
        begin = initialize.index("begin_boot_consume(consume_incomplete)")
        first_barrier = initialize.index("__sync_synchronize();", begin)
        capture = initialize.index("capture_boot_context(")
        deny = initialize.index("if (!previous_consume_complete) context.software_reset = false;")
        retry = initialize.index("for (")
        rejected = initialize.index("if (!consumed)")
        finish = initialize.index("finish_boot_consume(consume_incomplete)")
        final_barrier = initialize.index("__sync_synchronize();", finish)
        ready = initialize.index("storage_ready = true;")
        positions = [begin, first_barrier, capture, deny, retry, rejected, finish, final_barrier, ready]
        self.assertEqual(positions, sorted(positions))
        self.assertEqual(initialize.count("finish_boot_consume("), 1)


if __name__ == "__main__":
    unittest.main()
