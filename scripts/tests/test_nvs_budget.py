from pathlib import Path
import contextlib
import io
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts"))
import check_nvs_budget as budget  # noqa: E402


def q_config(hp_count: int = 2) -> dict:
    config = {component: {} for component, _, _ in budget.CUSTOM_PREFERENCE_LAYOUTS}
    config["openquatt_odu_settings"] = [{} for _ in range(hp_count)]
    config["openquatt_odu_defrost"] = [{} for _ in range(hp_count)]
    config.update(wifi={}, api={"encryption": {}}, safe_mode={}, factory_reset={"resets_required": 3})
    return config


class NvsBudgetTest(unittest.TestCase):
    def test_custom_budget_tracks_configured_components_and_hp_instances(self) -> None:
        self.assertEqual(sum(budget.estimate_custom_preferences({}).values()), 0)
        self.assertEqual(sum(budget.estimate_custom_preferences(q_config(1)).values()), 55)
        self.assertEqual(sum(budget.estimate_custom_preferences(q_config(2)).values()), 64)
        config = q_config()
        del config["openquatt_debug_recorder"]
        self.assertEqual(sum(budget.estimate_custom_preferences(config).values()), 61)
        self.assertEqual(budget.estimate_custom_preferences({"openquatt_odu_defrost": []}), {})

    def test_phy_noise_factory_reset_and_namespaces_are_not_hidden_in_margin(self) -> None:
        entries = budget.estimate_system_preferences(q_config())
        self.assertEqual(entries["PHY calibration"], 63)
        self.assertEqual(entries["PHY version"], 1)
        self.assertEqual(entries["API Noise PSK"], 3)
        self.assertEqual(entries["factory reset"], 3)
        self.assertEqual(sum(entries.values()), 88)
        config = q_config()
        config["esp32"] = {"framework": {"sdkconfig_options": {"CONFIG_ESP_PHY_CALIBRATION_AND_DATA_STORAGE": False}}}
        self.assertNotIn("PHY calibration", budget.estimate_system_preferences(config))
        del config["wifi"]
        self.assertNotIn("WiFi settings", budget.estimate_system_preferences(config))
        config["safe_mode"]["disabled"] = True
        self.assertNotIn("safe mode", budget.estimate_system_preferences(config))
        del config["api"]["encryption"]
        self.assertNotIn("API Noise PSK", budget.estimate_system_preferences(config))

    def test_blob_chunk_overhead(self) -> None:
        self.assertEqual(budget.blob_entries(1904), 62)
        self.assertEqual(budget.blob_entries(1904, chunk_count=2), 63)
        with self.assertRaises(ValueError):
            budget.blob_entries(-1)
        with self.assertRaises(ValueError):
            budget.blob_entries(32, chunk_count=0)

    def test_known_full_population_shortage_fails_without_lowering_policy(self) -> None:
        config = q_config()
        with tempfile.TemporaryDirectory() as directory:
            partition = Path(directory) / "partitions.csv"
            partition.write_text("nvs,data,nvs,0x9000,0x6000\n")
            config["esp32"] = {"partitions": str(partition)}
            with patch.object(budget, "load_validated_config", return_value=config), patch.object(
                budget, "estimate_entity_preferences", return_value=({"entities": 433}, {"entities": 142})
            ), contextlib.redirect_stdout(io.StringIO()) as output:
                result = budget.check_config(Path("unused.yaml"))
            self.assertEqual(result, 1)
            self.assertIn("estimated=585 available=45 required=100", output.getvalue())
            self.assertIn("NVS budget: FAIL", output.getvalue())
            self.assertEqual(budget.REQUIRED_AVAILABLE_ENTRIES, 100)


if __name__ == "__main__":
    unittest.main()
