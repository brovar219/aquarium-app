#include "mqtt_nvs.hpp"

#include <cstring>

#include "esp_log.h"
#include "nvs.h"

static const char* TAG = "mqtt_nvs";
static constexpr const char* kNs = "aquarium";
static constexpr const char* kKey = "mqtt_cfg";

namespace aq {

bool mqtt_nvs_load(MqttNvsConfig& out) {
  nvs_handle_t h{};
  esp_err_t err = nvs_open(kNs, NVS_READONLY, &h);
  if (err != ESP_OK) {
    return false;
  }
  MqttNvsConfig tmp{};
  size_t sz = sizeof(tmp);
  err = nvs_get_blob(h, kKey, &tmp, &sz);
  nvs_close(h);
  if (err != ESP_OK || sz != sizeof(MqttNvsConfig)) {
    return false;
  }
  if (tmp.magic != 0x4D545451U) {
    return false;
  }
  out = tmp;
  return true;
}

bool mqtt_nvs_save(const MqttNvsConfig& in) {
  nvs_handle_t h{};
  esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "open %s", esp_err_to_name(err));
    return false;
  }
  MqttNvsConfig copy = in;
  copy.magic = 0x4D545451U;
  copy.version = 1;
  err = nvs_set_blob(h, kKey, &copy, sizeof(copy));
  if (err == ESP_OK) {
    err = nvs_commit(h);
  }
  nvs_close(h);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "save %s", esp_err_to_name(err));
    return false;
  }
  return true;
}

}  // namespace aq
