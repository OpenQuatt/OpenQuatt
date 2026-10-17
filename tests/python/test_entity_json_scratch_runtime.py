from pathlib import Path
import json
import tempfile
import unittest

from tests.support.cpp import COMPILE_TIMEOUT_SECONDS, RUN_TIMEOUT_SECONDS, compiler_command
from tests.support.process import run_process

ROOT = Path(__file__).resolve().parents[2]
CPP = ROOT / "components/openquatt_entities/OpenQuattEntities.cpp"


class EntityJsonScratchRuntimeTest(unittest.TestCase):
    def test_fit_overflow_and_allocation_failure_boundaries(self):
        cpp = CPP.read_text()
        writer = cpp[cpp.index("// Match JsonBuilder::serialize()") : cpp.index("struct RequestedEntity")]
        parser = cpp[cpp.index("bool parse_entity_line(") : cpp.index("class OpenQuattEntitiesRequestHandler")]
        method = cpp[cpp.index("void OpenQuattEntities::write_entities(") : cpp.rindex("}  // namespace openquatt_entities")]
        source = r"""
#include <algorithm>
#include <cassert>
#include <cstring>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
using ssize_t = long;
constexpr int ESP_OK = 0;
constexpr int HTTPD_RESP_USE_STRLEN = -1;
struct httpd_req_t { std::string body, status="200 OK"; int chunks=0; bool ended=false; };
int httpd_resp_send_chunk(httpd_req_t* r,const char* p,ssize_t n) {
  if (!p) r->ended=true; else {r->body.append(p,n); ++r->chunks;} return ESP_OK;
}
void httpd_resp_set_status(httpd_req_t* r,const char* s) {r->status=s;}
int httpd_resp_send(httpd_req_t* r,const char* s,int) {r->body=s; r->ended=true; return ESP_OK;}
bool fail_json=false, fail_writer=false;
int live_buffers=0;
template<class T> class PsramBuffer {
 std::unique_ptr<T[]> data_; size_t size_=0;
 public:
 ~PsramBuffer() {if(data_) --live_buffers;}
 bool allocate(size_t n) {assert(n==512); if(fail_writer)return false; return reserve(n);}
 bool allocate_external(size_t n) {assert(n==5120); if(fail_json)return false; return reserve(n);}
 bool reserve(size_t n) {data_.reset(new T[n]); size_=n; ++live_buffers; return true;}
 explicit operator bool()const{return bool(data_);}
 T* data(){return data_.get();} size_t size()const{return size_;}
};
namespace json {
class JsonBuilder {
 public:
 std::string payload;
 size_t serialize_to(char* p,size_t cap) {
   const size_t n=std::min(cap,payload.size()); std::memcpy(p,payload.data(),n);
   if(n<cap)p[n]=0; return n;
 }
};
}
struct RequestedEntity {std::string key,domain,name;};
struct Payload {std::string value;};
Payload small{R"({"value":1})"};
Payload fit{std::string("{\"value\":\"")+std::string(5107,'a')+"\"}"};
Payload equal{std::string("{\"value\":\"")+std::string(5108,'a')+"\"}"};
Payload large{std::string("{\"value\":\"")+std::string(5109,'a')+"\"}"};
void generate(void*,void* value,json::JsonBuilder& b) {b.payload=static_cast<Payload*>(value)->value;}
using EntityJsonGenerator = decltype(&generate);
struct EntityPayload {void* source=nullptr; decltype(&generate) state_generator=nullptr,all_generator=nullptr;};
EntityPayload find_entity_payload(const RequestedEntity& e,bool) {
 Payload* p=e.name=="small"?&small:e.name=="fit"?&fit:e.name=="equal"?&equal:e.name=="large"?&large:nullptr;
 return {p,p?generate:nullptr,p?generate:nullptr};
}
class OpenQuattEntities {
 public:
 void* web_server_=nullptr;
 void write_entities(httpd_req_t*,const std::string&,const std::string&)const;
};
""" + writer + parser + method + r"""
void run(const char* label,const std::string& entities) {
 httpd_req_t r; OpenQuattEntities c; c.write_entities(&r,"all",entities);
 assert(live_buffers==0);
 std::cout<<label<<'\t'<<r.status<<'\t'<<r.chunks<<'\t'<<r.ended<<'\t'<<r.body<<'\n';
}
int main() {
 assert(fit.value.size()==5119 && equal.value.size()==5120 && large.value.size()==5121);
 run("fit","fit\tsensor\tfit");
 run("equal","equal\tsensor\tequal");
 run("large","large\tsensor\tlarge");
 run("mixed","bad\"\\key\tsensor\tlarge\nsmall\tsensor\tsmall\nabsent\tsensor\tabsent\nlast\tsensor\tequal");
 fail_json=true; run("json_fail","small\tsensor\tsmall"); fail_json=false;
 fail_writer=true; run("writer_fail","small\tsensor\tsmall"); fail_writer=false;
 run("retry","small\tsensor\tsmall");
}
"""
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "entity_json_scratch"
            command = compiler_command("-x", "c++", "-", "-o", str(binary))
            compiled = run_process(command, input=source, text=True, capture_output=True,
                                   timeout=COMPILE_TIMEOUT_SECONDS)
            self.assertEqual(compiled.returncode, 0, compiled.stderr[-6000:])
            result = run_process([str(binary)], check=True, text=True, capture_output=True,
                                 timeout=RUN_TIMEOUT_SECONDS)
        rows = {}
        for line in result.stdout.splitlines():
            name, status, chunks, ended, body = line.split("\t", 4)
            rows[name] = (status, int(chunks), ended, json.loads(body))
        self.assertEqual(set(rows), {"fit", "equal", "large", "mixed", "json_fail", "writer_fail", "retry"})
        self.assertEqual(len(rows["fit"][3]["entities"]["fit"]["value"]), 5107)
        for name in ["equal", "large"]:
            self.assertEqual(rows[name][3]["entities"], {})
            self.assertEqual(rows[name][3]["errors"], [name])
        mixed = rows["mixed"][3]
        self.assertEqual(mixed["entities"], {"small": {"value": 1}})
        self.assertEqual(mixed["missing"], ["absent"])
        self.assertEqual(mixed["errors"], ['bad"\\key', "last"])
        for name in ["json_fail", "writer_fail"]:
            status, chunks, ended, body = rows[name]
            self.assertEqual(status, "503 Service Unavailable")
            self.assertEqual(chunks, 0)
            self.assertEqual(body, {"ok": False, "error": "serialization_unavailable"})
        self.assertEqual(rows["retry"][3]["entities"], {"small": {"value": 1}})
        self.assertTrue(all(row[2] == "1" for row in rows.values()))


if __name__ == "__main__":
    unittest.main()
