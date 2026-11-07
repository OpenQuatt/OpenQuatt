"""Run real MQTT lifecycle methods against bounded, controlled RTOS/SDK waits.

The fixture replaces platform services, preference transactions and entity
publication, not the worker/cleanup/subscription/callback implementations. It is
an offline scheduling regression, not an ESP-IDF or hardware watchdog test.
"""

from pathlib import Path
import re
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process


ROOT = Path(__file__).resolve().parents[2]
CPP = ROOT / "components/openquatt_mqtt_config/OpenQuattMqttConfig.cpp"
HEADER = CPP.with_suffix(".h")
FIXTURE = ROOT / "tests/fixtures/mqtt_worker_nonblocking/runtime.cpp"
METHODS = (
    "loop", "apply_storage_", "get_desired_client_config_",
    "active_client_matches_", "ensure_client_worker_",
    "start_permanent_failure_cleanup_", "process_permanent_failure_cleanup_",
    "request_client_reconcile_", "notify_client_worker_", "reconcile_client_",
    "start_client_", "stop_client_", "clear_active_client_state_",
    "close_client_event_gate_", "client_worker_task_",
    "process_pending_input_subscriptions_", "subscribe_inputs_",
    "is_numeric_input_enabled_", "is_binary_input_enabled_",
    "find_numeric_input_index_by_topic_", "find_binary_input_index_by_topic_",
    "mqtt_event_handler_", "lock_config_", "unlock_config_", "lock_runtime_",
    "unlock_runtime_", "copy_string_field_",
)
STATIC_METHODS = {"client_worker_task_", "mqtt_event_handler_", "copy_string_field_"}


def extract_method(source, name):
    """Keep complete production bodies, ignoring braces in comments/literals."""
    matches = list(re.finditer(r"\bOpenQuattMqttConfig::" + re.escape(name) + r"\(", source))
    if len(matches) != 1:
        raise AssertionError(f"expected one production definition of {name}")
    start = source.rfind("\n", 0, matches[0].start()) + 1
    opening = source.index("{", matches[0].end())
    tokens = re.compile(r'''//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[{}]''')
    depth = 0
    for token in tokens.finditer(source, opening):
        if token.group() == "{":
            depth += 1
        elif token.group() == "}":
            depth -= 1
            if depth == 0:
                return source[start:token.end()]
    raise AssertionError(f"unterminated production method {name}")


def runtime_source(source):
    methods = [extract_method(source, name) for name in METHODS]
    declarations = []
    for name, method in zip(METHODS, methods):
        signature = method[:method.index("{")].replace("OpenQuattMqttConfig::", "").strip()
        declarations.append(("static " if name in STATIC_METHODS else "") + signature + ";")
    stack = re.search(r"#if OQ_HARDWARE_HEATPUMP_CONTROLLER_Q\n[^#]+#else\n[^#]+#endif", HEADER.read_text())
    if stack is None:
        raise AssertionError("production MQTT task stack selection missing")
    return (FIXTURE.read_text()
            .replace("// PRODUCTION_MQTT_TASK_STACK", stack.group())
            .replace("// PRODUCTION_DECLARATIONS", "\n".join(declarations))
            .replace("// PRODUCTION_METHODS", "\n\n".join(methods)))


class MqttWorkerNonblockingRuntimeTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.directory.cleanup)
        cls.binary = Path(cls.directory.name) / "mqtt_worker_nonblocking"
        result = run_process(
            compiler_command("-DOQ_HARDWARE_HEATPUMP_CONTROLLER_Q=0", "-DEXPECTED_MQTT_TASK_STACK=12288", "-pthread", "-I", str(ROOT), "-x", "c++", "-", "-o", str(cls.binary)),
            input=runtime_source(CPP.read_text()), text=True, capture_output=True,
            timeout=COMPILE_TIMEOUT_SECONDS,
        )
        if result.returncode:
            raise AssertionError(result.stderr[-10000:])

    def test_q_stack_reaches_sdk_during_start_and_reconfigure(self):
        binary = Path(self.directory.name) / "mqtt_worker_q"
        result = run_process(
            compiler_command("-DOQ_HARDWARE_HEATPUMP_CONTROLLER_Q=1",
                             "-DEXPECTED_MQTT_TASK_STACK=6144", "-pthread",
                             "-I", str(ROOT), "-x", "c++", "-", "-o", str(binary)),
            input=runtime_source(CPP.read_text()), text=True, capture_output=True,
            timeout=COMPILE_TIMEOUT_SECONDS,
        )
        self.assertEqual(result.returncode, 0, result.stderr[-10000:])
        for case in ("cold_start", "reconfigure"):
            result = run_process([str(binary), case], text=True, capture_output=True, timeout=12)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn(f"PASS {case}", result.stdout)

    def run_case(self, name, now_ms=None):
        command = [str(self.binary), name]
        if now_ms is not None:
            command.append(str(now_ms))
        result = run_process(command, text=True, capture_output=True, timeout=12)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(f"PASS {name}", result.stdout)

    def test_non_q_cold_start_uses_existing_stack(self):
        self.run_case("cold_start")

    def test_loop_returns_while_worker_subscribe_waits(self):
        self.run_case("blocked_subscribe")

    def test_reconnect_without_deadline_survives_long_uptime(self):
        for now_ms in (0x7fffffff, 0x80000000, 0xffffffff, 0):
            with self.subTest(now_ms=hex(now_ms)):
                self.run_case("reconnect_at_uptime", now_ms)

    def test_reconnect_respects_retry_deadline_across_time_boundaries(self):
        for now_ms in (0x7fffffff, 0x80000000, 0xffffffff, 0):
            with self.subTest(now_ms=hex(now_ms)):
                self.run_case("reconnect_with_retry_at_uptime", now_ms)

    def test_terminal_cleanup_without_deadline_survives_long_uptime(self):
        for now_ms in (0x7fffffff, 0x80000000, 0xffffffff, 0):
            with self.subTest(now_ms=hex(now_ms)):
                self.run_case("cleanup_at_uptime", now_ms)

    def test_enable_before_worker_idle_is_not_lost(self):
        self.run_case("idle_handoff")

    def test_reconfigure_drops_old_callbacks_during_subscribe(self):
        self.run_case("reconfigure")

    def test_offline_enable_survives_worker_pending_flag_clear(self):
        self.run_case("offline_handoff")

    def test_terminal_cleanup_retries_and_ignores_queued_notification(self):
        self.run_case("terminal_cleanup")

    def test_invalid_worker_region_cannot_recurse_or_stop_on_main(self):
        self.run_case("invalid_region")

    def test_cleanup_during_init_never_opens_the_partial_client_gate(self):
        self.run_case("partial_init")


if __name__ == "__main__":
    unittest.main()
