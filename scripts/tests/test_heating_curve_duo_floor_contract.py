import re
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
RUNTIME = (ROOT / "openquatt/includes/control/oq_heating_curve_runtime.h").read_text()
HELPER = (ROOT / "openquatt/includes/control/oq_heating_curve_duo_floor_logic.h").read_text()
def compact(text: str) -> str:
    return re.sub(r"\s+", " ", text)
class HeatingCurveDuoFloorContractTest(unittest.TestCase):
    def test_floor_gate_uses_single_owner_and_only_the_normal_duo_path(self) -> None:
        source = compact(RUNTIME)
        self.assertIn("const bool owner_hp1 = single_owner != 2;", source)
        self.assertIn("const int owner_level = owner_hp1 ? previous_hp1 : previous_hp2;", source)
        self.assertIn(
            "oq_curve::duo_floor_reached({owner_level, owner_hp1 ? best_single.hp1_level : best_single.hp2_level, "
            "(owner_hp1 ? hp1_candidate : hp2_candidate).available_for_start && "
            "!(owner_hp1 ? hp1_valve_defrost : hp2_valve_defrost), level_power_w(owner_hp1, owner_level), duo_floor_w});",
            source)
        self.assertIn("duo_floor_w = std::fmin(duo_floor_w, item.power_w);", source)
        self.assertIn("(forced_capacity || dual_defrost || (duo_better && floor_reached && dispatch_u >= duo_enable_min_u &&", source)
        self.assertEqual(len(re.findall(r"\bfloor_reached\b", source)), 2)
    def test_duo_floor_helper_stays_within_line_budget(self) -> None:
        # The helper lives outside the heating-curve runtime budgets; keep its own growth explicit.
        self.assertLessEqual(len(HELPER.splitlines()), 27)
if __name__ == "__main__":
    unittest.main()
