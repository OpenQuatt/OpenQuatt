#include <atomic>
#include <cassert>
#include <thread>

#include "web_server_base.h"

using esphome::web_server_base::WebServerBase;

class Handler : public AsyncWebHandler {
 public:
  int calls{0};
  WebServerBase* update_from_callback{nullptr};
  void handleRequest(AsyncWebServerRequest*) override {
    ++calls;
    if (update_from_callback != nullptr) update_from_callback->set_auth_credentials("next", "next-password");
  }
  void handleUpload(AsyncWebServerRequest*, const std::string&, size_t, uint8_t*, size_t, bool) override { ++calls; }
  void handleBody(AsyncWebServerRequest*, uint8_t*, size_t, size_t, size_t) override { ++calls; }
};

static void invoke_all(AsyncWebHandler* handler, AsyncWebServerRequest* request) {
  handler->handleRequest(request);
  handler->handleUpload(request, "upload", 0, nullptr, 0, true);
  handler->handleBody(request, nullptr, 0, 0, 0);
}

int main() {
  WebServerBase base;
  // A queued web action stays invalid after recovery closes; a new action may run.
  const auto before_recovery = base.recovery_epoch();
  base.set_recovery_active(true);
  assert(base.is_recovery_active());
  assert(before_recovery != base.recovery_epoch());
  base.set_recovery_active(false);
  assert(!base.is_recovery_active());
  assert(before_recovery != base.recovery_epoch());
  const auto after_recovery = base.recovery_epoch();
  assert(after_recovery == base.recovery_epoch());
  Handler early;
  Handler late;
  Handler portal;
  // OpenQuatt configures bootstrap auth before setup, then applies open mode.
  base.set_auth_credentials("bootstrap", "bootstrap");
  base.set_auth_credentials("", "");
  base.add_handler(&early);
  base.init();
  base.add_handler_without_auth(&portal);
  auto& handlers = base.get_server()->handlers;
  AsyncWebServerRequest anonymous;
  invoke_all(handlers[0], &anonymous);
  assert(early.calls == 3);
  assert(base.request_is_authenticated(&anonymous));
  assert(!base.request_is_authenticated(&anonymous, true));

  char username[] = "admin";
  char password[] = "secret";
  base.set_auth_credentials(username, password);
  // The caller's storage may change or leave scope immediately.
  username[0] = 'X';
  password[0] = 'X';
  base.add_handler(&late);
  invoke_all(handlers[0], &anonymous);
  invoke_all(handlers[2], &anonymous);
  assert(early.calls == 3 && late.calls == 0 && anonymous.challenged);
  invoke_all(handlers[1], &anonymous);
  assert(portal.calls == 3);

  AsyncWebServerRequest admin;
  admin.username = "admin";
  admin.password = "secret";
  assert(base.request_is_authenticated(&admin, true));
  assert(!base.request_is_authenticated(nullptr, true));
  invoke_all(handlers[0], &admin);
  invoke_all(handlers[2], &admin);
  assert(early.calls == 6 && late.calls == 3);

  base.set_auth_credentials("admin", "replacement");
  invoke_all(handlers[0], &admin);
  assert(early.calls == 6 && admin.challenged);
  // Partial configured credentials must never inherit ESPHome's null-password bypass.
  base.set_auth_credentials("admin", nullptr);
  assert(!base.request_is_authenticated(&admin));
  base.set_auth_credentials(nullptr, "secret");
  assert(!base.request_is_authenticated(&admin));
  assert(!base.request_is_authenticated(&admin, true));
  base.set_auth_credentials("", "");
  invoke_all(handlers[0], &anonymous);
  assert(early.calls == 9);
  assert(!base.request_is_authenticated(&anonymous, true));

  // Registration after explicit empty credentials also follows later transitions.
  Handler open_registered;
  base.add_handler(&open_registered);
  base.set_auth_credentials("admin", "secret");
  invoke_all(handlers[3], &anonymous);
  assert(open_registered.calls == 0);
  early.update_from_callback = &base;
  handlers[0]->handleRequest(&admin);  // must not hold a non-recursive lock across the callback
  assert(!base.request_is_authenticated(&admin, true));

  // Concurrent auth must see a complete owned pair, never half a replacement.
  base.set_auth_credentials("first", "first");
  std::atomic<bool> done{false};
  std::thread writer([&]() {
    for (int i = 0; i < 20000; ++i) base.set_auth_credentials(i % 2 ? "first" : "second", i % 2 ? "first" : "second");
    done.store(true);
  });
  AsyncWebServerRequest reader;
  reader.paired_only = true;
  do {
    assert(base.request_is_authenticated(&reader, true));
  } while (!done.load());
  writer.join();
}
