#include "http_ws_portal.hpp"

#include <cstring>
#include <string>

#include "device_service.hpp"
#include "firmware_http_update.hpp"
#include "esp_http_server.h"
#include "esp_log.h"
#include "json_state.hpp"

static const char* TAG = "http_ws";

extern const char index_html_start[] asm("_binary_index_html_start");
extern const char index_html_end[] asm("_binary_index_html_end");

namespace aq {

DeviceService* HttpWsPortal::s_svc_{nullptr};
httpd_handle_t HttpWsPortal::s_hd_{nullptr};
int HttpWsPortal::s_ws_fd_{-1};

struct AsyncWsText {
  httpd_handle_t hd;
  int fd;
  char* json;
};

static void ws_send_text_work(void* arg) {
  auto* w = static_cast<AsyncWsText*>(arg);
  if (w->fd < 0 || w->json == nullptr) {
    free(w->json);
    free(w);
    return;
  }
  httpd_ws_frame_t pkt{};
  pkt.type = HTTPD_WS_TYPE_TEXT;
  pkt.payload = reinterpret_cast<uint8_t*>(w->json);
  pkt.len = strlen(w->json);
  const esp_err_t e = httpd_ws_send_frame_async(w->hd, w->fd, &pkt);
  if (e != ESP_OK) {
    ESP_LOGW(TAG, "ws_send_frame_async: %s", esp_err_to_name(e));
  }
  free(w->json);
  free(w);
}

static esp_err_t root_get(httpd_req_t* req) {
  const size_t len = static_cast<size_t>(index_html_end - index_html_start);
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, index_html_start, len);
}

static esp_err_t ws_handler(httpd_req_t* req) {
  HttpWsPortal::set_ws_client_fd(httpd_req_to_sockfd(req));

  httpd_ws_frame_t ws_pkt{};
  memset(&ws_pkt, 0, sizeof(ws_pkt));
  ws_pkt.type = HTTPD_WS_TYPE_TEXT;
  esp_err_t ret = httpd_ws_recv_frame(req, &ws_pkt, 0);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "ws recv len %s", esp_err_to_name(ret));
    return ret;
  }

  if (ws_pkt.len > 4096) {
    return ESP_FAIL;
  }

  uint8_t* buf = static_cast<uint8_t*>(calloc(1, ws_pkt.len + 1));
  if (ws_pkt.len && buf == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  ws_pkt.payload = buf;
  if (ws_pkt.len) {
    ret = httpd_ws_recv_frame(req, &ws_pkt, ws_pkt.len);
    if (ret != ESP_OK) {
      free(buf);
      return ret;
    }
  }

  if (ws_pkt.type != HTTPD_WS_TYPE_TEXT) {
    free(buf);
    return ESP_OK;
  }

  DeviceService* svc = HttpWsPortal::s_svc_;
  if (svc == nullptr) {
    free(buf);
    return ESP_FAIL;
  }

  const std::string reply =
      ws_pkt.len ? svc->handle_ws_json(reinterpret_cast<char*>(buf)) : svc->handle_ws_json(R"({"type":"cmd","name":"get_state"})");

  free(buf);

  uint8_t* outbuf = static_cast<uint8_t*>(malloc(reply.size()));
  if (outbuf == nullptr) {
    return ESP_ERR_NO_MEM;
  }
  memcpy(outbuf, reply.data(), reply.size());

  httpd_ws_frame_t out{};
  out.type = HTTPD_WS_TYPE_TEXT;
  out.payload = outbuf;
  out.len = reply.size();
  ret = httpd_ws_send_frame(req, &out);
  free(outbuf);
  if (ret != ESP_OK) {
    ESP_LOGW(TAG, "ws send %s", esp_err_to_name(ret));
  }
  return ret;
}

esp_err_t HttpWsPortal::start(httpd_handle_t* out_server, DeviceService* svc) {
  s_svc_ = svc;
  s_ws_fd_ = -1;
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.stack_size = 8192;
  cfg.max_uri_handlers = 10;
  cfg.lru_purge_enable = true;
  // Довший таймаут для прийому тіла POST /update (OTA), байти можуть йти повільно.
  cfg.recv_wait_timeout = 120;
  cfg.send_wait_timeout = 30;
  if (httpd_start(out_server, &cfg) != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start");
    return ESP_FAIL;
  }
  s_hd_ = *out_server;

  httpd_uri_t u_root = {.uri = "/", .method = HTTP_GET, .handler = root_get, .user_ctx = nullptr};
  httpd_uri_t u_ws = {.uri = "/ws",
                      .method = HTTP_GET,
                      .handler = ws_handler,
                      .user_ctx = nullptr,
                      .is_websocket = true};

  ESP_ERROR_CHECK(httpd_register_uri_handler(*out_server, &u_root));
  ESP_ERROR_CHECK(httpd_register_uri_handler(*out_server, &u_ws));
  ESP_ERROR_CHECK(register_firmware_update_uri(*out_server));
  ESP_LOGI(TAG, "HTTP :80, WS /ws, POST /update (OTA)");
  return ESP_OK;
}

void HttpWsPortal::stop(httpd_handle_t server) {
  if (server) {
    httpd_stop(server);
  }
  s_hd_ = nullptr;
  s_ws_fd_ = -1;
}

void HttpWsPortal::set_ws_client_fd(int fd) { s_ws_fd_ = fd; }

int HttpWsPortal::ws_client_fd() { return s_ws_fd_; }

httpd_handle_t HttpWsPortal::server_handle() { return s_hd_; }

void HttpWsPortal::queue_state_broadcast() {
  if (s_hd_ == nullptr || s_ws_fd_ < 0 || s_svc_ == nullptr) {
    return;
  }
  char* js = state_to_json_malloc(s_svc_->snapshot());
  if (js == nullptr) {
    return;
  }
  auto* w = static_cast<AsyncWsText*>(malloc(sizeof(AsyncWsText)));
  if (w == nullptr) {
    free(js);
    return;
  }
  w->hd = s_hd_;
  w->fd = s_ws_fd_;
  w->json = js;
  if (httpd_queue_work(s_hd_, ws_send_text_work, w) != ESP_OK) {
    free(js);
    free(w);
  }
}

}  // namespace aq
