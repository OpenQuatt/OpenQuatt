from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]


def source(component, filename):
    return (ROOT / "components" / component / filename).read_text()


class RecoveryDeferredContractTest(unittest.TestCase):
    def test_direct_entity_actions_are_separated_by_cpu_reset(self):
        self.assertFalse((ROOT / "components/web_server").exists())
        recovery = source("openquatt_recovery", "OpenQuattRecovery.cpp")
        self.assertIn("httpd_queue_work", recovery)
        self.assertIn("barrier_ready_.load", recovery)
        self.assertIn("barrier_seen_", recovery)
        self.assertIn("this->restart_(recovery)", recovery)
        self.assertNotIn("end_recovery_guard", recovery)

    def test_runtime_editor_cancels_before_queue_and_after_safety_read(self):
        cpp = source("openquatt_odu_runtime_frequency", "OpenQuattOduRuntimeFrequency.cpp")
        loop = cpp.split("void OpenQuattOduRuntimeFrequency::loop()", 1)[1].split(
            "void OpenQuattOduRuntimeFrequency::finish_without_write_", 1)[0]
        self.assertLess(loop.index("normal_web_access_allowed"), loop.index("this->queue_guard_("))
        self.assertIn('finish_without_write_("Request cancelled by recovery"', loop)
        write = cpp.split("void OpenQuattOduRuntimeFrequency::begin_write_", 1)[1].split(
            "void OpenQuattOduRuntimeFrequency::queue_write_register_", 1)[0]
        self.assertLess(write.index("normal_web_access_allowed"), write.index("this->write_started_ = true"))

    def test_settings_cancel_web_queue_before_persistence_not_autonomous_reconcile(self):
        cpp = source("openquatt_odu_settings", "OpenQuattOduSettings.cpp")
        loop = cpp.split("void OpenQuattOduSettings::loop()", 1)[1]
        self.assertIn("pending != PendingAction::RECONCILE", loop)
        self.assertLess(loop.index("normal_web_access_allowed"), loop.index("this->persist_profile_(profile)"))
        self.assertIn('finish_operation_("RECOVERY_CANCELLED", request_token)', loop)
        read = cpp.split("void OpenQuattOduSettings::handle_settings_read_", 1)[1].split(
            "void OpenQuattOduSettings::queue_next_write_", 1)[0]
        self.assertIn("operation == Operation::APPLY", read)
        self.assertLess(read.index("normal_web_access_allowed"), read.index("this->queue_next_write_("))
        self.assertIn("this->manual_apply_pending_.store(false", read)

    def test_incident_cancellation_completes_request_instead_of_executing(self):
        cpp = source("openquatt_incident_manager", "OpenQuattIncidentManager.cpp")
        for name, action in (("defer_start_failure_retry", "start_failure_retry"),
                             ("defer_odu_power_cycle_confirmation", "confirm_odu_power_cycle")):
            method = cpp.split(f"OpenQuattIncidentManager::{name}(", 1)[1].split("\n}\n", 1)[0]
            self.assertIn("!openquatt_web_auth::normal_web_access_allowed()", method)
            self.assertIn(f'record_action_result_(*unit, "{action}", "recovery_cancelled"', method)
            self.assertIn("this->publish_snapshot_(millis());\n      return;", method)


if __name__ == "__main__":
    unittest.main()
