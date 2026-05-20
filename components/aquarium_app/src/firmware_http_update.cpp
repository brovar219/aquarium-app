#include "firmware_http_update.hpp"

#include <algorithm>
#include <array>
#include <cstring>

#include "esp_app_desc.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "ota_http";

namespace aq {

void ota_mark_app_valid_if_needed() {
  const esp_partition_t* running = esp_ota_get_running_partition();
  if (running == nullptr) {
    return;
  }
  esp_ota_img_states_t state{};
  if (esp_ota_get_state_partition(running, &state) != ESP_OK) {
    return;
  }
  if (state == ESP_OTA_IMG_PENDING_VERIFY) {
    const esp_err_t e = esp_ota_mark_app_valid_cancel_rollback();
    if (e != ESP_OK) {
      ESP_LOGW(TAG, "mark_app_valid: %s", esp_err_to_name(e));
    } else {
      ESP_LOGI(TAG, "Нова прошивка підтверджена (rollback скасовано)");
    }
  }
}

static esp_err_t update_post_handler(httpd_req_t* req) {
  if (req->method != HTTP_POST) {
    httpd_resp_set_status(req, "405 Method Not Allowed");
    httpd_resp_sendstr(req, "Use POST");
    return ESP_OK;
  }

  const int content_len = req->content_len;
  if (content_len <= 0) {
    httpd_resp_set_status(req, "411 Length Required");
    httpd_resp_sendstr(req, "Content-Length required");
    return ESP_OK;
  }

  const esp_partition_t* update_partition = esp_ota_get_next_update_partition(nullptr);
  if (update_partition == nullptr) {
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "No OTA partition (check partition table)");
    return ESP_OK;
  }

  if (static_cast<size_t>(content_len) > update_partition->size) {
    httpd_resp_set_status(req, "413 Payload Too Large");
    httpd_resp_sendstr(req, "Firmware larger than OTA slot");
    return ESP_OK;
  }

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin: %s", esp_err_to_name(err));
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "ota_begin failed");
    return ESP_OK;
  }

  constexpr size_t kBuf = 4096;
  std::array<char, kBuf> buf{};
  int remaining = content_len;
  while (remaining > 0) {
    const int to_read = std::min(static_cast<int>(buf.size()), remaining);
    const int recv_len = httpd_req_recv(req, buf.data(), static_cast<size_t>(to_read));
    if (recv_len < 0) {
      ESP_LOGE(TAG, "httpd_req_recv: %d", recv_len);
      esp_ota_abort(ota_handle);
      httpd_resp_set_status(req, "500 Internal Server Error");
      httpd_resp_sendstr(req, "recv failed");
      return ESP_OK;
    }
    if (recv_len == 0) {
      esp_ota_abort(ota_handle);
      httpd_resp_set_status(req, "400 Bad Request");
      httpd_resp_sendstr(req, "unexpected end of body");
      return ESP_OK;
    }
    err = esp_ota_write(ota_handle, buf.data(), static_cast<size_t>(recv_len));
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_ota_write: %s", esp_err_to_name(err));
      esp_ota_abort(ota_handle);
      httpd_resp_set_status(req, "500 Internal Server Error");
      httpd_resp_sendstr(req, "ota_write failed");
      return ESP_OK;
    }
    remaining -= recv_len;
  }

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    if (err == ESP_ERR_OTA_VALIDATE_FAILED) {
      ESP_LOGW(TAG, "validate failed");
      httpd_resp_set_status(req, "400 Bad Request");
      httpd_resp_sendstr(req, "Invalid firmware image");
    } else {
      ESP_LOGE(TAG, "esp_ota_end: %s", esp_err_to_name(err));
      httpd_resp_set_status(req, "500 Internal Server Error");
      httpd_resp_sendstr(req, "ota_end failed");
    }
    return ESP_OK;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition: %s", esp_err_to_name(err));
    httpd_resp_set_status(req, "500 Internal Server Error");
    httpd_resp_sendstr(req, "set_boot_partition failed");
    return ESP_OK;
  }

  ESP_LOGI(TAG, "OTA OK, reboot…");
  httpd_resp_set_type(req, "text/plain; charset=utf-8");
  httpd_resp_sendstr(req, "OK rebooting");
  vTaskDelay(pdMS_TO_TICKS(300));
  esp_restart();
  return ESP_OK;
}

esp_err_t register_firmware_update_uri(httpd_handle_t server) {
  httpd_uri_t u = {.uri = "/update",
                   .method = HTTP_POST,
                   .handler = update_post_handler,
                   .user_ctx = nullptr};
  return httpd_register_uri_handler(server, &u);
}

}  // namespace aq
