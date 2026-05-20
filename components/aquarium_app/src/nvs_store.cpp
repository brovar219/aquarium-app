#include "nvs_store.hpp"

#include <cstring>

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char* TAG = "nvs_store";
static constexpr const char* kNs = "aquarium";
static constexpr const char* kKey = "cfg";

namespace aq {

bool NvsSettingsStore::load(PersistedSettings& out) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNs, NVS_READONLY, &h);
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "немає збережених налаштувань (%s)", esp_err_to_name(err));
    return false;
  }
  size_t sz = sizeof(out);
  err = nvs_get_blob(h, kKey, &out, &sz);
  nvs_close(h);
  if (err != ESP_OK || sz != sizeof(out)) {
    ESP_LOGW(TAG, "get_blob fail");
    return false;
  }
  if (out.magic != 0xA01A01U) {
    return false;
  }
  return true;
}

bool NvsSettingsStore::save(const PersistedSettings& in) {
  nvs_handle_t h;
  esp_err_t err = nvs_open(kNs, NVS_READWRITE, &h);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "open %s", esp_err_to_name(err));
    return false;
  }
  err = nvs_set_blob(h, kKey, &in, sizeof(in));
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
