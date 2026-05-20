#include "device_service.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "json_state.hpp"

namespace aq {

static const char* TAG = "device";

static float json_float(cJSON* parent, const char* key) {
  cJSON* i = cJSON_GetObjectItemCaseSensitive(parent, key);
  return cJSON_IsNumber(i) ? static_cast<float>(i->valuedouble) : 0.F;
}

DeviceService::DeviceService(ILightOutput& light, IPumpOutput& pump, ITemperatureSensor& temp,
                              ISettingsStore& store, ITimeSource& time, const IScheduleEngine& schedule)
    : light_(light),
      pump_(pump),
      temp_(temp),
      store_(store),
      time_(time),
      schedule_(schedule),
      mutex_(xSemaphoreCreateMutex()) {
  if (!store_.load(settings_)) {
    ESP_LOGW(TAG, "NVS порожній — типові налаштування");
  }
  state_.operation_mode = settings_.operation_mode;
  state_.light_program = settings_.light_program;
  state_.pump_on = settings_.pump_on;
  state_.acclimation = settings_.acclimation;
  state_.scene_mode = SceneMode::AUTO;
}

void DeviceService::persist_if_dirty() {
  if (!dirty_settings_) {
    return;
  }
  if (store_.save(settings_)) {
    dirty_settings_ = false;
  }
}

void DeviceService::apply_show_guests_locked() {
  state_.phase_label = "Показ (100%)";
  light_.set_rgbw(1.F, 1.F, 1.F, 1.F, 1.F);
  state_.out_r = state_.out_g = state_.out_b = state_.out_w = 1.F;
  state_.out_brightness = 1.F;
}

void DeviceService::apply_manual_light_locked() {
  state_.phase_label = "Ручний режим";
  light_.set_rgbw(manual_.r, manual_.g, manual_.b, manual_.w, manual_.brightness);
  state_.out_r = manual_.r;
  state_.out_g = manual_.g;
  state_.out_b = manual_.b;
  state_.out_w = manual_.w;
  state_.out_brightness = manual_.brightness;
}

void DeviceService::apply_auto_light_locked() {
  if (!time_.is_valid()) {
    state_.phase_label = "Очікування SNTP…";
    light_.turn_off();
    state_.out_brightness = 0.F;
    return;
  }
  int h = 0, m = 0, s = 0;
  time_.local_time(h, m, s);
  const float hour = static_cast<float>(h) + static_cast<float>(m) / 60.F + static_cast<float>(s) / 3600.F;

  std::string phase;
  RgbwTarget t{};
  schedule_.compute(hour, settings_, state_.thermal_throttle, phase, t);
  state_.phase_label = phase;
  if (t.brightness < 0.01F) {
    light_.turn_off();
    state_.out_brightness = 0.F;
    state_.out_r = state_.out_g = state_.out_b = state_.out_w = 0.F;
  } else {
    light_.set_rgbw(t.r, t.g, t.b, t.w, t.brightness);
    state_.out_r = t.r;
    state_.out_g = t.g;
    state_.out_b = t.b;
    state_.out_w = t.w;
    state_.out_brightness = t.brightness;
  }
}

DeviceState DeviceService::snapshot() const {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(200)) != pdTRUE) {
    return {};
  }
  DeviceState c = state_;
  xSemaphoreGive(mutex_);
  return c;
}

void DeviceService::tick() {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) {
    return;
  }

  state_.uptime_ms = esp_timer_get_time() / 1000;
  wifi_ap_record_t ap{};
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
    state_.wifi_rssi = ap.rssi;
  }

  state_.time_valid = time_.is_valid();
  if (state_.time_valid) {
    time_.local_time(state_.time_h, state_.time_m, state_.time_s);
  }

  state_.water_temp_c = temp_.read_celsius();
  if (state_.water_temp_c.has_value()) {
    if (*state_.water_temp_c > 28.F) {
      state_.thermal_throttle = true;
    } else if (*state_.water_temp_c < 26.5F) {
      state_.thermal_throttle = false;
    }
  }

  pump_.set_on(settings_.pump_on);
  state_.pump_on = settings_.pump_on;
  state_.acclimation = settings_.acclimation;
  state_.operation_mode = settings_.operation_mode;
  state_.light_program = settings_.light_program;

  if (state_.scene_mode == SceneMode::SHOW_GUESTS) {
    apply_show_guests_locked();
  } else if (settings_.operation_mode == OperationMode::MANUAL || state_.scene_mode == SceneMode::MANUAL) {
    apply_manual_light_locked();
  } else {
    apply_auto_light_locked();
  }

  persist_if_dirty();
  xSemaphoreGive(mutex_);
}

std::string DeviceService::handle_ws_json(const char* json) {
  cJSON* root = cJSON_Parse(json);
  if (!root) {
    return R"({"type":"error","msg":"json"})";
  }

  cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
  const char* ts = cJSON_IsString(type) ? type->valuestring : "";
  if (strcmp(ts, "cmd") != 0) {
    cJSON_Delete(root);
    return R"({"type":"error","msg":"need type:cmd"})";
  }

  cJSON* name = cJSON_GetObjectItemCaseSensitive(root, "name");
  const char* cmd = (cJSON_IsString(name) && name->valuestring) ? name->valuestring : "";

  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(500)) != pdTRUE) {
    cJSON_Delete(root);
    return R"({"type":"error","msg":"busy"})";
  }

  if (strcmp(cmd, "get_state") == 0) {
    DeviceState st = state_;
    cJSON_Delete(root);
    xSemaphoreGive(mutex_);
    char* js = state_to_json_malloc(st);
    if (!js) {
      return R"({"type":"error","msg":"oom"})";
    }
    std::string out(js);
    free(js);
    return out;
  }

  if (strcmp(cmd, "set_operation_mode") == 0) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(root, "value");
    if (cJSON_IsString(v)) {
      OperationMode m{};
      if (operation_mode_from_api(v->valuestring, m)) {
        settings_.operation_mode = m;
        if (m == OperationMode::AUTO_24H) {
          state_.scene_mode = SceneMode::AUTO;
        }
        dirty_settings_ = true;
      }
    }
    cJSON_Delete(root);
    xSemaphoreGive(mutex_);
    return std::string(R"({"type":"ack"})");
  }

  if (strcmp(cmd, "set_light_program") == 0) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(root, "value");
    if (cJSON_IsString(v)) {
      LightProgram p{};
      if (light_program_from_api(v->valuestring, p)) {
        settings_.light_program = p;
        dirty_settings_ = true;
      }
    }
    cJSON_Delete(root);
    xSemaphoreGive(mutex_);
    return std::string(R"({"type":"ack"})");
  }

  if (strcmp(cmd, "set_manual_rgbw") == 0) {
    manual_.r = json_float(root, "r");
    manual_.g = json_float(root, "g");
    manual_.b = json_float(root, "b");
    manual_.w = json_float(root, "w");
    manual_.brightness = json_float(root, "brightness");
    settings_.operation_mode = OperationMode::MANUAL;
    state_.scene_mode = SceneMode::MANUAL;
    dirty_settings_ = true;
    cJSON_Delete(root);
    xSemaphoreGive(mutex_);
    return std::string(R"({"type":"ack"})");
  }

  if (strcmp(cmd, "set_pump") == 0) {
    cJSON* on = cJSON_GetObjectItemCaseSensitive(root, "on");
    if (cJSON_IsBool(on)) {
      settings_.pump_on = cJSON_IsTrue(on);
      dirty_settings_ = true;
    }
    cJSON_Delete(root);
    xSemaphoreGive(mutex_);
    return std::string(R"({"type":"ack"})");
  }

  if (strcmp(cmd, "set_schedule") == 0) {
    settings_.hour_start = json_float(root, "hour_start");
    settings_.hour_end = json_float(root, "hour_end");
    settings_.hour_moon_end = json_float(root, "hour_moon_end");
    dirty_settings_ = true;
    cJSON_Delete(root);
    xSemaphoreGive(mutex_);
    return std::string(R"({"type":"ack"})");
  }

  if (strcmp(cmd, "set_brightness") == 0) {
    settings_.max_brightness_pct = json_float(root, "max_brightness");
    settings_.brightness_trim_pct = json_float(root, "brightness_trim");
    settings_.moon_brightness_pct = json_float(root, "moon_brightness");
    dirty_settings_ = true;
    cJSON_Delete(root);
    xSemaphoreGive(mutex_);
    return std::string(R"({"type":"ack"})");
  }

  if (strcmp(cmd, "set_flags") == 0) {
    cJSON* a = cJSON_GetObjectItemCaseSensitive(root, "acclimation");
    if (cJSON_IsBool(a)) {
      settings_.acclimation = cJSON_IsTrue(a);
    }
    cJSON* th = cJSON_GetObjectItemCaseSensitive(root, "thermal_throttle");
    if (cJSON_IsBool(th)) {
      state_.thermal_throttle = cJSON_IsTrue(th);
    }
    dirty_settings_ = true;
    cJSON_Delete(root);
    xSemaphoreGive(mutex_);
    return std::string(R"({"type":"ack"})");
  }

  if (strcmp(cmd, "scene_show_guests") == 0) {
    state_.scene_mode = SceneMode::SHOW_GUESTS;
    cJSON_Delete(root);
    xSemaphoreGive(mutex_);
    return std::string(R"({"type":"ack"})");
  }

  if (strcmp(cmd, "scene_end_special") == 0) {
    state_.scene_mode = SceneMode::AUTO;
    cJSON_Delete(root);
    xSemaphoreGive(mutex_);
    return std::string(R"({"type":"ack"})");
  }

  cJSON_Delete(root);
  xSemaphoreGive(mutex_);
  return R"({"type":"error","msg":"unknown cmd"})";
}

}  // namespace aq
