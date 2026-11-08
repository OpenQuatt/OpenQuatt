import re
import unittest
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
RUNTIME = (ROOT / "openquatt/includes/control/oq_heating_curve_runtime.h").read_text()
def compact(text: str) -> str:
    return re.sub(r"\s+", " ", text)
class HeatingCurveDuoFloorContractTest(unittest.TestCase):
    def test_floor_gate_uses_single_owner_and_only_the_normal_duo_path(self) -> None:
        source = compact(RUNTIME)
        self.assertIn("const bool owner_hp1 = single_owner != 2;", source)
        self.assertIn("const int owner_level = owner_hp1 ? previous_hp1 : previous_hp2;", source)
        self.assertIn(
            "oq_curve::duo_floor_reached({owner_level, owner_request, "
            "(owner_hp1 ? hp1_candidate : hp2_candidate).available_for_start && "
            "!(owner_hp1 ? hp1_valve_defrost : hp2_valve_defrost), level_power_w(owner_hp1, owner_level), duo_floor_w});",
            source)
        self.assertIn("duo_floor_w = std::fmin(duo_floor_w, item.power_w);", source)
        self.assertIn("(dual_defrost || (!defrost_active && duo_better && floor_reached &&", source)
        self.assertIn("dispatch_u >= duo_enable_min_u && regular_duo_needed &&", source)
        self.assertEqual(len(re.findall(r"\bfloor_reached\b", source)), 2)
if __name__ == "__main__":
    unittest.main()
