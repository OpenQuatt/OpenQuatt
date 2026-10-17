#pragma once
#include <cstddef>
#include <functional>
#include <cstdlib>
inline void httpd_resp_set_status(AsyncWebServerRequest& request, const char* status) {
  request.pending_status = std::atoi(status);
}
inline constexpr int ESP_OK = 0;
inline bool test_queue_ok = true;
inline bool test_close_ok = true;
inline std::function<void()> test_httpd_work;
inline int httpd_queue_work(void*, void (*callback)(void*), void* argument) {
  if (!test_queue_ok) return -1;
  test_httpd_work = [=]() { callback(argument); };
  return ESP_OK;
}
inline int httpd_get_client_list(void*, size_t* count, int* sockets) {
  if (!test_close_ok) return -1;
  *count = 1;
  sockets[0] = 42;
  return ESP_OK;
}
