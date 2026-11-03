"""Execute production write lambdas with ESPHome 2026.10.0 ModbusSelect::control.

The verified complete SDK select.cpp is compiled unchanged against small host
peripheral/queue stubs. This tests its real clear-before-lambda control path,
not a complete ESPHome/Modbus hub or hardware. Production command, incident and
normal defrost permission code is reused without replacing the safety gates.
"""
from pathlib import Path
import hashlib
import importlib.util
import os
import re
import tempfile
import textwrap
import unittest

from tests.python.test_manual_hp_start_runtime import fixture_prefix
from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]
SDK_SHA256 = "28e226d167b5a56f6ab3029dc8c75fb3928b40edc4976ae415091b22dd0b3740"


def sdk_select_source():
    """Locate an installed, explicitly verified SDK; never download or build it."""
    packages = []
    if os.environ.get("OQ_ESPHOME_PACKAGE"):
        packages.append(Path(os.environ["OQ_ESPHOME_PACKAGE"]))
    spec = importlib.util.find_spec("esphome")
    if spec and spec.origin:
        packages.append(Path(spec.origin).parent)
    # Support the repository's bootstrapped environment without depending on a
    # machine-specific cache or another checkout.
    packages.extend(ROOT.glob(".venv/lib/python*/site-packages/esphome"))
    for package in packages:
        source = package / "components/modbus_controller/select/modbus_select.cpp"
        metadata = package.parent / "esphome-2026.10.0.dist-info/METADATA"
        if source.is_file() and metadata.is_file() and "Version: 2026.10.0\n" in metadata.read_text():
            data = source.read_bytes()
            if hashlib.sha256(data).hexdigest() == SDK_SHA256:
                return data
    raise AssertionError("Verified ESPHome 2026.10.0 modbus_select.cpp unavailable; set OQ_ESPHOME_PACKAGE to its package directory")


SDK_STUB = r'''#pragma once
#include <algorithm>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <vector>
namespace esphome {
template<class T> using optional = std::optional<T>;
namespace modbus {
using RegisterValues = std::vector<uint16_t>;
namespace helpers {
inline optional<int64_t> payload_to_number(std::span<const uint8_t>, int, size_t, uint32_t) { return 0; }
inline void number_to_payload(RegisterValues& words, int64_t value, int) { words.push_back(static_cast<uint16_t>(value)); }
}
}
namespace modbus_controller {
class ModbusSelect {
 public:
  struct Frame { uint16_t address; uint16_t value; };
  using Transform = std::function<optional<std::string>(ModbusSelect*, int64_t, std::span<const uint8_t>)>;
  using WriteTransform = std::function<optional<int64_t>(ModbusSelect*, std::string, int64_t, modbus::RegisterValues&)>;
  std::vector<int64_t> mapping_;
  std::vector<std::string> options;
  optional<Transform> transform_func_;
  optional<WriteTransform> write_transform_func_;
  std::function<void(size_t)> on_publish;
  std::vector<Frame> queue;
  unsigned clear_calls{0};
  int sensor_value_type{0}; size_t offset{0}; uint32_t bitmask{0xffff}; uint16_t start_address{0};
  bool optimistic_{true}, use_write_multiple_{false};
  void dump_config(); void parse_and_publish(std::span<const uint8_t> data); void control(size_t index);
  const char* option_at(size_t index) const { return options.at(index).c_str(); }
  const std::string& get_name() const { static const std::string name{"host select"}; return name; }
  void publish_state(size_t index) { if (on_publish) on_publish(index); }
  void publish_state(const std::string&) {}
  void clear_dispatched_() {}
  bool dispatched() const { return false; }
  void clear_tx_queue_for_device() { ++clear_calls; queue.clear(); }
  void warn_write_buffer_deprecated_(const char*, uint16_t) {}
  unsigned entity_count() const { return 1; }
  uint16_t write_address() const { return start_address; }
  bool write_single_register(uint16_t address, uint16_t value) { queue.push_back({address,value}); return true; }
  bool write_multiple_registers(uint16_t address, const modbus::RegisterValues& values) { return !values.empty() && write_single_register(address,values[0]); }
};
}
}
'''

LOG_STUB = r'''#pragma once
#define ESP_LOGD(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGV(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGW(tag, ...) do { (void)(tag); } while (0)
#define ESP_LOGE(tag, ...) do { (void)(tag); } while (0)
#define LOG_SELECT(tag, ...) do { (void)(tag); } while (0)
#define LOG_STR(value) value
'''

MAIN = r'''
#include <cstdio>
#include "modbus_select.cpp"
using Select = esphome::modbus_controller::ModbusSelect;
using Gate = std::optional<int> (*)(int);
int failures = 0;
int ota_target = 0;
void require(bool condition, const char* label, int hp, int address, const char* cause, int attempt = -1) {
  if (!condition) { ++failures; std::fprintf(stderr,"%s HP%d register=%d fault=%s attempt=%d\n",label,hp+1,address,cause,attempt); }
}
void setup() {
  test_now_ms=100000;
  oq_enabled.state=true; oq_strategy_active_code=3; oq_control_mode_code=2;
  oq_manual_hp_active=oq_manual_hp_stop_requested=oq_commissioning_active=false;
  oq_cooling_stop_reason_code=0;
  oq_sources::hp1={}; oq_sources::hp2={};
  oq_boot_resume_runtime::ota_hold_reader=nullptr; oq_boot_resume_runtime::read_sources=nullptr; ota_target=0;
  oq_lowflow_fault_active=oq_water_temp_hard_trip_active=false;
  oq_incident_manager={}; hp1_odu_defrost={}; hp2_odu_defrost={};
  hp1_compressor_command_revalidation_required=hp2_compressor_command_revalidation_required=false;
  hp_generation.option="V2";
  hp1_is_online=hp2_is_online=true;
  hp1_odu_generation_detection_complete=hp2_odu_generation_detection_complete=true;
  hp1_generation_variant_code=hp2_generation_variant_code=static_cast<int>(oq_odu::Variant::V2_NEW_MODEL);
  hp1_set_working_mode.option=hp2_set_working_mode.option="Heating";
  hp1_compressor_level.selected=hp2_compressor_level.selected=8;
  oq_day_max_frequency_hz.state=90;
  oq_odu::RuntimeFrequencySnapshot snapshot;
  snapshot.variant=oq_odu::Variant::V2_NEW_MODEL; snapshot.heating.valid=true; snapshot.heating.level_count=21;
  snapshot.heating.hz={0,20,26,30,36,40,45,48,52,55,60,65,68,72,76,82,85,90,95,102,110};
  hp1_runtime_frequency_snapshot_storage=hp2_runtime_frequency_snapshot_storage=oq_odu::encode_runtime_frequency_snapshot(snapshot);
  for (auto* service : {&hp1_odu_defrost,&hp2_odu_defrost}) {
    service->cycle.observe_mode(2,test_now_ms); service->cycle.observe_bit(false,test_now_ms);
  }
}
Select select_for(int hp, int address, Gate gate) {
  Select select; select.start_address=static_cast<uint16_t>(address);
  const int maximum=address==3999 ? 2 : 20;
  for (int value=0;value<=maximum;++value) { select.mapping_.push_back(value); select.options.push_back(std::to_string(value)); }
  select.write_transform_func_=Select::WriteTransform([gate](Select*,std::string,int64_t value,esphome::modbus::RegisterValues&) -> std::optional<int64_t> { return gate(static_cast<int>(value)); });
  select.on_publish=[hp,address](size_t index) {
    if (address==1999) (hp==0?hp1_compressor_level:hp2_compressor_level).selected=index;
    else (hp==0?hp1_set_working_mode:hp2_set_working_mode).option=index==2 ? "Heating" : index==1 ? "Cooling" : "Standby";
  };
  return select;
}
bool dispatch_allowed(int hp, int address, int value) {
  const uint8_t pdu[]{6,static_cast<uint8_t>(address>>8),static_cast<uint8_t>(address),0,static_cast<uint8_t>(value)};
  auto& service=hp==0?hp1_odu_defrost:hp2_odu_defrost;
  const auto calls=service.queue_clear_calls;
  const bool allowed=oq_command_runtime::queued_command(hp==0,pdu,sizeof(pdu));
  assert(service.queue_clear_calls==calls); // Dispatch guards must stay read-only.
  return allowed;
}
int main() {
  Gate gates[2][2]={{gate0,gate2},{gate1,gate3}};
  const char* causes[]{"lowflow","water-trip","STOPPING","STOP_UNCONFIRMED","telemetry-fault"};
  for (int hp=0;hp<2;++hp) for (int which=0;which<2;++which) {
    const int address=which==0?3999:1999;
    for (int fault=0;fault<5;++fault) {
      setup(); auto& engine=oq_incident_manager.engines[hp]; auto& service=hp==0?hp1_odu_defrost:hp2_odu_defrost;
      if (fault==0) oq_lowflow_fault_active=true;
      if (fault==1) oq_water_temp_hard_trip_active=true;
      if (fault==2 || fault==3) {
        engine.observe_run({test_now_ms,true,true,false,true,90.0F}); assert(engine.request_stop(test_now_ms));
        if (fault==3) { test_now_ms+=60000; assert(!engine.request_stop(test_now_ms)); }
        assert(engine.outputs().run_state==(fault==2?oq_incidents::RunState::STOPPING:oq_incidents::RunState::STOP_UNCONFIRMED));
      }
      service.cycle.observe_mode(2,fault==4?1000:test_now_ms); service.cycle.observe_bit(false,fault==4?1000:test_now_ms);
      assert(!oq_incident_manager.boot_resume_pending(hp+1,test_now_ms));
      assert(!oq_boot_resume_runtime::ota_hold(hp==0));
      auto select=select_for(hp,address,gates[hp][which]);
      select.control(0);
      require(select.queue.size()==1 && select.queue[0].value==0,"INITIAL_ZERO_MISSING",hp,address,causes[fault]);
      for (int attempt=0;attempt<3;++attempt) {
        // Repeated positive requests must replace the pending stop with another
        // zero after the real SDK has already cleared the prior queue entry.
        service.cycle.request(test_now_ms); service.forced_write_pending_=true;
        const int positive=which==0 ? (attempt==0?1:2) : (attempt==0?1:attempt==1?17:20);
        const auto clears=select.clear_calls;
        select.control(static_cast<size_t>(positive));
        require(select.clear_calls==clears+1,"SDK_CLEAR_ORDER_NOT_EXERCISED",hp,address,causes[fault],attempt);
        require(select.queue.size()==1 && select.queue[0].address==address && select.queue[0].value==0,
                "SAFETY_ZERO_REMOVED_BY_POSITIVE_SELECT",hp,address,causes[fault],attempt);
        require(service.cycle.interrupted && !service.forced_write_pending_,"SAFETY_HOOK_NOT_RUN",hp,address,causes[fault],attempt);
        require((hp==0?hp1_compressor_command_revalidation_required:hp2_compressor_command_revalidation_required),"REVALIDATION_NOT_SET",hp,address,causes[fault],attempt);
        require(!dispatch_allowed(hp,address,positive),"POSITIVE_DISPATCH_ALLOWED",hp,address,causes[fault],attempt);
        if (!select.queue.empty()) require(dispatch_allowed(hp,address,select.queue[0].value),"ZERO_DISPATCH_REJECTED",hp,address,causes[fault],attempt);
      }
      // Clearing the fault/incident restores ordinary valid commands; replacement
      // must not install a sticky global zero or bypass the queued command check.
      oq_lowflow_fault_active=oq_water_temp_hard_trip_active=false; engine=oq_incidents::HpIncidentEngine{};
      service.cycle=oq_defrost::Cycle{}; service.cycle.observe_mode(2,test_now_ms); service.cycle.observe_bit(false,test_now_ms);
      hp1_set_working_mode.option=hp2_set_working_mode.option="Heating"; hp1_compressor_level.selected=hp2_compressor_level.selected=8;
      const int recovered=which==0?2:8; select.control(static_cast<size_t>(recovered));
      require(select.queue.size()==1 && select.queue[0].value==recovered,"RECOVERY_POSITIVE_NOT_QUEUED",hp,address,causes[fault]);
      require(dispatch_allowed(hp,address,recovered),"RECOVERY_DISPATCH_REJECTED",hp,address,causes[fault]);
    }
    // Ordinary defrost ownership is a rejection, not a safety-stop replacement.
    setup(); auto& service=hp==0?hp1_odu_defrost:hp2_odu_defrost;
    auto select=select_for(hp,address,gates[hp][which]); select.control(0);
    service.cycle.observe_bit(true,test_now_ms); service.cycle.phase=oq_defrost::Phase::ACTIVE;
    select.control(which==0?2:8);
    require(select.queue.empty(),"ORDINARY_DEFROST_BECAME_ZERO_WRITE",hp,address,"ordinary-defrost");
    // A short stale-telemetry gap preserves ownership; it is not yet a safety fault.
    setup(); auto& stale=hp==0?hp1_odu_defrost:hp2_odu_defrost;
    auto gap=select_for(hp,address,gates[hp][which]); gap.control(0); test_now_ms+=45000;
    assert(!stale.cycle.fresh(test_now_ms) && !stale.cycle.telemetry_fault(test_now_ms));
    gap.control(which==0?2:8); require(gap.queue.empty(),"ORDINARY_GAP_BECAME_ZERO_WRITE",hp,address,"short-gap");
    if (which==1) {
      setup(); auto cap=select_for(hp,address,gates[hp][which]); cap.control(0); cap.control(20);
      require(cap.queue.empty(),"FREQUENCY_REJECTION_BECAME_ZERO_WRITE",hp,address,"frequency-cap");
    }
    // The dew veto is an ordinary cooling rejection. It must not be promoted
    // to the latched hard-stop outcome used by the queue-replacement fix.
    setup(); auto dew=select_for(hp,address,gates[hp][which]); dew.control(0);
    (hp==0?hp1_set_working_mode:hp2_set_working_mode).option="Cooling";
    oq_cooling_stop_reason_code=oq_cooling::WATER_STOP_DEW;
    dew.control(which==0?1:8);
    require(dew.queue.empty(),"DEW_REJECTION_BECAME_ZERO_WRITE",hp,address,"dew-stop");
    // Ownership gates precede the hard-stop decision. Even a later fault must
    // not synthesize a zero while the existing boot/OTA gate still owns writes.
    setup(); auto bootSelect=select_for(hp,address,gates[hp][which]); bootSelect.control(0);
    auto& boot=oq_incident_manager.boot[hp]; boot.begin(true,test_now_ms);
    auto& bootService=hp==0?hp1_odu_defrost:hp2_odu_defrost;
    bootService.cycle.observe_mode(2,1000); bootService.cycle.observe_bit(false,1000);
    assert(bootService.cycle.telemetry_fault(test_now_ms));
    for (int request : {which==0?2:8,0}) {
      bootSelect.control(static_cast<size_t>(request));
      require(boot.pending(test_now_ms),"BOOT_OWNERSHIP_LOST",hp,address,"boot-pending");
      require(bootSelect.queue.empty(),"BOOT_REJECTION_BECAME_ZERO_WRITE",hp,address,"boot-pending");
      require(!dispatch_allowed(hp,address,0),"BOOT_ZERO_DISPATCH_ALLOWED",hp,address,"boot-pending");
    }
    setup(); auto otaSelect=select_for(hp,address,gates[hp][which]); otaSelect.control(0);
    ota_target=hp+1; oq_boot_resume_runtime::ota_hold_reader=[](uint8_t unit,uint32_t){return unit==ota_target;};
    oq_lowflow_fault_active=true;
    for (int request : {which==0?2:8,0}) {
      otaSelect.control(static_cast<size_t>(request));
      require(otaSelect.queue.empty(),"OTA_REJECTION_BECAME_ZERO_WRITE",hp,address,"ota-hold");
      require(!dispatch_allowed(hp,address,0),"OTA_ZERO_DISPATCH_ALLOWED",hp,address,"ota-hold");
    }
  }
  if (failures) std::fprintf(stderr,"TOTAL_QUEUE_REGRESSION_FAILURES=%d\n",failures);
  return failures?1:0;
}
'''


class LowflowSelectQueueTest(unittest.TestCase):
    def test_real_sdk_control_preserves_queued_safety_stop(self):
        sdk_source = sdk_select_source()
        yaml = (ROOT / "openquatt/oq_HP_io.yaml").read_text()
        bodies = []
        for suffix in ("set_working_mode", "compressor_level"):
            section = yaml[yaml.index("id: ${hp_id}_" + suffix + "\n"):]
            match = re.search(r"    write_lambda: \|-\n((?:      [^\n]*\n|\n)+)", section)
            self.assertIsNotNone(match, suffix)
            body = textwrap.dedent(match.group(1))
            for hp in (1, 2):
                bodies.append(body.replace("${hp_id}", "hp" + str(hp)).replace("${hp_index}", str(hp)))
        gates = "\n".join("std::optional<int> gate%d(int value) {\n%s\n}" % (index, body)
                          for index, body in enumerate(bodies))
        unit = '#include "modbus_select.h"\n' + fixture_prefix(real_incidents=True) + "\n" + gates + "\n" + MAIN
        args = compiler_command("-std=c++20", "-I", str(ROOT))
        with tempfile.TemporaryDirectory(prefix="openquatt-select-queue-") as directory:
            root = Path(directory)
            (root / "esphome/core").mkdir(parents=True)
            (root / "esphome/core/log.h").write_text(LOG_STUB)
            (root / "modbus_select.h").write_text(SDK_STUB)
            (root / "modbus_select.cpp").write_bytes(sdk_source)
            source, binary = root / "test.cpp", root / "test"
            source.write_text(unit)
            built = run_process(args + ["-I", str(root), str(source), "-o", str(binary)], timeout=COMPILE_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            result = run_process([str(binary)], timeout=RUN_TIMEOUT_SECONDS, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
