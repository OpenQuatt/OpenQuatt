from pathlib import Path
import json
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
MQTT_CPP = (ROOT / "components/openquatt_mqtt_config/OpenQuattMqttConfig.cpp").read_text()
MQTT_H = (ROOT / "components/openquatt_mqtt_config/OpenQuattMqttConfig.h").read_text()
ENERGY_CPP = (ROOT / "components/openquatt_energy_history/OpenQuattEnergyHistory.cpp").read_text()


def section(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


HTTP_STUBS = r"""
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
using StringRef = std::string_view;
constexpr int HTTP_GET=1, HTTP_POST=2, ESP_OK=0, HTTPD_RESP_USE_STRLEN=-1;
constexpr const char* HTTPD_200="200 OK";
struct httpd_req_t {
  void* handle=this; std::string body,status="200 OK",type;
  std::map<std::string,std::string> headers;
  int chunks=0; bool ended=false,closed=false,fail_send=false;
};
void httpd_resp_set_status(httpd_req_t* r,const char* s){r->status=s;}
void httpd_resp_set_type(httpd_req_t* r,const char* s){r->type=s;}
void httpd_resp_set_hdr(httpd_req_t* r,const char* k,const char* v){r->headers[k]=v;}
int httpd_resp_send(httpd_req_t* r,const char* p,int n){r->body.assign(p,n<0?std::strlen(p):n);r->ended=true;return ESP_OK;}
int httpd_resp_send_chunk(httpd_req_t* r,const char* p,long n){
 if(r->fail_send)return -1;
 if(p){r->body.append(p,n);++r->chunks;}else r->ended=true;return ESP_OK;
}
constexpr int SHUT_RDWR=2;
httpd_req_t* closing_request=nullptr;
int httpd_req_to_sockfd(httpd_req_t* r){closing_request=r;return 7;}
int shutdown(int fd,int how){assert(fd==7&&how==SHUT_RDWR);closing_request->closed=true;return ESP_OK;}
struct AsyncResponseStream {
 std::string body;
 void printf(const char* f,...){
  va_list a;va_start(a,f);int n=std::vsnprintf(nullptr,0,f,a);va_end(a);assert(n>=0);
  std::vector<char> b(n+1);va_start(a,f);std::vsnprintf(b.data(),b.size(),f,a);va_end(a);body.append(b.data(),n);
 }
};
struct AsyncWebServerRequest {
 static constexpr size_t URL_BUF_SIZE=513;
 std::string path;int verb=HTTP_GET,status=0;httpd_req_t raw;
 std::map<std::string,std::string> args,headers{{"Host","lab"}};
 explicit AsyncWebServerRequest(std::string p,int v=HTTP_GET):path(std::move(p)),verb(v){}
 StringRef url_to(char* p){assert(path.size()<URL_BUF_SIZE);std::strcpy(p,path.c_str());return p;}
 int method()const{return verb;}
 std::string arg(const char* k){auto i=args.find(k);return i==args.end()?std::string():i->second;}
 std::optional<std::string> get_header(const char* k){auto i=headers.find(k);if(i==headers.end())return {};return i->second;}
 AsyncResponseStream* beginResponseStream(const char*){return new AsyncResponseStream;}
 void send(AsyncResponseStream* p){status=200;raw.body=p->body;delete p;}
 void send(int code,const char* =nullptr,const char* body=""){status=code;raw.body=body;}
 operator httpd_req_t*(){return &raw;}
};
struct AsyncWebHandler {
 virtual ~AsyncWebHandler()=default;
 virtual bool canHandle(AsyncWebServerRequest*)const=0;
 virtual void handleRequest(AsyncWebServerRequest*)=0;
};
"""


class HttpHandlerStackRuntimeTest(unittest.TestCase):
    def compile_run(self, source):
        compiler = shutil.which("c++") or shutil.which("g++-15")
        if compiler is None:
            self.skipTest("C++ compiler unavailable")
        with tempfile.TemporaryDirectory(prefix="oq-http-stack-") as directory:
            binary = Path(directory) / "fixture"
            command = [compiler, "-std=c++17", "-x", "c++", "-", "-o", str(binary)]
            if sys.platform == "darwin":
                sdk = subprocess.check_output(["xcrun", "--sdk", "macosx", "--show-sdk-path"], text=True).strip()
                headers = Path(sdk) / "usr/include/c++/v1"
                if headers.exists():
                    command.extend(["-isystem", str(headers)])
            compiled = subprocess.run(command, input=source, capture_output=True, text=True)
            self.assertEqual(compiled.returncode, 0, compiled.stderr[-8000:])
            return subprocess.run([str(binary)], check=True, capture_output=True, text=True, timeout=15).stdout

    def test_mqtt_routes_csrf_mutation_errors_and_owned_status(self):
        enums = section(MQTT_H, "  enum class NumericInputKind", "  void set_bootstrap_broker")
        snapshot = section(MQTT_H, "  struct StatusSnapshot", "  StatusSnapshot get_status_snapshot")
        validator = section(MQTT_CPP, "bool OpenQuattMqttConfig::validate_csrf_token", "OpenQuattMqttConfig::StatusSnapshot")
        escaping = section(MQTT_CPP, "std::string json_escape_", "std::string base64_encode_bytes_")
        origin = section(MQTT_CPP, "bool header_matches_host_", "const char* skip_spaces_")
        handlers = section(MQTT_CPP, "bool send_mutation_error_", "}  // namespace\n")
        source = HTTP_STUBS + "class OpenQuattMqttConfig { public:\nstatic constexpr size_t NUMERIC_INPUT_COUNT=6, BINARY_INPUT_COUNT=2;\n" + enums + snapshot + r"""
 void* config_lock_=this;std::string csrf_token_="valid";
 int depth=0,locks=0,snapshots=0,mutations=0;
 StatusSnapshot state{};MutationResult next=MutationResult::OK;
 std::string saved_broker,saved_username,saved_password,last_key;uint16_t saved_port=0;bool last_value=false,cleared=false;
 bool validate_csrf_token(const std::string&);
 void lock_config_(){assert(depth==0);++depth;++locks;}
 void unlock_config_(){assert(depth==1);--depth;}
 StatusSnapshot get_status_snapshot(){++snapshots;return state;}
 MutationResult set_runtime_config(const std::string& b,uint16_t p,const std::string& u,const std::string& pw,bool clear,bool enabled){
  ++mutations;saved_broker=b;saved_port=p;saved_username=u;saved_password=pw;cleared=clear;state.enabled=enabled;return next;
 }
 MutationResult set_input_enabled(const std::string& k,bool v){++mutations;last_key=k;last_value=v;return next;}
 MutationResult set_input_accept_retained(const std::string& k,bool v){++mutations;last_key=k;last_value=v;return next;}
};
""" + validator + escaping + origin + handlers + r"""
int main(){
 OpenQuattMqttConfig p;MqttConfigHandler h(&p);
 assert(!p.validate_csrf_token(""));assert(p.locks==0);
 p.config_lock_=nullptr;assert(!p.validate_csrf_token("valid"));assert(p.locks==0);p.config_lock_=&p;
 assert(!p.validate_csrf_token("wrong"));assert(p.validate_csrf_token("valid"));assert(p.depth==0&&p.locks==2);
 for(const char* route:{"/mqtt/save","/mqtt/input/save","/mqtt/input/retained/save"}){
  for(const char* token:{"","wrong"}){
   AsyncWebServerRequest r(route,HTTP_POST);r.args["csrf_token"]=token;
   assert(h.canHandle(&r));h.handleRequest(&r);assert(r.status==403&&p.mutations==0&&p.snapshots==0);
  }
  AsyncWebServerRequest bad_origin(route,HTTP_POST);bad_origin.args["csrf_token"]="valid";
  bad_origin.headers["Origin"]="http://foreign";h.handleRequest(&bad_origin);assert(bad_origin.status==403&&p.mutations==0);
  AsyncWebServerRequest no_host(route,HTTP_POST);no_host.args["csrf_token"]="valid";no_host.headers.clear();
  h.handleRequest(&no_host);assert(no_host.status==403&&p.mutations==0);
  AsyncWebServerRequest bad_referer(route,HTTP_POST);bad_referer.args["csrf_token"]="valid";
  bad_referer.headers["Origin"]="http://lab";bad_referer.headers["Referer"]="http://foreign/a";
  h.handleRequest(&bad_referer);assert(bad_referer.status==403&&p.mutations==0);
  AsyncWebServerRequest wrong_method(route,HTTP_GET);assert(!h.canHandle(&wrong_method));
 }
 for(const char* port:{"0","65536","abc","1883x"}){
  AsyncWebServerRequest r("/mqtt/save",HTTP_POST);r.args={{"csrf_token","valid"},{"port",port}};
  h.handleRequest(&r);assert(r.status==409&&p.mutations==0);
 }
 AsyncWebServerRequest save("/mqtt/save",HTTP_POST);
 save.args={{"csrf_token","valid"},{"enabled","false"},{"port","1883"},{"username","discard"},{"password","discard"}};
 save.headers["Origin"]="http://lab";save.headers["Referer"]="http://lab/settings";
 h.handleRequest(&save);assert(save.status==200&&p.mutations==1&&p.saved_port==1883&&p.cleared);
 assert(p.saved_broker.empty()&&p.saved_username.empty()&&p.saved_password.empty());
 for(const char* route:{"/mqtt/input/save","/mqtt/input/retained/save"}){
  AsyncWebServerRequest r(route,HTTP_POST);r.args={{"csrf_token","valid"},{"input","room_setpoint"},{"enabled","true"},{"accept_retained","true"}};
  h.handleRequest(&r);assert(r.status==200&&p.last_key=="room_setpoint"&&p.last_value);
 }
 int snapshots=p.snapshots;
 for(auto result:{OpenQuattMqttConfig::MutationResult::BUSY,OpenQuattMqttConfig::MutationResult::PERSISTENCE_PENDING,OpenQuattMqttConfig::MutationResult::SAVE_FAILED}){
  p.next=result;AsyncWebServerRequest r("/mqtt/save",HTTP_POST);r.args={{"csrf_token","valid"}};
  h.handleRequest(&r);assert(r.status>=500&&p.snapshots==snapshots&&r.raw.body.find("\"ok\":false")!=std::string::npos);
 }
 p.state.broker="a\"b\\c\n";p.state.username="user";p.state.csrf_token="valid";p.state.config_source="stored";
 for(size_t i=0;i<6;++i){p.state.input_topics[i]="input/"+std::to_string(i);p.state.input_enabled[i]=true;}
 p.state.dew_point_topic=p.state.input_topics[0];
 for(size_t i=0;i<2;++i)p.state.binary_input_topics[i]="binary/"+std::to_string(i);
 AsyncWebServerRequest status("/mqtt/status");assert(h.canHandle(&status));h.handleRequest(&status);assert(status.status==200);
 std::cout<<status.raw.body<<'\n';
 AsyncWebServerRequest unknown("/mqtt/unknown");assert(!h.canHandle(&unknown));h.handleRequest(&unknown);assert(unknown.status==404);
 assert(p.depth==0);
}
"""
        payload = json.loads(self.compile_run(source))
        self.assertEqual(payload["broker"], 'a"b\\c\n')
        self.assertEqual(payload["source"], "stored")
        self.assertEqual(payload["csrf_token"], "valid")
        self.assertEqual(len(payload["input_topics"]), 8)
        self.assertTrue(payload["input_enabled"]["room_setpoint"])
        self.assertEqual(payload["non_retained_stateful_timeout_s"], 1800)

    def test_energy_private_scratch_bounds_failure_retry_and_routes(self):
        writer = section(ENERGY_CPP, "class ChunkedTextWriter {", "class OpenQuattEnergyHistoryRequestHandler")
        route = section(ENERGY_CPP, "class OpenQuattEnergyHistoryRequestHandler", "void OpenQuattEnergyHistory::setup()")
        path_match = section(ENERGY_CPP, "static bool url_path_matches", "static bool read_query_value")
        origin = section(MQTT_CPP, "bool header_matches_host_", "const char* skip_spaces_")
        source = HTTP_STUBS + r"""
bool fail_alloc=false;int live_buffers=0;
namespace openquatt_common {
 template<class T>class PsramBuffer{
  std::unique_ptr<T[]> data_;size_t size_=0;
  public:
  ~PsramBuffer(){if(data_)--live_buffers;}
  bool allocate_external(size_t n){assert(n==192);if(fail_alloc)return false;data_.reset(new T[n]);size_=n;++live_buffers;return true;}
  explicit operator bool()const{return bool(data_);}
  T* data(){return data_.get();}size_t size()const{return size_;}
 };
}
class OpenQuattEnergyHistory {
 public:
 bool ready=true;int histories=0,exports=0,imports=0;
 bool runtime_storage_ready(){return ready;}
 std::string get_csrf_token(){return "valid";}
 void write_history(httpd_req_t*){++histories;}
 void write_history_export(httpd_req_t*){++exports;}
 std::string import_history_records(const std::string&){++imports;return "{}";}
};
""" + origin + path_match + writer + route + r"""
int main(){
 for(bool download:{false,true}){
  httpd_req_t r;{ChunkedTextWriter w(&r);fail_alloc=true;assert(!w.begin_response("text/plain",download));fail_alloc=false;}
  assert(live_buffers==0&&r.status=="503 Service Unavailable"&&r.type=="application/json"&&r.chunks==0&&r.ended);
  assert(!r.headers.count("Content-Disposition"));assert(r.body.find("serialization_unavailable")!=std::string::npos);
 }
 {
  httpd_req_t a,b;ChunkedTextWriter x(&a),y(&b);
  assert(x.begin_response("text/plain")&&y.begin_response("application/json",true));assert(live_buffers==2);
  assert(x.printf("%s",std::string(191,'a').c_str()));assert(a.body.size()==191);
  assert(y.printf("%s","other"));assert(b.body=="other"&&b.headers.count("Content-Disposition"));
  assert(!x.printf("%s",std::string(192,'b').c_str()));assert(a.body.size()==191&&a.closed&&!a.ended);
  assert(!x.printf("%s","after overflow")&&a.body.size()==191&&!a.ended);
  b.fail_send=true;assert(!y.printf("%s","unsent"));assert(b.body=="other"&&b.closed&&!b.ended);
  b.fail_send=false;assert(!y.printf("%s","after failure")&&b.body=="other"&&!b.ended);
 }
 assert(live_buffers==0);
 OpenQuattEnergyHistory p;OpenQuattEnergyHistoryRequestHandler h(&p);
 for(const char* path:{"/energy/history","/energy/history?meta=1","/energy/history/export?mode=hours"}){
  AsyncWebServerRequest r(path);assert(h.canHandle(&r));h.handleRequest(&r);
 }
 assert(p.histories==2&&p.exports==1);
 AsyncWebServerRequest import("/energy/history/import",HTTP_POST);assert(h.canHandle(&import));
 h.handleRequest(&import);assert(import.status==409&&p.imports==0);
 import.args["csrf_token"]="valid";import.headers["Origin"]="http://foreign";
 h.handleRequest(&import);assert(import.status==409&&p.imports==0);
 import.headers["Origin"]="http://lab";h.handleRequest(&import);assert(import.status==200&&p.imports==1);
 AsyncWebServerRequest wrong("/energy/history/export",HTTP_POST);assert(!h.canHandle(&wrong));
 AsyncWebServerRequest suffix("/energy/history/other");assert(!h.canHandle(&suffix));
 p.ready=false;AsyncWebServerRequest unavailable("/energy/history");h.handleRequest(&unavailable);
 assert(unavailable.status==503&&p.histories==2);
}
"""
        self.compile_run(source)


if __name__ == "__main__":
    unittest.main()
