"""Exercise production ODU firmware snapshots, serialization and wiring."""

from pathlib import Path
import json
import re
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]
PERFORMANCE = ROOT / "components/openquatt_performance_telemetry"
SOURCE = (PERFORMANCE / "OpenQuattPerformanceTelemetry.cpp").read_text()
HEADER = (PERFORMANCE / "OpenQuattPerformanceTelemetry.h").read_text()


class OduFirmwareTelemetryTest(unittest.TestCase):
    def test_package_and_codegen_wiring(self) -> None:
        for kind in ("usage", "performance"):
            package = (ROOT / f"openquatt/oq_{kind}_telemetry.yaml").read_text()
            self.assertIn("hp1_odu_firmware: hp1_pcb_firmware_raw", package)
            self.assertIn("hp2_odu_firmware: ${secondary_hp_id}_pcb_firmware_raw", package)
            component = ROOT / f"components/openquatt_{kind}_telemetry"
            codegen = (component / "__init__.py").read_text()
            header = next(component.glob("*Telemetry.h")).read_text()
            self.assertIn("cv.use_id(sensor.Sensor)", codegen)
            self.assertIn("set_hp1_odu_firmware", header)
            self.assertIn("set_hp2_odu_firmware", header)
        usage = (ROOT / "components/openquatt_usage_telemetry/OpenQuattUsageTelemetry.cpp").read_text()
        self.assertIn('append_json_key_(payload, "odu_fw")', usage)
        self.assertIn("openquatt_common::append_odu_firmware(payload, odu_firmware, hp_count)", usage)
        self.assertIn('this->topology_ == "duo" ? 2U : 1U', usage)
        io = (ROOT / "openquatt/oq_HP_io.yaml").read_text()
        self.assertIn("component.update: ${hp_id}_pcb_firmware_version", io)
        self.assertIn("esphome::openquatt_common::format_odu_firmware(firmware_raw, buf)", io)

    def test_production_sampling_batches_and_json(self) -> None:
        methods = []
        for name in (
            "sample_minute_", "finish_minute_", "append_record_", "close_window_",
            "build_pending_payload_", "valid_sensor_", "valid_binary_",
            "generation_code_", "generation_name_", "map_id_",
        ):
            match = re.search(
                r"(?:void|bool|int|const char\*) OpenQuattPerformanceTelemetry::"
                + name + r"\([^\n]*\) \{.*?\n\}", SOURCE, re.S,
            )
            self.assertIsNotNone(match, name)
            methods.append(match.group())
        structs = []
        for name in ("HpSources", "HpRuntime", "HpAccumulator", "MinuteAccumulator", "MinuteRecord"):
            match = re.search(r"  struct " + name + r" \{.*?\n  \};", HEADER, re.S)
            self.assertIsNotNone(match, name)
            structs.append(match.group())
        helpers = SOURCE[SOURCE.index("void append_float("):SOURCE.index("\n}  // namespace")]
        usage = (ROOT / "components/openquatt_usage_telemetry/OpenQuattUsageTelemetry.cpp").read_text()
        usage_snapshot = usage[usage.index("  std::array<uint16_t, 2U> odu_firmware{};"):usage.index("  append_json_flow_source_(payload,")]
        usage_key = re.search(r"void append_json_key_\([^\n]*\) \{.*?\n\}", usage, re.S).group()
        fixture = r'''
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include "components/openquatt_common/OpenQuattOduFirmware.h"
#include "components/openquatt_performance_telemetry/OpenQuattPerformanceTelemetryPolicy.h"
using namespace esphome::openquatt_performance_telemetry;
namespace openquatt_common = esphome::openquatt_common;
namespace sensor { struct Sensor { float state=0; bool known=true; bool has_state() const { return known; } }; }
namespace binary_sensor { struct BinarySensor { bool state=false; bool has_state() const { return true; } }; }
namespace select { struct Select { bool has_state() const { return true; } std::string current_option() const { return "V2"; } }; }
#define ESP_LOGW(...) ((void)0)
struct Buffer {
 std::vector<char> bytes;
 void release() { bytes.clear(); }
 bool allocate_external(size_t n) { bytes.resize(n); return true; }
 char* data() { return bytes.data(); }
 size_t size() const { return bytes.size(); }
};
struct Transport { const char* external_installation_id() { return "00000000-0000-4000-8000-000000000001"; } };
class OpenQuattPerformanceTelemetry {
 public:
// STRUCTS
 struct Records : std::array<MinuteRecord,30> { explicit operator bool() const { return true; } } records_;
 static constexpr size_t RECORDS_PER_BATCH=15, PAYLOAD_CAPACITY=4096;
 static constexpr uint32_t SAMPLE_SECONDS=10, MINUTE_SECONDS=60;
 static constexpr uint32_t START_SETTLE_SECONDS=300, LEVEL_SETTLE_SECONDS=60, DEFROST_SETTLE_SECONDS=300;
 std::array<HpSources,2> hp_{};
 std::array<HpRuntime,2> hp_runtime_{};
 MinuteAccumulator minute_;
 select::Select generation; select::Select* generation_select_=&generation;
 sensor::Sensor outside,flow;
 sensor::Sensor* outside_temp_sensor_=&outside; sensor::Sensor* flow_sensor_=&flow;
 Buffer payload_;
 Transport transport; Transport* transport_=&transport;
 size_t active_record_count_=0,pending_record_count_=0;
 uint32_t active_window_s_=0,pending_window_s_=0,next_retry_ms_=0;
 unsigned consecutive_failures_=0;
 bool pending_publish_allowed_=false;
 std::string pending_batch_id_,firmware_version_="test-openquatt",topology_="duo";
 static std::string random_uuid_() { return "00000000-0000-4000-8000-000000000002"; }
 bool sample_minute_(uint32_t); void finish_minute_(); bool append_record_(const MinuteRecord&);
 void close_window_(bool); bool build_pending_payload_();
 static bool valid_sensor_(const sensor::Sensor*); static bool valid_binary_(const binary_sensor::BinarySensor*);
 static int generation_code_(const select::Select*); static const char* generation_name_(int); static const char* map_id_(int);
};
// PRODUCTION
// USAGE_KEY
struct Usage {
 std::array<sensor::Sensor*,2> odu_firmware_{};
 std::string topology_="duo";
 std::string snapshot() {
  char buffer[128];FixedBufferWriter payload(buffer,sizeof(buffer));payload+="{\"test\":true";
// USAGE_SNAPSHOT
  payload+='}';assert(payload.ok());return buffer;
 }
};
int main() {
 using esphome::openquatt_common::odu_firmware_raw;
 using esphome::openquatt_common::format_odu_firmware;
 for (float value : {NAN, INFINITY, -1.f, 0.f, 65535.f, 65536.f, 514.5f}) assert(odu_firmware_raw(value)==0);
 for (auto [raw, expected] : std::array<std::pair<uint16_t,const char*>,8>{{
   {0x0119,"1.25"},{0x011e,"1.30"},{0x0122,"1.34"},{0x0201,"2.01"},
   {0x0202,"2.02"},{0x0307,"3.07"},{0xfffe,"255.254"},{0,""}}}) {
  char version[8]; format_odu_firmware(raw,version); assert(std::string(version)==expected);
 }
 OpenQuattPerformanceTelemetry c;
 sensor::Sensor fw1{514},fw2{513},mode{2},hz{40},level{5},power{900},heat{3600},pump{30},ti{30},to{33};
 binary_sensor::BinarySensor off;
 c.outside.state=7;c.flow.state=800;
 for (size_t i=0;i<2;++i) {
  c.hp_[i]={i==0?&fw1:&fw2,&mode,&hz,&level,&power,&heat,&pump,&ti,&to,&off,&off,&off,&off};
  c.hp_runtime_[i].active_known=true;c.hp_runtime_[i].active=true;c.hp_runtime_[i].level=5;
 }
 c.minute_.start_s=120000;
 for (uint32_t t=120000;t<120060;t+=10) assert(c.sample_minute_(t));
 c.finish_minute_();assert(c.active_record_count_==1);
 assert(c.records_[0].odu_firmware==std::array<uint16_t,2>({514,513}));
 c.close_window_(true);
 fw1.state=515;fw2.state=514;
 assert(c.build_pending_payload_());
 std::string original=c.payload_.data();
 assert(original.find("\"odu_fw\":[\"2.02\",\"2.01\"]")!=std::string::npos);
 assert(original.find("\"gen\":\"v2\"")!=std::string::npos);
 assert(original.find("\"map\":\"v2-2026-09-a\"")!=std::string::npos);
 assert(c.build_pending_payload_() && original==c.payload_.data());
 std::cout<<original<<'\n';
 c.minute_={};c.minute_.start_s=120060;
 assert(c.sample_minute_(120060));fw1.state=516;
 assert(!c.sample_minute_(120070) && c.minute_.invalid);
 c.finish_minute_();assert(c.active_record_count_==0);
 c.pending_record_count_=0;
 auto first=c.records_[15];first.start_s=120120;
 auto next=first;next.start_s+=60;next.odu_firmware[0]=516;
 c.append_record_(first);c.append_record_(next);
 assert(c.pending_record_count_==1 && c.active_record_count_==1 && !c.pending_publish_allowed_);
 assert(c.records_[15].odu_firmware[0]==514 && c.records_[0].odu_firmware[0]==516);
 c.topology_="single";c.records_[15].odu_firmware={0,513};
 assert(c.build_pending_payload_());std::cout<<c.payload_.data()<<'\n';
 c.topology_="duo";c.records_[15].odu_firmware={514,0};
 assert(c.build_pending_payload_());std::cout<<c.payload_.data()<<'\n';
 c.minute_={};c.minute_.start_s=120180;fw1.known=false;fw2.state=65535;
 for (uint32_t t=120180;t<120230;t+=10) assert(c.sample_minute_(t));
 assert(c.minute_.odu_firmware==std::array<uint16_t,2>({0,0}));
 fw1.known=true;
 assert(!c.sample_minute_(120230) && c.minute_.invalid);
 c.active_record_count_=0;c.pending_record_count_=0;
 first.odu_firmware={0,0};next.odu_firmware={514,0};
 c.append_record_(first);c.append_record_(next);
 assert(c.pending_record_count_==1 && c.records_[15].odu_firmware[0]==0);
 c.pending_record_count_=15;
 for(size_t i=0;i<15;++i) {
  c.records_[15+i]=first;c.records_[15+i].odu_firmware={0xfffe,0xfffe};
  c.records_[15+i].active_mask=3;
 }
 assert(c.build_pending_payload_());
 std::cout<<"{\"batch_bytes\":"<<std::strlen(c.payload_.data())<<",\"minute_record_bytes\":"<<sizeof(first)<<"}\n";
 Usage u;u.odu_firmware_={&fw1,&fw2};fw1.state=514;fw2.state=513;
 std::cout<<u.snapshot()<<'\n';
 u.topology_="single";std::cout<<u.snapshot()<<'\n';
 fw1.known=false;std::cout<<u.snapshot()<<'\n';
 u.topology_="duo";u.odu_firmware_[0]=nullptr;fw2.state=65535;
 std::cout<<u.snapshot()<<'\n';
}
'''
        fixture = fixture.replace("// STRUCTS", "\n".join(structs))
        fixture = fixture.replace("// PRODUCTION", helpers + "\n" + "\n".join(methods))
        fixture = fixture.replace("// USAGE_KEY", usage_key).replace("// USAGE_SNAPSHOT", usage_snapshot)
        # assert() is a macro: protect the comma in std::array's template arguments.
        fixture = re.sub(r"assert\(([^\n]*std::array<uint16_t,2>[^\n]*)\);", r"assert((\1));", fixture)
        args = compiler_command("-I", str(ROOT))
        with tempfile.TemporaryDirectory(prefix="openquatt-odu-firmware-") as directory:
            unit = Path(directory) / "test.cpp"
            binary = Path(directory) / "test"
            unit.write_text(fixture)
            compiled = run_process(
                args + [str(unit), "-o", str(binary)],
                capture_output=True, text=True, timeout=COMPILE_TIMEOUT_SECONDS,
            )
            self.assertEqual(compiled.returncode, 0, compiled.stderr)
            executed = run_process(
                [str(binary)], capture_output=True, text=True, timeout=RUN_TIMEOUT_SECONDS,
            )
            self.assertEqual(executed.returncode, 0, executed.stderr)
        payloads = [json.loads(line) for line in executed.stdout.splitlines()]
        self.assertEqual(payloads[0]["odu_fw"], ["2.02", "2.01"])
        self.assertEqual(payloads[1]["odu_fw"], [None])
        self.assertEqual(payloads[2]["odu_fw"], ["2.02", None])
        self.assertLessEqual(payloads[3]["batch_bytes"], 4096)
        self.assertLessEqual(payloads[3]["minute_record_bytes"], 120)
        self.assertEqual(payloads[4]["odu_fw"], ["2.02", "2.01"])
        self.assertEqual(payloads[5]["odu_fw"], ["2.02"])
        self.assertEqual(payloads[6]["odu_fw"], [None])
        self.assertEqual(payloads[7]["odu_fw"], [None, None])


if __name__ == "__main__":
    unittest.main()
