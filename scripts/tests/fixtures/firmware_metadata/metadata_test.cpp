#include "OpenQuattFirmwareMetadata.h"

#include <cassert>
#include <limits>
#include <thread>

using esphome::openquatt_firmware_metadata::OpenQuattFirmwareMetadata;
using esphome::openquatt_web_auth::OpenQuattWebAuth;
using esphome::web_server_base::WebServerBase;

class TestMetadata : public OpenQuattFirmwareMetadata {
 public:
  void set_revision(uint32_t revision) { this->manifest_revision_.store(revision); }
};

int main() {
  WebServerBase base;
  esphome::web_server_base::global_web_server_base = &base;
  OpenQuattWebAuth auth;
  TestMetadata metadata;
  metadata.setup();
  assert(base.handlers.empty());  // Missing auth must not register an open endpoint.
  metadata.set_web_auth(&auth);
  metadata.setup();
  assert(base.handlers.size() == 1U && base.handlers.front() == &metadata);
  assert(metadata.get_setup_priority() > esphome::setup_priority::WIFI + 1.0f);  // Before captive-portal listener.

  AsyncWebServerRequest request;
  request.url = "/openquatt/firmware/metadata";
  assert(metadata.canHandle(&request));
  request.url += "?test=1";
  assert(metadata.canHandle(&request));
  request.url = "/openquatt/firmware/metadata-extra";
  assert(!metadata.canHandle(&request));
  request.url = "/openquatt/firmware/metadat";
  assert(!metadata.canHandle(&request));
  request.url = "/openquatt/firmware/metadata%00-extra";
  assert(!metadata.canHandle(&request));
  request.url = "/openquatt/firmware/metadata%3Ftest=1";
  assert(!metadata.canHandle(&request));
  request.url = "/openquatt/firmware/%6detadata";
  assert(metadata.canHandle(&request));
  request.url = "/openquatt/firmware/metadata";
  request.verb = HTTP_POST;
  assert(!metadata.canHandle(&request));
  request.verb = HTTP_GET;
  metadata.handleRequest(&request);
  assert(request.challenged && request.response_body.empty());
  auth.permitted = true;
  metadata.handleRequest(&request);
  const std::string boot_prefix = request.response_body.substr(0, request.response_body.find(","));
  assert(request.response_code == 200);
  assert(request.response_headers["Cache-Control"] == "no-store");
  assert(request.response_body.find("\"manifest_revision\":0") != std::string::npos);

  constexpr unsigned COUNT = 20000;
  std::thread first([&]() {
    for (unsigned i = 0; i < COUNT; ++i) metadata.record_manifest_publication();
  });
  std::thread second([&]() {
    for (unsigned i = 0; i < COUNT; ++i) metadata.record_manifest_publication();
  });
  for (unsigned i = 0; i < 100; ++i) {
    metadata.handleRequest(&request);
    assert(request.response_body.substr(0, request.response_body.find(",")) == boot_prefix);
    assert(request.response_body.size() < 80U);
  }
  first.join();
  second.join();
  metadata.handleRequest(&request);
  assert(request.response_body.find("\"manifest_revision\":40000") != std::string::npos);
  metadata.set_revision(std::numeric_limits<uint32_t>::max());
  metadata.handleRequest(&request);
  assert(request.response_body.find("\"manifest_revision\":4294967295") != std::string::npos);
  metadata.record_manifest_publication();
  metadata.handleRequest(&request);
  assert(request.response_body.find("\"manifest_revision\":0") != std::string::npos);
}
