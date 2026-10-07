"""Exercise the production NVS cleanup helper with failing NVS peripherals."""

from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]


class NvsCleanupRuntimeTest(unittest.TestCase):
    def test_targeted_cleanup_and_failure_boundaries(self) -> None:
        compiler = shutil.which(os.environ.get("CXX", "c++"))
        self.assertIsNotNone(compiler, "A C++ compiler is required")
        nvs_stub = r'''
#pragma once
#include <cstddef>
#include <cstdint>
using esp_err_t = int;
using nvs_handle_t = int;
constexpr int ESP_OK=0, ESP_ERR_NVS_NOT_FOUND=1, ESP_ERR_NVS_TYPE_MISMATCH=2;
constexpr int NVS_READONLY=0, NVS_READWRITE=1;
struct nvs_stats_t {
  size_t used_entries=0, free_entries=0, available_entries=0, total_entries=0, namespace_count=0;
};
const char* esp_err_to_name(int);
int nvs_open(const char*, int, int*);
int nvs_get_blob(int, const char*, void*, size_t*);
int nvs_erase_key(int, const char*);
int nvs_commit(int);
void nvs_close(int);
int nvs_get_stats(const char*, nvs_stats_t*);
'''
        harness = r'''
#include <cassert>
#include <map>
#include <string>
#include "openquatt/includes/storage/oq_nvs_cleanup.h"
struct Record { size_t length; bool blob=true; };
std::map<std::string, Record> records;
bool namespace_exists=true;
int open_read_error=0, open_write_error=0, read_error=0, erase_error=0, commit_error=0;
int handles=0, readonly_opens=0, write_opens=0, erases=0, commits=0;
const char* esp_err_to_name(int) { return "stub"; }
int nvs_open(const char* ns, int mode, int* handle) {
  assert(std::string(ns)=="esphome");
  if (mode==NVS_READONLY) {
    ++readonly_opens;
    if (!namespace_exists) return ESP_ERR_NVS_NOT_FOUND;
    if (open_read_error) return open_read_error;
  } else {
    assert(mode==NVS_READWRITE); ++write_opens;
    if (open_write_error) return open_write_error;
    namespace_exists=true;
  }
  ++handles; *handle=mode+1; return ESP_OK;
}
int nvs_get_blob(int handle, const char* key, void* payload, size_t* length) {
  assert(handle==1 && payload==nullptr);  // No credential/report payload read.
  if (read_error) return read_error;
  auto it=records.find(key);
  if (it==records.end()) return ESP_ERR_NVS_NOT_FOUND;
  if (!it->second.blob) return ESP_ERR_NVS_TYPE_MISMATCH;
  *length=it->second.length; return ESP_OK;
}
int nvs_erase_key(int handle, const char* key) {
  assert(handle==2); ++erases;
  if (erase_error) return erase_error;
  return records.erase(key) ? ESP_OK : ESP_ERR_NVS_NOT_FOUND;
}
int nvs_commit(int handle) { assert(handle==2); ++commits; return commit_error; }
void nvs_close(int) { --handles; assert(handles==0); }
int nvs_get_stats(const char* partition, nvs_stats_t*) { assert(partition==nullptr); return ESP_OK; }
constexpr uint32_t CRASH=752195988U, OLD_API=1156115452U, NOISE=88491486U;
uint32_t fnv1(const char* text) {
  uint32_t hash=2166136261U;
  while (*text) { hash*=16777619U; hash^=static_cast<uint8_t>(*text++); }
  return hash;
}
bool cleanup(uint32_t key=CRASH, size_t size=2812) {
  bool ok=oq_nvs_cleanup::erase_esphome_blob_if_size(key,size,"test");
  assert(handles==0); return ok;
}
void reset() {
  records.clear(); namespace_exists=true;
  open_read_error=open_write_error=read_error=erase_error=commit_error=0;
  readonly_opens=write_opens=erases=commits=handles=0;
}
int main() {
  assert(fnv1("openquatt_crash_telemetry_record")==CRASH);
  assert(fnv1("openquatt_api_security_store")==OLD_API);
  assert(fnv1("ram_log_history")==306736601U);
  const auto state=std::to_string(fnv1("openquatt_crash_telemetry_state"));
  reset(); namespace_exists=false;
  assert(cleanup() && !namespace_exists && write_opens==0);
  reset(); assert(cleanup() && write_opens==0);
  reset(); open_read_error=3; assert(!cleanup() && write_opens==0);
  reset(); records[std::to_string(CRASH)]={2812}; read_error=3;
  assert(!cleanup() && write_opens==0 && records.size()==1);
  reset(); records[std::to_string(CRASH)]={2812,false};
  assert(!cleanup() && write_opens==0 && records.size()==1);
  reset(); records[std::to_string(CRASH)]={56};
  assert(!cleanup() && write_opens==0 && records.size()==1);
  reset(); records[std::to_string(CRASH)]={2812}; open_write_error=3;
  assert(!cleanup() && erases==0 && records.size()==1);
  reset(); records[std::to_string(CRASH)]={2812}; erase_error=3;
  assert(!cleanup() && commits==0 && records.size()==1);
  erase_error=0; assert(cleanup() && records.empty() && commits==1);
  reset(); records[std::to_string(CRASH)]={2812}; commit_error=3;
  assert(!cleanup() && commits==1);
  // NVS may have already persisted erase when commit reports failure.
  // Retrying must be harmless, whether the key survived or disappeared.
  commit_error=0; assert(cleanup());
  records[std::to_string(CRASH)]={2812}; assert(cleanup());
  reset(); records[std::to_string(CRASH)]={2812};
  records[std::to_string(OLD_API)]={40}; records[std::to_string(NOISE)]={32};
  records[state]={56}; records["current-setting"]={4};
  assert(cleanup() && cleanup(OLD_API,40));
  assert(records.size()==3 && records.contains(std::to_string(NOISE)));
  assert(records.contains(state) && records.contains("current-setting"));
  int writes=write_opens;
  assert(cleanup() && cleanup(OLD_API,40) && write_opens==writes);
  // Exercise the aggregate boot path, then the duplicate/next-boot call.
  reset();
  esphome::EntityBase air;
  records["123"]={1};
  for (auto key : {3739268635U, 2888263739U, 556120657U, 3778223577U,
                   3433032332U, 390691303U, 393124903U, 3948348002U}) {
    records[std::to_string(key)]={4};
  }
  records[std::to_string(CRASH)]={2812};
  records[std::to_string(OLD_API)]={40}; records[std::to_string(NOISE)]={32};
  records[state]={56}; records["current-setting"]={4};
  records["306736601"]={1}; records["2881445393"]={4};
  records["1275799272"]={4};
  records["515187816"]={1}; records["3865822963"]={1};
  oq_nvs_cleanup::retire_openquatt_preferences(&air);
  assert(handles==0 && records.size()==3 && commits==9);
  assert(records.contains(std::to_string(NOISE)) && records.contains(state));
  assert(records.contains("current-setting"));
  oq_nvs_cleanup::retire_openquatt_preferences(&air);
  assert(handles==0 && records.size()==3 && commits==9);
  // Unexpected records under the retired keys are preserved by the boot path.
  records["306736601"]={4}; records["2881445393"]={4,false};
  records["1275799272"]={8};
  records["515187816"]={4}; records["3865822963"]={1,false};
  oq_nvs_cleanup::retire_openquatt_preferences(&air);
  assert(handles==0 && records.size()==8 && commits==9);
  // Single-key retries use the same failure boundaries as the old cleanup.
  reset(); records["306736601"]={1}; records["2881445393"]={4};
  records["1275799272"]={4};
  records["515187816"]={1}; records["3865822963"]={1};
  erase_error=3;
  assert(!cleanup(306736601U,1) && !cleanup(2881445393U,4));
  assert(!cleanup(1275799272U,4));
  assert(!cleanup(515187816U,1) && !cleanup(3865822963U,1));
  assert(records.size()==5 && commits==0);
  erase_error=0;
  assert(cleanup(306736601U,1) && cleanup(2881445393U,4));
  assert(cleanup(1275799272U,4));
  assert(cleanup(515187816U,1) && cleanup(3865822963U,1));
  assert(records.empty() && commits==5);
}
'''
        with tempfile.TemporaryDirectory(prefix="openquatt-nvs-cleanup-") as directory:
            temp = Path(directory)
            (temp / "nvs.h").write_text(nvs_stub)
            (temp / "nvs_flash.h").write_text('#pragma once\n#include "nvs.h"\n')
            core = temp / "esphome/core"
            core.mkdir(parents=True)
            (core / "entity_base.h").write_text(
                "#pragma once\nnamespace esphome { struct EntityBase { "
                "unsigned get_preference_hash() { return 123; } }; }\n"
            )
            (core / "log.h").write_text(
                "#pragma once\ntemplate<typename... T> void test_log(T...) {}\n"
                "#define ESP_LOGW(...) test_log(__VA_ARGS__)\n"
                "#define ESP_LOGE(...) test_log(__VA_ARGS__)\n"
                "#define ESP_LOGI(...) test_log(__VA_ARGS__)\n"
            )
            unit, binary = temp / "test.cpp", temp / "test"
            unit.write_text(harness)
            args = [compiler, "-std=c++20", "-Wall", "-Wextra", "-Werror", "-I", str(temp), "-I", str(ROOT)]
            if os.uname().sysname == "Darwin":
                sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
                args += ["-isystem", str(Path(sdk) / "usr/include/c++/v1")]
            compiled = subprocess.run(args + [str(unit), "-o", str(binary)], capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(executed.returncode, 0, executed.stderr)


if __name__ == "__main__":
    unittest.main()
