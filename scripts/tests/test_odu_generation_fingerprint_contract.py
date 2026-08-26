from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
HP_IO = (ROOT / "openquatt" / "oq_HP_io.yaml").read_text()
GENERATION_HEADER = (
    ROOT / "openquatt" / "includes" / "odu" / "oq_odu_generation.h"
).read_text()


def source_block(source: str, start_marker: str, end_marker: str) -> str:
    start = source.index(start_marker)
    end = source.index(end_marker, start)
    return source[start:end]


class OduGenerationFingerprintContractTest(unittest.TestCase):
    def test_registers_and_word_offsets_match_device_addressing(self) -> None:
        self.assertIn("CORE_IDENTITY_REGISTER = 2114U", GENERATION_HEADER)
        self.assertIn("CORE_IDENTITY_REGISTER_COUNT = 14U", GENERATION_HEADER)
        self.assertIn("COMPRESSOR_CODE_WORD_OFFSET = 0U", GENERATION_HEADER)
        self.assertIn("PCB_PROGRAM_WORD_OFFSET = 8U", GENERATION_HEADER)
        self.assertIn("CONTROL_BOARD_ITEM_WORD_OFFSET = 13U", GENERATION_HEADER)
        self.assertIn("CUSTOMER_MODEL_REGISTER = 11160U", GENERATION_HEADER)
        self.assertIn("CUSTOMER_MODEL_REGISTER_COUNT = 2U", GENERATION_HEADER)

    def test_ambiguous_board_requires_exact_additional_fingerprint(self) -> None:
        self.assertIn("CONTROL_BOARD_ITEM_V1_5_OR_V2_OLD_MODEL = 0x0E37U", GENERATION_HEADER)
        self.assertIn("PCB_PROGRAM_V1_5 = 0x011EU", GENERATION_HEADER)
        self.assertIn("PCB_PROGRAM_V2_OLD_MODEL = 0x0122U", GENERATION_HEADER)
        self.assertIn("COMPRESSOR_CODE_V2 = 2825U", GENERATION_HEADER)
        self.assertIn("CUSTOMER_MODEL_AM_WORD = 0x414DU", GENERATION_HEADER)
        self.assertIn("CUSTOMER_MODEL_H6_WORD = 0x4836U", GENERATION_HEADER)
        self.assertIn("core.pcb_program == PCB_PROGRAM_V2_OLD_MODEL", GENERATION_HEADER)
        self.assertIn("core.compressor_code == COMPRESSOR_CODE_V2", GENERATION_HEADER)
        self.assertIn("core.pcb_program == PCB_PROGRAM_V1_5", GENERATION_HEADER)
        self.assertIn("core.compressor_code == COMPRESSOR_CODE_V1_5", GENERATION_HEADER)

    def test_boot_and_manual_detection_share_one_shot_script(self) -> None:
        boot = source_block(HP_IO, "esphome:\n", "# ----------------------------\n# MODBUS CONTROLLER")
        self.assertIn("script.execute: ${hp_id}_detect_odu_generation_once", boot)

        button = source_block(
            HP_IO,
            "id: ${hp_id}_detect_odu_generation",
            "# ----------------------------\n# SELECT",
        )
        self.assertIn("script.execute: ${hp_id}_detect_odu_generation_once", button)

    def test_detection_is_read_only_conditional_and_late_callback_safe(self) -> None:
        detection = source_block(
            HP_IO,
            "id: ${hp_id}_detect_odu_generation_once",
            "id: ${hp_id}_detect_compressor_level_profile_once",
        )
        self.assertIn("oq_odu::CORE_IDENTITY_REGISTER", detection)
        self.assertIn("oq_odu::CORE_IDENTITY_REGISTER_COUNT", detection)
        self.assertIn("oq_odu::requires_customer_model(core)", detection)
        self.assertIn("oq_odu::CUSTOMER_MODEL_REGISTER", detection)
        self.assertIn("oq_odu::CUSTOMER_MODEL_REGISTER_COUNT", detection)
        token_guards = re.findall(
            r"request_token\s*!=\s*id\(\$\{hp_id\}_odu_generation_request_token\)",
            detection,
        )
        self.assertGreaterEqual(len(token_guards), 2)
        self.assertGreaterEqual(
            detection.count("!id(${hp_id}_odu_generation_request_pending)"),
            3,
        )
        self.assertIn("id(${hp_id}_odu_eeprom_dump).is_active()", detection)
        self.assertIn("id(oq_runtime_polling_paused).state", detection)
        self.assertIn("next_request_token", detection)
        self.assertNotIn("create_write", detection)
        self.assertNotIn("3050", detection)
        self.assertNotIn("compressor_level", detection)

    def test_customer_model_is_not_periodically_polled(self) -> None:
        control_board = source_block(
            HP_IO,
            "id: ${hp_id}_control_board_item",
            "id: ${hp_id}_condensing_temp",
        )
        self.assertNotIn("on_value:", control_board)

        customer_model = source_block(
            HP_IO,
            "id: ${hp_id}_customer_model_code",
            "id: ${hp_id}_working_mode_label",
        )
        self.assertIn("internal: true", customer_model)
        self.assertIn("update_interval: never", customer_model)
        self.assertNotIn("platform: modbus_controller", customer_model)


if __name__ == "__main__":
    unittest.main()
