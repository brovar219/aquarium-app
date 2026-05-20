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
  light_.set_rgbw(settings_.manual_r, settings_.manual_g, settings_.manual_b, settings_.manual_w,
                  settings_.manual_brightness);
  state_.out_r = settings_.manual_r;
  state_.out_g = settings_.manual_g;
  state_.out_b = settings_.manual_b;
  state_.out_w = settings_.manual_w;
  state_.out_brightness = settings_.manual_brightness;
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
    settings_.manual_r = json_float(root, "r");
    settings_.manual_g = json_float(root, "g");
    settings_.manual_b = json_float(root, "b");
    settings_.manual_w = json_float(root, "w");
    settings_.manual_brightness = json_float(root, "brightness");
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

static float clamp01(float x) {
  if (x < 0.F) {
    return 0.F;
  }
  if (x > 1.F) {
    return 1.F;
  }
  return x;
}

static float clamp_hour(float x) {
  if (x < 0.F) {
    return 0.F;
  }
  if (x > 24.F) {
    return 24.F;
  }
  return x;
}

static float clamp_pct(float x) {
  if (x < 0.F) {
    return 0.F;
  }
  if (x > 200.F) {
    return 200.F;
  }
  return x;
}

char* DeviceService::export_settings_data_json_malloc() const {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(500)) != pdTRUE) {
    return nullptr;
  }
  cJSON* d = cJSON_CreateObject();
  if (!d) {
    xSemaphoreGive(mutex_);
    return nullptr;
  }
  cJSON_AddStringToObject(d, "operation_mode", operation_mode_to_api(settings_.operation_mode).c_str());
  cJSON_AddStringToObject(d, "light_program", light_program_to_api(settings_.light_program).c_str());
  cJSON_AddNumberToObject(d, "hour_start", settings_.hour_start);
  cJSON_AddNumberToObject(d, "hour_end", settings_.hour_end);
  cJSON_AddNumberToObject(d, "hour_moon_end", settings_.hour_moon_end);
  cJSON_AddNumberToObject(d, "max_brightness_pct", settings_.max_brightness_pct);
  cJSON_AddNumberToObject(d, "brightness_trim_pct", settings_.brightness_trim_pct);
  cJSON_AddNumberToObject(d, "moon_brightness_pct", settings_.moon_brightness_pct);
  cJSON_AddBoolToObject(d, "acclimation", settings_.acclimation);
  cJSON_AddBoolToObject(d, "pump_on", settings_.pump_on);
  cJSON_AddBoolToObject(d, "thermal_throttle", state_.thermal_throttle);
  cJSON_AddNumberToObject(d, "scene_mode", static_cast<double>(static_cast<int>(state_.scene_mode)));

  cJSON* man = cJSON_CreateObject();
  if (man) {
    cJSON_AddNumberToObject(man, "r", settings_.manual_r);
    cJSON_AddNumberToObject(man, "g", settings_.manual_g);
    cJSON_AddNumberToObject(man, "b", settings_.manual_b);
    cJSON_AddNumberToObject(man, "w", settings_.manual_w);
    cJSON_AddNumberToObject(man, "brightness", settings_.manual_brightness);
    cJSON_AddItemToObject(d, "manual", man);
  }

  xSemaphoreGive(mutex_);
  char* out = cJSON_PrintUnformatted(d);
  cJSON_Delete(d);
  return out;
}

std::string DeviceService::import_settings_data_json(const char* json_object) {
  cJSON* root = cJSON_Parse(json_object);
  if (!root || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return "json";
  }

  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(500)) != pdTRUE) {
    cJSON_Delete(root);
    return "busy";
  }

  cJSON* it = cJSON_GetObjectItemCaseSensitive(root, "operation_mode");
  if (cJSON_IsString(it) && it->valuestring) {
    OperationMode m{};
    if (operation_mode_from_api(it->valuestring, m)) {
      settings_.operation_mode = m;
      if (m == OperationMode::AUTO_24H) {
        state_.scene_mode = SceneMode::AUTO;
      }
      dirty_settings_ = true;
    }
  }

  it = cJSON_GetObjectItemCaseSensitive(root, "light_program");
  if (cJSON_IsString(it) && it->valuestring) {
    LightProgram p{};
    if (light_program_from_api(it->valuestring, p)) {
      settings_.light_program = p;
      dirty_settings_ = true;
    }
  }

  it = cJSON_GetObjectItemCaseSensitive(root, "hour_start");
  if (cJSON_IsNumber(it)) {
    settings_.hour_start = clamp_hour(static_cast<float>(it->valuedouble));
    dirty_settings_ = true;
  }
  it = cJSON_GetObjectItemCaseSensitive(root, "hour_end");
  if (cJSON_IsNumber(it)) {
    settings_.hour_end = clamp_hour(static_cast<float>(it->valuedouble));
    dirty_settings_ = true;
  }
  it = cJSON_GetObjectItemCaseSensitive(root, "hour_moon_end");
  if (cJSON_IsNumber(it)) {
    settings_.hour_moon_end = clamp_hour(static_cast<float>(it->valuedouble));
    dirty_settings_ = true;
  }

  it = cJSON_GetObjectItemCaseSensitive(root, "max_brightness_pct");
  if (cJSON_IsNumber(it)) {
    settings_.max_brightness_pct = clamp_pct(static_cast<float>(it->valuedouble));
    dirty_settings_ = true;
  }
  it = cJSON_GetObjectItemCaseSensitive(root, "brightness_trim_pct");
  if (cJSON_IsNumber(it)) {
    settings_.brightness_trim_pct = clamp_pct(static_cast<float>(it->valuedouble));
    dirty_settings_ = true;
  }
  it = cJSON_GetObjectItemCaseSensitive(root, "moon_brightness_pct");
  if (cJSON_IsNumber(it)) {
    settings_.moon_brightness_pct = clamp_pct(static_cast<float>(it->valuedouble));
    dirty_settings_ = true;
  }

  it = cJSON_GetObjectItemCaseSensitive(root, "acclimation");
  if (cJSON_IsBool(it)) {
    settings_.acclimation = cJSON_IsTrue(it);
    dirty_settings_ = true;
  }
  it = cJSON_GetObjectItemCaseSensitive(root, "pump_on");
  if (cJSON_IsBool(it)) {
    settings_.pump_on = cJSON_IsTrue(it);
    dirty_settings_ = true;
  }

  it = cJSON_GetObjectItemCaseSensitive(root, "thermal_throttle");
  if (cJSON_IsBool(it)) {
    state_.thermal_throttle = cJSON_IsTrue(it);
    dirty_settings_ = true;
  }

  it = cJSON_GetObjectItemCaseSensitive(root, "scene_mode");
  if (cJSON_IsNumber(it)) {
    const int v = static_cast<int>(it->valuedouble);
    if (v >= 0 && v <= 6) {
      state_.scene_mode = static_cast<SceneMode>(v);
    }
  }

  it = cJSON_GetObjectItemCaseSensitive(root, "manual");
  if (cJSON_IsObject(it)) {
    cJSON* j = cJSON_GetObjectItemCaseSensitive(it, "r");
    if (cJSON_IsNumber(j)) {
      settings_.manual_r = clamp01(static_cast<float>(j->valuedouble));
      dirty_settings_ = true;
    }
    j = cJSON_GetObjectItemCaseSensitive(it, "g");
    if (cJSON_IsNumber(j)) {
      settings_.manual_g = clamp01(static_cast<float>(j->valuedouble));
      dirty_settings_ = true;
    }
    j = cJSON_GetObjectItemCaseSensitive(it, "b");
    if (cJSON_IsNumber(j)) {
      settings_.manual_b = clamp01(static_cast<float>(j->valuedouble));
      dirty_settings_ = true;
    }
    j = cJSON_GetObjectItemCaseSensitive(it, "w");
    if (cJSON_IsNumber(j)) {
      settings_.manual_w = clamp01(static_cast<float>(j->valuedouble));
      dirty_settings_ = true;
    }
    j = cJSON_GetObjectItemCaseSensitive(it, "brightness");
    if (cJSON_IsNumber(j)) {
      settings_.manual_brightness = clamp01(static_cast<float>(j->valuedouble));
      dirty_settings_ = true;
    }
  }

  cJSON_Delete(root);
  xSemaphoreGive(mutex_);
  return {};
}

}  // namespace aq
