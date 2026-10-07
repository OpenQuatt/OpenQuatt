"""Compile the real component against fault-injected NVS/ESPHome adapters."""

from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
STUBS = {
    "esphome/core/component.h": r'''
#pragma once
namespace esphome {
namespace setup_priority { constexpr float HARDWARE = 800.0f; }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual void on_shutdown() {}
  virtual float get_setup_priority() const { return 0.0f; }
  void status_set_warning(const char*) { warning = true; }
  void status_clear_warning() { warning = false; }
  bool warning{false};
};
}
''',
    "esphome/components/number/number.h": r'''
#pragma once
#include <cstdint>
#include <functional>
namespace esphome { namespace number {
class Number {
 public:
  virtual ~Number() = default;
  void publish_state(float value) { state = value; if (callback) callback(value); }
  uint32_t get_preference_hash() const { return key; }
  void set(float value) { control(value); }
  float state{0.0f};
  uint32_t key{0U};
  std::function<void(float)> callback;
 protected:
  virtual void control(float value) = 0;
};
}}
''',
    "esphome/core/hal.h": r'''
#pragma once
#include <cstdint>
extern uint32_t fake_millis;
namespace esphome { inline uint32_t millis() { return fake_millis; } }
''',
    "esphome/core/log.h": r'''
#pragma once
#define ESP_LOGW(tag, ...) ((void)(tag))
#define ESP_LOGCONFIG(tag, ...) ((void)(tag))
#define LOG_NUMBER(prefix, label, number) ((void)(number))
''',
    "freertos/FreeRTOS.h": r'''
#pragma once
#include <mutex>
using portMUX_TYPE = std::mutex;
#define portMUX_INITIALIZER_UNLOCKED {}
#define portENTER_CRITICAL(mux) (mux)->lock()
#define portEXIT_CRITICAL(mux) (mux)->unlock()
''',
    "freertos/task.h": r'''
#pragma once
#include <thread>
using TaskHandle_t = std::thread::id;
inline TaskHandle_t xTaskGetCurrentTaskHandle() { return std::this_thread::get_id(); }
''',
    "nvs.h": r'''
#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
using nvs_handle_t = unsigned;
constexpr int ESP_OK = 0;
constexpr int ESP_ERR_NVS_NOT_FOUND = 1;
constexpr int ESP_ERR_NVS_TYPE_MISMATCH = 2;
constexpr int ESP_ERR_NVS_NOT_INITIALIZED = 3;
constexpr int ESP_ERR_NVS_NOT_ENOUGH_SPACE = 4;
constexpr int ESP_FAIL = 5;
constexpr int NVS_READONLY = 0;
constexpr int NVS_READWRITE = 1;
esp_err_t nvs_open(const char*, int, nvs_handle_t*);
esp_err_t nvs_get_blob(nvs_handle_t, const char*, void*, size_t*);
esp_err_t nvs_set_blob(nvs_handle_t, const char*, const void*, size_t);
esp_err_t nvs_erase_key(nvs_handle_t, const char*);
esp_err_t nvs_commit(nvs_handle_t);
void nvs_close(nvs_handle_t);
inline const char* esp_err_to_name(esp_err_t) { return "injected"; }
''',
}


class CompressorLimitsRuntimeTest(unittest.TestCase):
    def test_real_runtime_failure_and_shutdown_boundaries(self):
        compiler = shutil.which("c++")
        if not compiler:
            self.skipTest("C++ compiler required")
        with tempfile.TemporaryDirectory(prefix="oq-compressor-limits-") as directory:
            temporary = Path(directory)
            for relative, source in STUBS.items():
                target = temporary / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_text(source)
            arguments = [compiler, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-pthread"]
            if sys.platform == "darwin":
                sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
                arguments += ["-isystem", str(Path(sdk) / "usr/include/c++/v1")]
            arguments += [
                "-I", str(temporary), "-I", str(ROOT),
                str(ROOT / "scripts/tests/fixtures/compressor_limits/runtime_test.cpp"),
                str(ROOT / "components/openquatt_compressor_limits/openquatt_compressor_limits.cpp"),
                "-o", str(temporary / "runtime_test"),
            ]
            subprocess.run(arguments, check=True, capture_output=True, text=True)
            subprocess.run([str(temporary / "runtime_test")], check=True, timeout=20)


if __name__ == "__main__":
    unittest.main()
