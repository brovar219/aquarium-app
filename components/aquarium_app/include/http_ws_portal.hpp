#pragma once

#include "esp_http_server.h"

namespace aq {

class DeviceService;

class HttpWsPortal {
 public:
  static esp_err_t start(httpd_handle_t* out_server, DeviceService* svc);
  static void stop(httpd_handle_t server);
  /** Поставити сокет останнього WS-клієнта (для async-пушу). */
  static void set_ws_client_fd(int fd);
  static int ws_client_fd();
  static httpd_handle_t server_handle();
  static void queue_state_broadcast();

 private:
  static DeviceService* s_svc_;
  static httpd_handle_t s_hd_;
  static int s_ws_fd_;
};

}  // namespace aq
