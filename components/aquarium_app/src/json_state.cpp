#include "json_state.hpp"

#include <cstdlib>
#include <cstring>

#include "cJSON.h"
#include "esp_app_desc.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

namespace aq {

char* state_to_json_malloc(const DeviceState& s) {
  cJSON* root = cJSON_CreateObject();
  if (!root) {
    return nullptr;
  }
  cJSON_AddStringToObject(root, "type", "state");
  cJSON* d = cJSON_CreateObject();
  if (!d) {
    cJSON_Delete(root);
    return nullptr;
  }
  cJSON_AddItemToObject(root, "data", d);

  cJSON_AddStringToObject(d, "operation_mode", operation_mode_to_api(s.operation_mode).c_str());
  cJSON_AddNumberToObject(d, "scene_mode", static_cast<double>(static_cast<int>(s.scene_mode)));
  cJSON_AddStringToObject(d, "light_program", light_program_to_api(s.light_program).c_str());
  cJSON_AddStringToObject(d, "phase_label", s.phase_label.c_str());

  cJSON_AddNumberToObject(d, "out_r", s.out_r);
  cJSON_AddNumberToObject(d, "out_g", s.out_g);
  cJSON_AddNumberToObject(d, "out_b", s.out_b);
  cJSON_AddNumberToObject(d, "out_w", s.out_w);
  cJSON_AddNumberToObject(d, "out_brightness", s.out_brightness);

  cJSON_AddBoolToObject(d, "pump_on", s.pump_on);
  cJSON_AddBoolToObject(d, "acclimation", s.acclimation);
  cJSON_AddBoolToObject(d, "thermal_throttle", s.thermal_throttle);

  if (s.water_temp_c.has_value()) {
    cJSON_AddNumberToObject(d, "water_temp_c", *s.water_temp_c);
  } else {
    cJSON_AddNullToObject(d, "water_temp_c");
  }

  cJSON_AddBoolToObject(d, "time_valid", s.time_valid);
  cJSON_AddNumberToObject(d, "time_h", s.time_h);
  cJSON_AddNumberToObject(d, "time_m", s.time_m);
  cJSON_AddNumberToObject(d, "time_s", s.time_s);
  cJSON_AddNumberToObject(d, "wifi_rssi", s.wifi_rssi);
  cJSON_AddNumberToObject(d, "uptime_ms", static_cast<double>(s.uptime_ms));

  const esp_app_desc_t* app = esp_app_get_description();
  if (app != nullptr) {
    cJSON_AddStringToObject(d, "fw_version", app->version);
    cJSON_AddStringToObject(d, "fw_idf", app->idf_ver);
  } else {
    cJSON_AddNullToObject(d, "fw_version");
    cJSON_AddNullToObject(d, "fw_idf");
  }
  const esp_partition_t* run = esp_ota_get_running_partition();
  if (run != nullptr && run->label != nullptr) {
    cJSON_AddStringToObject(d, "ota_partition", run->label);
  } else {
    cJSON_AddNullToObject(d, "ota_partition");
  }

  char* out = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return out;
}

}  // namespace aq
