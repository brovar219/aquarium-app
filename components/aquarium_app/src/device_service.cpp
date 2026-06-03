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

// ── helpers ──────────────────────────────────────────────────────

static float json_float(cJSON* parent, const char* key) {
  cJSON* i = cJSON_GetObjectItemCaseSensitive(parent, key);
  return cJSON_IsNumber(i) ? static_cast<float>(i->valuedouble) : 0.F;
}

static float json_float_or(cJSON* parent, const char* key, float fallback) {
  cJSON* i = cJSON_GetObjectItemCaseSensitive(parent, key);
  return cJSON_IsNumber(i) ? static_cast<float>(i->valuedouble) : fallback;
}

static void copy_json_str(char* dest, size_t dest_sz, cJSON* item) {
  if (!dest || !dest_sz) return;
  if (cJSON_IsString(item) && item->valuestring) {
    strncpy(dest, item->valuestring, dest_sz - 1);
    dest[dest_sz - 1] = '\0';
  }
}

static float clampf(float v, float lo, float hi) {
  return v < lo ? lo : (v > hi ? hi : v);
}

static float clamp01(float x) { return clampf(x, 0.F, 1.F); }
static float clamp_hour(float x) { return clampf(x, 0.F, 24.F); }
static float clamp_pct(float x) { return clampf(x, 0.F, 200.F); }

// ── PhaseColors JSON helpers ──────────────────────────────────────

static cJSON* phase_colors_to_json(const PhaseColors& pc) {
  cJSON* o = cJSON_CreateObject();
  if (!o) return nullptr;
  cJSON_AddNumberToObject(o, "r", pc.r);
  cJSON_AddNumberToObject(o, "g", pc.g);
  cJSON_AddNumberToObject(o, "b", pc.b);
  cJSON_AddNumberToObject(o, "w", pc.w);
  return o;
}

static void phase_colors_from_json(cJSON* o, PhaseColors& pc) {
  if (!cJSON_IsObject(o)) return;
  cJSON* i;
  if ((i = cJSON_GetObjectItemCaseSensitive(o, "r")) && cJSON_IsNumber(i)) pc.r = clamp01(static_cast<float>(i->valuedouble));
  if ((i = cJSON_GetObjectItemCaseSensitive(o, "g")) && cJSON_IsNumber(i)) pc.g = clamp01(static_cast<float>(i->valuedouble));
  if ((i = cJSON_GetObjectItemCaseSensitive(o, "b")) && cJSON_IsNumber(i)) pc.b = clamp01(static_cast<float>(i->valuedouble));
  if ((i = cJSON_GetObjectItemCaseSensitive(o, "w")) && cJSON_IsNumber(i)) pc.w = clamp01(static_cast<float>(i->valuedouble));
}

// ── constructor ───────────────────────────────────────────────────

DeviceService::DeviceService(ILightOutput& light, IPumpOutput& pump, ITemperatureSensor& temp,
                              ISettingsStore& store, ITimeSource& time, const IScheduleEngine& schedule)
    : light_(light), pump_(pump), temp_(temp), store_(store), time_(time), schedule_(schedule),
      mutex_(xSemaphoreCreateMutex()) {
  if (!store_.load(settings_)) {
    ESP_LOGW(TAG, "NVS empty — using defaults");
  }
  time_.set_timezone(settings_.timezone_posix);
  state_.operation_mode = settings_.operation_mode;
  state_.light_program  = settings_.light_program;
  state_.pump_on        = settings_.pump_on;
  state_.acclimation    = settings_.acclimation;
  guest_scene_active_   = settings_.resume_scene_mode == SceneMode::SHOW_GUESTS;
  moon_scene_active_    = settings_.resume_scene_mode == SceneMode::MOON_TEMP;
  state_.scene_mode     = settings_.resume_scene_mode;
  refresh_live_state_locked(true);
}

void DeviceService::persist_if_dirty() {
  if (!dirty_settings_) return;
  if (store_.save(settings_)) dirty_settings_ = false;
}

// ── scene helpers ─────────────────────────────────────────────────

void DeviceService::apply_show_guests_locked() {
  state_.phase_label = "Показ (100%)";
  light_.set_rgbw(1.F, 1.F, 1.F, 1.F, 1.F);
  state_.out_r = state_.out_g = state_.out_b = state_.out_w = 1.F;
  state_.out_brightness = 1.F;
}

void DeviceService::apply_manual_moon_locked() {
  state_.phase_label = "Нічне світло";
  const float moon = clampf(settings_.moon_brightness_pct / 100.F, 0.03F, 0.08F);
  const PhaseColors& mc = settings_.custom_phases.moon;
  light_.set_rgbw(mc.r, mc.g, mc.b, mc.w, moon);
  state_.out_r = mc.r; state_.out_g = mc.g;
  state_.out_b = mc.b; state_.out_w = mc.w;
  state_.out_brightness = moon;
}

uint32_t DeviceService::next_lightning_rand_locked(uint32_t mn, uint32_t mx) {
  lightning_rng_ = lightning_rng_ * 1664525U + 1013904223U;
  if (mx <= mn) return mn;
  return mn + (lightning_rng_ % (mx - mn + 1));
}

void DeviceService::apply_lightning_locked() {
  state_.phase_label = lightning_flash_on_ ? "Шторм: спалах" : "Шторм: хмари";

  if (state_.uptime_ms >= lightning_next_step_ms_) {
    if (lightning_flash_on_) {
      lightning_flash_on_ = false;
      if (lightning_flashes_left_ > 0) {
        lightning_next_step_ms_ = state_.uptime_ms + next_lightning_rand_locked(80, 180);
      } else {
        lightning_flashes_left_ = static_cast<int>(next_lightning_rand_locked(2, 4));
        lightning_next_step_ms_ = state_.uptime_ms + next_lightning_rand_locked(1400, 3200);
      }
    } else {
      lightning_flash_on_ = true;
      lightning_next_step_ms_ = state_.uptime_ms + next_lightning_rand_locked(60, 140);
      if (lightning_flashes_left_ > 0) --lightning_flashes_left_;
    }
  }

  if (lightning_flash_on_) {
    const float flash = clampf(static_cast<float>(70 + next_lightning_rand_locked(0, 25)) / 100.F, 0.7F, 0.95F);
    light_.set_rgbw(0.9F, 0.95F, 1.F, 1.F, flash);
    state_.out_r = 0.9F; state_.out_g = 0.95F; state_.out_b = 1.F; state_.out_w = 1.F;
    state_.out_brightness = flash;
  } else {
    light_.set_rgbw(0.04F, 0.08F, 0.22F, 0.1F, 0.035F);
    state_.out_r = 0.04F; state_.out_g = 0.08F; state_.out_b = 0.22F; state_.out_w = 0.1F;
    state_.out_brightness = 0.035F;
  }
}

// ── tick ──────────────────────────────────────────────────────────

void DeviceService::refresh_live_state_locked(bool read_sensor) {
  state_.uptime_ms = esp_timer_get_time() / 1000;

  wifi_ap_record_t ap{};
  if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) state_.wifi_rssi = ap.rssi;

  state_.time_valid = time_.is_valid();
  if (state_.time_valid) time_.local_time(state_.time_h, state_.time_m, state_.time_s);

  if (read_sensor) {
    // Non-blocking — DS18B20 updated by its own background task.
    state_.water_temp_c = temp_.read_celsius();
    if (state_.water_temp_c.has_value()) {
      if (*state_.water_temp_c > 28.F) state_.thermal_throttle = true;
      else if (*state_.water_temp_c < 26.5F) state_.thermal_throttle = false;
    }
  }

  const bool feed_active      = feed_until_ms_      > state_.uptime_ms;
  const bool lightning_active = lightning_until_ms_  > state_.uptime_ms;

  pump_.set_on(settings_.pump_on && !feed_active);
  state_.pump_on        = settings_.pump_on && !feed_active;
  state_.acclimation    = settings_.acclimation;
  state_.operation_mode = settings_.operation_mode;
  state_.light_program  = settings_.light_program;

  if (guest_scene_active_) {
    state_.scene_mode = SceneMode::SHOW_GUESTS;
    apply_show_guests_locked();
  } else if (lightning_active) {
    state_.scene_mode = SceneMode::STORM;
    apply_lightning_locked();
  } else if (moon_scene_active_) {
    state_.scene_mode = SceneMode::MOON_TEMP;
    apply_manual_moon_locked();
  } else if (settings_.operation_mode == OperationMode::MANUAL) {
    state_.scene_mode = feed_active ? SceneMode::FEED : SceneMode::MANUAL;
    apply_manual_light_locked();
  } else {
    state_.scene_mode = feed_active ? SceneMode::FEED : SceneMode::AUTO;
    apply_auto_light_locked();
  }

  if (feed_active) state_.phase_label += " · Кормлення";
}

void DeviceService::tick() {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) != pdTRUE) return;
  refresh_live_state_locked(true);
  persist_if_dirty();
  xSemaphoreGive(mutex_);
}

void DeviceService::apply_manual_light_locked() {
  state_.phase_label = "Ручний режим";
  light_.set_rgbw(settings_.manual_r, settings_.manual_g, settings_.manual_b,
                  settings_.manual_w, settings_.manual_brightness);
  state_.out_r = settings_.manual_r; state_.out_g = settings_.manual_g;
  state_.out_b = settings_.manual_b; state_.out_w = settings_.manual_w;
  state_.out_brightness = settings_.manual_brightness;
}

void DeviceService::apply_auto_light_locked() {
  const bool time_ok       = time_.is_valid();
  const bool time_fallback = !time_ok && state_.uptime_ms > 90'000;
  if (!time_ok && !time_fallback) {
    state_.phase_label = "Очікування SNTP…";
    light_.turn_off();
    state_.out_brightness = 0.F;
    return;
  }
  float hour = 14.F;
  if (time_ok) {
    int h = 0, m = 0, s = 0;
    time_.local_time(h, m, s);
    hour = static_cast<float>(h) + static_cast<float>(m) / 60.F + static_cast<float>(s) / 3600.F;
  }

  std::string ph;
  RgbwTarget t{};
  schedule_.compute(hour, settings_, state_.thermal_throttle, ph, t);
  state_.phase_label = time_fallback ? (ph + " · без SNTP") : ph;

  if (t.brightness < 0.01F) {
    light_.turn_off();
    state_.out_brightness = 0.F;
    state_.out_r = state_.out_g = state_.out_b = state_.out_w = 0.F;
  } else {
    light_.set_rgbw(t.r, t.g, t.b, t.w, t.brightness);
    state_.out_r = t.r; state_.out_g = t.g; state_.out_b = t.b;
    state_.out_w = t.w; state_.out_brightness = t.brightness;
  }
}

DeviceState DeviceService::snapshot() const {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(200)) != pdTRUE) return {};
  DeviceState c = state_;
  xSemaphoreGive(mutex_);
  return c;
}

// ── WS command dispatch ───────────────────────────────────────────

std::string DeviceService::handle_ws_json(const char* json) {
  cJSON* root = cJSON_Parse(json);
  if (!root) return R"({"type":"error","msg":"json"})";

  cJSON* type_j = cJSON_GetObjectItemCaseSensitive(root, "type");
  if (!cJSON_IsString(type_j) || strcmp(type_j->valuestring, "cmd") != 0) {
    cJSON_Delete(root);
    return R"({"type":"error","msg":"need type:cmd"})";
  }

  cJSON* name_j = cJSON_GetObjectItemCaseSensitive(root, "name");
  const char* cmd = (cJSON_IsString(name_j) && name_j->valuestring) ? name_j->valuestring : "";

  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(500)) != pdTRUE) {
    cJSON_Delete(root);
    return R"({"type":"error","msg":"busy"})";
  }

  // ── get_state ────────────────────────────────────────────────
  if (strcmp(cmd, "get_state") == 0) {
    DeviceState st = state_;
    cJSON_Delete(root);
    xSemaphoreGive(mutex_);
    char* js = state_to_json_malloc(st);
    if (!js) return R"({"type":"error","msg":"oom"})";
    std::string out(js); free(js); return out;
  }

  // ── set_operation_mode ────────────────────────────────────────
  if (strcmp(cmd, "set_operation_mode") == 0) {
    cJSON* v = cJSON_GetObjectItemCaseSensitive(root, "value");
    if (cJSON_IsString(v)) {
      OperationMode m{};
      if (operation_mode_from_api(v->valuestring, m)) {
        settings_.operation_mode = m;
        guest_scene_active_ = moon_scene_active_ = false;
        lightning_until_ms_ = 0;
        settings_.resume_scene_mode = (m == OperationMode::AUTO_24H) ? SceneMode::AUTO : SceneMode::MANUAL;
        dirty_settings_ = true;
      }
    }
    persist_if_dirty();
    cJSON_Delete(root); xSemaphoreGive(mutex_);
    return R"({"type":"ack"})";
  }

  // ── set_manual_rgbw ───────────────────────────────────────────
  if (strcmp(cmd, "set_manual_rgbw") == 0) {
    settings_.manual_r          = json_float(root, "r");
    settings_.manual_g          = json_float(root, "g");
    settings_.manual_b          = json_float(root, "b");
    settings_.manual_w          = json_float(root, "w");
    settings_.manual_brightness = json_float(root, "brightness");
    settings_.operation_mode    = OperationMode::MANUAL;
    guest_scene_active_ = moon_scene_active_ = false;
    lightning_until_ms_ = 0;
    settings_.resume_scene_mode = SceneMode::MANUAL;
    dirty_settings_ = true;
    persist_if_dirty();
    cJSON_Delete(root); xSemaphoreGive(mutex_);
    return R"({"type":"ack"})";
  }

  // ── set_pump ──────────────────────────────────────────────────
  if (strcmp(cmd, "set_pump") == 0) {
    cJSON* on = cJSON_GetObjectItemCaseSensitive(root, "on");
    if (cJSON_IsBool(on)) { settings_.pump_on = cJSON_IsTrue(on); dirty_settings_ = true; }
    persist_if_dirty();
    cJSON_Delete(root); xSemaphoreGive(mutex_);
    return R"({"type":"ack"})";
  }

  // ── save_preset ───────────────────────────────────────────────
  if (strcmp(cmd, "save_preset") == 0) {
    cJSON* idx_j = cJSON_GetObjectItemCaseSensitive(root, "index");
    if (cJSON_IsNumber(idx_j)) {
      const int idx = static_cast<int>(idx_j->valuedouble);
      if (idx >= 0 && idx < 4) {
        ManualPreset& p = settings_.presets[idx];
        p.r          = clamp01(json_float_or(root, "r",          1.F));
        p.g          = clamp01(json_float_or(root, "g",          1.F));
        p.b          = clamp01(json_float_or(root, "b",          1.F));
        p.w          = clamp01(json_float_or(root, "w",          1.F));
        p.brightness = clamp01(json_float_or(root, "brightness", 1.F));
        p.valid = true;
        dirty_settings_ = true;
      }
    }
    persist_if_dirty();
    cJSON_Delete(root); xSemaphoreGive(mutex_);
    return R"({"type":"ack"})";
  }

  // ── load_preset ───────────────────────────────────────────────
  if (strcmp(cmd, "load_preset") == 0) {
    cJSON* idx_j = cJSON_GetObjectItemCaseSensitive(root, "index");
    if (cJSON_IsNumber(idx_j)) {
      const int idx = static_cast<int>(idx_j->valuedouble);
      if (idx >= 0 && idx < 4 && settings_.presets[idx].valid) {
        const ManualPreset& p = settings_.presets[idx];
        settings_.manual_r = p.r; settings_.manual_g = p.g;
        settings_.manual_b = p.b; settings_.manual_w = p.w;
        settings_.manual_brightness = p.brightness;
        settings_.operation_mode = OperationMode::MANUAL;
        guest_scene_active_ = moon_scene_active_ = false;
        lightning_until_ms_ = 0;
        settings_.resume_scene_mode = SceneMode::MANUAL;
        dirty_settings_ = true;
        persist_if_dirty();
        cJSON_Delete(root); xSemaphoreGive(mutex_);
        return R"({"type":"ack"})";
      }
    }
    cJSON_Delete(root); xSemaphoreGive(mutex_);
    return R"({"type":"error","msg":"no preset"})";
  }

  // ── scene commands ────────────────────────────────────────────
  if (strcmp(cmd, "scene_show_guests") == 0) {
    guest_scene_active_ = true; moon_scene_active_ = false; lightning_until_ms_ = 0;
    settings_.resume_scene_mode = SceneMode::SHOW_GUESTS; dirty_settings_ = true;
    persist_if_dirty(); cJSON_Delete(root); xSemaphoreGive(mutex_);
    return R"({"type":"ack"})";
  }
  if (strcmp(cmd, "scene_feed_15min") == 0) {
    feed_until_ms_ = (esp_timer_get_time() / 1000) + (15LL * 60 * 1000);
    cJSON_Delete(root); xSemaphoreGive(mutex_);
    return R"({"type":"ack"})";
  }
  if (strcmp(cmd, "scene_manual_moon") == 0) {
    guest_scene_active_ = false; moon_scene_active_ = true; lightning_until_ms_ = 0;
    settings_.resume_scene_mode = SceneMode::MOON_TEMP; dirty_settings_ = true;
    persist_if_dirty(); cJSON_Delete(root); xSemaphoreGive(mutex_);
    return R"({"type":"ack"})";
  }
  if (strcmp(cmd, "scene_lightning") == 0) {
    guest_scene_active_ = moon_scene_active_ = false;
    state_.uptime_ms = esp_timer_get_time() / 1000;
    lightning_rng_ ^= static_cast<uint32_t>(state_.uptime_ms);
    lightning_until_ms_ = state_.uptime_ms + 12000;
    lightning_flashes_left_ = static_cast<int>(next_lightning_rand_locked(2, 4));
    lightning_flash_on_ = false;
    lightning_next_step_ms_ = state_.uptime_ms + next_lightning_rand_locked(120, 280);
    cJSON_Delete(root); xSemaphoreGive(mutex_);
    return R"({"type":"ack"})";
  }
  if (strcmp(cmd, "scene_end_special") == 0) {
    guest_scene_active_ = moon_scene_active_ = false;
    lightning_until_ms_ = feed_until_ms_ = 0;
    settings_.resume_scene_mode = (settings_.operation_mode == OperationMode::MANUAL)
                                  ? SceneMode::MANUAL : SceneMode::AUTO;
    dirty_settings_ = true;
    persist_if_dirty(); cJSON_Delete(root); xSemaphoreGive(mutex_);
    return R"({"type":"ack"})";
  }

  // ── legacy commands (still accepted) ─────────────────────────
  if (strcmp(cmd, "set_schedule") == 0) {
    settings_.hour_start    = clamp_hour(json_float(root, "hour_start"));
    settings_.hour_end      = clamp_hour(json_float(root, "hour_end"));
    settings_.hour_moon_end = clamp_hour(json_float(root, "hour_moon_end"));
    dirty_settings_ = true; persist_if_dirty();
    cJSON_Delete(root); xSemaphoreGive(mutex_);
    return R"({"type":"ack"})";
  }
  if (strcmp(cmd, "set_flags") == 0) {
    cJSON* a = cJSON_GetObjectItemCaseSensitive(root, "acclimation");
    if (cJSON_IsBool(a)) settings_.acclimation = cJSON_IsTrue(a);
    dirty_settings_ = true; persist_if_dirty();
    cJSON_Delete(root); xSemaphoreGive(mutex_);
    return R"({"type":"ack"})";
  }

  cJSON_Delete(root); xSemaphoreGive(mutex_);
  return R"({"type":"error","msg":"unknown cmd"})";
}

// ── settings export / import ──────────────────────────────────────

char* DeviceService::export_settings_data_json_malloc() const {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(500)) != pdTRUE) return nullptr;

  cJSON* d = cJSON_CreateObject();
  if (!d) { xSemaphoreGive(mutex_); return nullptr; }

  cJSON_AddStringToObject(d, "operation_mode", operation_mode_to_api(settings_.operation_mode).c_str());
  cJSON_AddStringToObject(d, "light_program",  light_program_to_api(settings_.light_program).c_str());
  cJSON_AddNumberToObject(d, "hour_start",          settings_.hour_start);
  cJSON_AddNumberToObject(d, "hour_end",            settings_.hour_end);
  cJSON_AddNumberToObject(d, "hour_moon_end",       settings_.hour_moon_end);
  cJSON_AddNumberToObject(d, "max_brightness_pct",  settings_.max_brightness_pct);
  cJSON_AddNumberToObject(d, "brightness_trim_pct", settings_.brightness_trim_pct);
  cJSON_AddNumberToObject(d, "moon_brightness_pct", settings_.moon_brightness_pct);
  cJSON_AddBoolToObject(d,   "acclimation",         settings_.acclimation);
  cJSON_AddBoolToObject(d,   "pump_on",             settings_.pump_on);
  cJSON_AddBoolToObject(d,   "thermal_throttle",    state_.thermal_throttle);
  cJSON_AddNumberToObject(d, "scene_mode",          static_cast<double>(static_cast<int>(state_.scene_mode)));
  cJSON_AddStringToObject(d, "timezone_country",    settings_.timezone_country);
  cJSON_AddStringToObject(d, "timezone_posix",      settings_.timezone_posix);

  // manual RGBW
  cJSON* man = cJSON_CreateObject();
  if (man) {
    cJSON_AddNumberToObject(man, "r",          settings_.manual_r);
    cJSON_AddNumberToObject(man, "g",          settings_.manual_g);
    cJSON_AddNumberToObject(man, "b",          settings_.manual_b);
    cJSON_AddNumberToObject(man, "w",          settings_.manual_w);
    cJSON_AddNumberToObject(man, "brightness", settings_.manual_brightness);
    cJSON_AddItemToObject(d, "manual", man);
  }

  // custom phases
  cJSON* cp = cJSON_CreateObject();
  if (cp) {
    cJSON* dawn = phase_colors_to_json(settings_.custom_phases.dawn);
    cJSON* day  = phase_colors_to_json(settings_.custom_phases.day);
    cJSON* moon = phase_colors_to_json(settings_.custom_phases.moon);
    if (dawn) cJSON_AddItemToObject(cp, "dawn", dawn);
    if (day)  cJSON_AddItemToObject(cp, "day",  day);
    if (moon) cJSON_AddItemToObject(cp, "moon", moon);
    cJSON_AddItemToObject(d, "custom_phases", cp);
  }

  // presets
  cJSON* pa = cJSON_CreateArray();
  if (pa) {
    for (int i = 0; i < 4; ++i) {
      const ManualPreset& p = settings_.presets[i];
      cJSON* pj = cJSON_CreateObject();
      if (pj) {
        cJSON_AddBoolToObject(pj,   "valid",      p.valid);
        cJSON_AddNumberToObject(pj, "r",          p.r);
        cJSON_AddNumberToObject(pj, "g",          p.g);
        cJSON_AddNumberToObject(pj, "b",          p.b);
        cJSON_AddNumberToObject(pj, "w",          p.w);
        cJSON_AddNumberToObject(pj, "brightness", p.brightness);
        cJSON_AddItemToArray(pa, pj);
      }
    }
    cJSON_AddItemToObject(d, "presets", pa);
  }

  xSemaphoreGive(mutex_);
  char* out = cJSON_PrintUnformatted(d);
  cJSON_Delete(d);
  return out;
}

std::string DeviceService::import_settings_data_json(const char* json_object) {
  cJSON* root = cJSON_Parse(json_object);
  if (!root || !cJSON_IsObject(root)) { cJSON_Delete(root); return "json"; }

  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(500)) != pdTRUE) { cJSON_Delete(root); return "busy"; }

  cJSON* it;

  it = cJSON_GetObjectItemCaseSensitive(root, "operation_mode");
  if (cJSON_IsString(it) && it->valuestring) {
    OperationMode m{};
    if (operation_mode_from_api(it->valuestring, m)) {
      settings_.operation_mode = m;
      settings_.resume_scene_mode = (m == OperationMode::AUTO_24H) ? SceneMode::AUTO : SceneMode::MANUAL;
      guest_scene_active_ = moon_scene_active_ = false;
      lightning_until_ms_ = 0;
      state_.scene_mode = settings_.resume_scene_mode;
      dirty_settings_ = true;
    }
  }

  it = cJSON_GetObjectItemCaseSensitive(root, "light_program");
  if (cJSON_IsString(it) && it->valuestring) {
    LightProgram p{};
    if (light_program_from_api(it->valuestring, p)) { settings_.light_program = p; dirty_settings_ = true; }
  }

#define LOAD_HOUR(field, key) \
  it = cJSON_GetObjectItemCaseSensitive(root, key); \
  if (cJSON_IsNumber(it)) { settings_.field = clamp_hour(static_cast<float>(it->valuedouble)); dirty_settings_ = true; }

#define LOAD_PCT(field, key) \
  it = cJSON_GetObjectItemCaseSensitive(root, key); \
  if (cJSON_IsNumber(it)) { settings_.field = clamp_pct(static_cast<float>(it->valuedouble)); dirty_settings_ = true; }

  LOAD_HOUR(hour_start,    "hour_start")
  LOAD_HOUR(hour_end,      "hour_end")
  LOAD_HOUR(hour_moon_end, "hour_moon_end")
  LOAD_PCT(max_brightness_pct,  "max_brightness_pct")
  LOAD_PCT(brightness_trim_pct, "brightness_trim_pct")
  LOAD_PCT(moon_brightness_pct, "moon_brightness_pct")

#undef LOAD_HOUR
#undef LOAD_PCT

  it = cJSON_GetObjectItemCaseSensitive(root, "acclimation");
  if (cJSON_IsBool(it)) { settings_.acclimation = cJSON_IsTrue(it); dirty_settings_ = true; }

  it = cJSON_GetObjectItemCaseSensitive(root, "pump_on");
  if (cJSON_IsBool(it)) { settings_.pump_on = cJSON_IsTrue(it); dirty_settings_ = true; }

  it = cJSON_GetObjectItemCaseSensitive(root, "timezone_country");
  if (cJSON_IsString(it) && it->valuestring) {
    copy_json_str(settings_.timezone_country, sizeof(settings_.timezone_country), it);
    dirty_settings_ = true;
  }
  it = cJSON_GetObjectItemCaseSensitive(root, "timezone_posix");
  if (cJSON_IsString(it) && it->valuestring) {
    copy_json_str(settings_.timezone_posix, sizeof(settings_.timezone_posix), it);
    time_.set_timezone(settings_.timezone_posix);
    dirty_settings_ = true;
  }

  // manual
  it = cJSON_GetObjectItemCaseSensitive(root, "manual");
  if (cJSON_IsObject(it)) {
    cJSON* j;
    if ((j = cJSON_GetObjectItemCaseSensitive(it, "r"))          && cJSON_IsNumber(j)) { settings_.manual_r          = clamp01(static_cast<float>(j->valuedouble)); dirty_settings_ = true; }
    if ((j = cJSON_GetObjectItemCaseSensitive(it, "g"))          && cJSON_IsNumber(j)) { settings_.manual_g          = clamp01(static_cast<float>(j->valuedouble)); dirty_settings_ = true; }
    if ((j = cJSON_GetObjectItemCaseSensitive(it, "b"))          && cJSON_IsNumber(j)) { settings_.manual_b          = clamp01(static_cast<float>(j->valuedouble)); dirty_settings_ = true; }
    if ((j = cJSON_GetObjectItemCaseSensitive(it, "w"))          && cJSON_IsNumber(j)) { settings_.manual_w          = clamp01(static_cast<float>(j->valuedouble)); dirty_settings_ = true; }
    if ((j = cJSON_GetObjectItemCaseSensitive(it, "brightness")) && cJSON_IsNumber(j)) { settings_.manual_brightness = clamp01(static_cast<float>(j->valuedouble)); dirty_settings_ = true; }
  }

  // custom_phases
  it = cJSON_GetObjectItemCaseSensitive(root, "custom_phases");
  if (cJSON_IsObject(it)) {
    phase_colors_from_json(cJSON_GetObjectItemCaseSensitive(it, "dawn"), settings_.custom_phases.dawn);
    phase_colors_from_json(cJSON_GetObjectItemCaseSensitive(it, "day"),  settings_.custom_phases.day);
    phase_colors_from_json(cJSON_GetObjectItemCaseSensitive(it, "moon"), settings_.custom_phases.moon);
    dirty_settings_ = true;
  }

  if (dirty_settings_) { store_.save(settings_); dirty_settings_ = false; }

  cJSON_Delete(root);
  xSemaphoreGive(mutex_);
  return {};
}

}  // namespace aq
