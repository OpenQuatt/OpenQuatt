#pragma once
#include "esp_http_server.h"
constexpr int SHUT_RDWR = 2;
inline int shutdown(int socket, int how) {
  if (socket != 7 || how != SHUT_RDWR || active_socket_request == nullptr) return -1;
  ++active_socket_request->closes;
  return 0;
}
