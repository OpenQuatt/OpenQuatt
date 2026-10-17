#pragma once
#define CONFIG_LWIP_MAX_SOCKETS 21
#define SHUT_RDWR 2
inline unsigned test_closed_sockets = 0;
inline int shutdown(int, int) {
  ++test_closed_sockets;
  return 0;
}
