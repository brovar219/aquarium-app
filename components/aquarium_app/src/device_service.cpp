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
  const float moon = clampf(settings_.moon_brightness_pct / 100.F, 0.03F, 0.30F);
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

// ──────────────────────────────────────────────────────────────────
// ГРОЗА — швидкий цикл (~15 мс) з моделлю на затуханні (afterglow).
//
// Природна блискавка = головний розряд + кілька обратних розрядів за ~0.2 с
// (характерне мерехтіння). Тут кожен «спайк» миттєво підіймає яскравість, а
// потім вона експоненційно згасає кожен крок — так виходить природний хвіст
// післясвічення, а серія спайків дає дрож. Сила удару (далекий/близький) і
// частота ударів пливуть за огинаючою грози (наростання → пік → затихання).
// ──────────────────────────────────────────────────────────────────
void DeviceService::drive_storm_fast_locked(int64_t now) {
  const float ambient = 0.035F;  // тёмне синє грозове марево між спалахами

  // Огинаюча грози: 0 на старті/в кінці, 1 на піку (дзвін). Керує частотою
  // та яскравістю ударів — гроза наближається й віддаляється.
  const float dur = static_cast<float>(lightning_until_ms_ - lightning_start_ms_);
  float p = dur > 1.F ? static_cast<float>(now - lightning_start_ms_) / dur : 1.F;
  p = clampf(p, 0.F, 1.F);
  const float env = std::sin(3.14159265F * p);

  const bool flashing = now < lightning_flash_end_ms_;

  // Початок нового удару. Три ТИПИ вспышки (як у природі):
  //   0 СТРІЛА  — близький різкий короткий яскравий удар (90–200 мс).
  //   1 ЗІРНИЦЯ — далекий розлив, освітлює все, ДОВГИЙ і м'який (350–650 мс).
  //   2 ВІДБЛИСК— слабкий синюватий далекий спалах (220–450 мс).
  // Вспышка тримається свою тривалість і всередині дрожить (не гасне у темряву) —
  // саме це читається як мерехтіння блискавки.
  if (!flashing && now >= lightning_next_strike_ms_) {
    const uint32_t roll = next_lightning_rand_locked(0, 99);
    uint32_t dur;
    if (roll < 45) {            // СТРІЛА (45%)
      lightning_type_ = 0;
      lightning_intensity_ = clampf((0.90F + next_lightning_rand_locked(0, 10) / 100.F) * (0.85F + 0.15F * env), 0.5F, 1.F);
      dur = next_lightning_rand_locked(90, 200);
    } else if (roll < 78) {     // ЗІРНИЦЯ (33%)
      lightning_type_ = 1;
      lightning_intensity_ = clampf((0.55F + next_lightning_rand_locked(0, 25) / 100.F) * (0.80F + 0.20F * env), 0.4F, 1.F);
      dur = next_lightning_rand_locked(350, 650);
    } else {                    // ВІДБЛИСК (22%)
      lightning_type_ = 2;
      lightning_intensity_ = clampf(0.30F + next_lightning_rand_locked(0, 20) / 100.F, 0.25F, 0.6F);
      dur = next_lightning_rand_locked(220, 450);
    }
    lightning_flash_end_ms_ = now + dur;
    lightning_next_spike_ms_ = now;  // дрож одразу
    // Інтервал до наступного удару: 35% — КЛАСТЕР (швидкий повтор, гроза «рвана»),
    // інакше довга пауза (на піку грози коротша).
    if (next_lightning_rand_locked(0, 99) < 35) {
      lightning_next_strike_ms_ = lightning_flash_end_ms_ + next_lightning_rand_locked(120, 650);
    } else {
      const uint32_t gmin = static_cast<uint32_t>(2400.F + (900.F - 2400.F) * env);
      const uint32_t gmax = static_cast<uint32_t>(5500.F + (2200.F - 5500.F) * env);
      lightning_next_strike_ms_ = lightning_flash_end_ms_ + next_lightning_rand_locked(gmin, gmax);
    }
  }

  if (now < lightning_flash_end_ms_) {
    // Каденція й глибина дрожання залежать від типу: стріла — швидко й різко,
    // зірниця — повільно й м'яко (стале яскраве світло, що ледь тремтить).
    uint32_t cad_min, cad_max;
    float floor_k;
    if (lightning_type_ == 0)      { cad_min = 14; cad_max = 30; floor_k = 0.45F; }
    else if (lightning_type_ == 1) { cad_min = 32; cad_max = 62; floor_k = 0.62F; }
    else                           { cad_min = 26; cad_max = 52; floor_k = 0.50F; }

    if (now >= lightning_next_spike_ms_) {
      const float k = floor_k + next_lightning_rand_locked(0, 100) / 100.F * (1.F - floor_k);
      lightning_level_ = clampf(lightning_intensity_ * k, ambient, 1.F);
      lightning_next_spike_ms_ = now + next_lightning_rand_locked(cad_min, cad_max);
    }

    // М'який хвіст: останні fade_len мс плавно гасимо до фону (зірниця — довше).
    const int64_t fade_len = (lightning_type_ == 0) ? 45 : (lightning_type_ == 1 ? 160 : 110);
    const int64_t left = lightning_flash_end_ms_ - now;
    if (left < fade_len) {
      const float f = static_cast<float>(left) / static_cast<float>(fade_len);
      lightning_level_ = ambient + (lightning_level_ - ambient) * f;
    }
  } else {
    lightning_level_ = ambient;  // між ударами — темне синє марево
  }

  float r, g, b, w, br;
  if (lightning_level_ > ambient + 0.01F) {
    // Колір за типом: близький удар біліший, далекий — синіший (розсіювання).
    if (lightning_type_ == 0)      { r = 0.70F; g = 0.90F; b = 1.F; w = 1.00F; }  // стріла
    else if (lightning_type_ == 1) { r = 0.62F; g = 0.85F; b = 1.F; w = 0.92F; }  // зірниця
    else                           { r = 0.42F; g = 0.70F; b = 1.F; w = 0.60F; }  // відблиск
    br = lightning_level_;
    state_.phase_label = "Шторм: блискавка";
  } else {
    r = 0.04F; g = 0.08F; b = 0.22F; w = 0.10F; br = ambient;
    state_.phase_label = "Шторм: хмари";
  }
  light_.set_rgbw(r, g, b, w, br);
  state_.out_r = r; state_.out_g = g; state_.out_b = b; state_.out_w = w;
  state_.out_brightness = br;
  state_.scene_mode = SceneMode::STORM;
}

void DeviceService::weather_get_city(char* buf, size_t buf_len) const {
  if (buf == nullptr || buf_len == 0) return;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(100)) == pdTRUE) {
    strncpy(buf, settings_.weather_city, buf_len - 1);
    buf[buf_len - 1] = '\0';
    xSemaphoreGive(mutex_);
  } else {
    buf[0] = '\0';
  }
}

void DeviceService::weather_set_result(const WeatherData& wd, const char* resolved_city) {
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(200)) != pdTRUE) return;
  state_.weather_valid   = wd.valid;
  state_.weather_code    = wd.code;
  state_.weather_cloud   = wd.cloud_cover;
  state_.weather_temp_c  = wd.temp_c;
  state_.weather_wind    = wd.wind_kmh;
  state_.weather_is_day  = wd.is_day;
  if (resolved_city != nullptr) state_.weather_city = resolved_city;
  xSemaphoreGive(mutex_);
}

void DeviceService::arm_storm_locked(int64_t now, int duration_ms) {
  lightning_rng_ ^= static_cast<uint32_t>(now);
  lightning_start_ms_ = now;
  lightning_until_ms_ = now + duration_ms;
  lightning_level_ = 0.035F;
  lightning_intensity_ = 0.F;
  lightning_flash_end_ms_ = 0;
  lightning_next_strike_ms_ = now + next_lightning_rand_locked(300, 900);
  lightning_next_spike_ms_ = now;
}

bool DeviceService::storm_task_step() {
  bool active = false;
  if (xSemaphoreTake(mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
    const int64_t now = esp_timer_get_time() / 1000;
    active = now < lightning_until_ms_;
    if (active) drive_storm_fast_locked(now);
    xSemaphoreGive(mutex_);
  }
  return active;
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

  // Реальна гроза за погодою (під галочкою) → періодично заводимо storm-ефект
  // (30 с кожні 3–5 хв, поки погода грозова), тільки в авто-режимі.
  if (settings_.weather_link && state_.weather_valid &&
      settings_.operation_mode == OperationMode::AUTO_24H && !guest_scene_active_ &&
      (state_.weather_code == 95 || state_.weather_code == 96 || state_.weather_code == 99)) {
    if (state_.uptime_ms >= next_weather_storm_ms_ && lightning_until_ms_ <= state_.uptime_ms) {
      arm_storm_locked(state_.uptime_ms, 30000);
      next_weather_storm_ms_ =
          state_.uptime_ms + 180000 + static_cast<int64_t>(next_lightning_rand_locked(0, 120)) * 1000;
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
    // Світлом керує швидка задача aq_storm (drive_storm_fast_locked) ~15 мс,
    // тут лише фіксуємо сцену й не чіпаємо вихід, щоб не було двох письменників.
    state_.scene_mode = SceneMode::STORM;
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

// Модифікує денний таргет за реальною погодою (тільки коли увімкнено галочку).
// Хмарність гасить яскравість; опади/туман додають приглушення й зсув кольору.
void DeviceService::apply_weather_to_target_locked(RgbwTarget& t) {
  const float cloud = clampf(state_.weather_cloud / 100.F, 0.F, 1.F);
  float dim = 1.F - 0.55F * cloud;  // ясно=1.0, суцільна хмарність≈0.45 (і місяць тьмяніє)
  const int c = state_.weather_code;
  bool greyer = false, cooler = false;
  if (c == 45 || c == 48) {                                  // туман
    dim *= 0.75F; greyer = true;
  } else if ((c >= 51 && c <= 67) || (c >= 80 && c <= 82)) {  // мряка/дощ/зливи
    dim *= 0.85F; cooler = true;
  } else if ((c >= 71 && c <= 77) || c == 85 || c == 86) {    // сніг
    dim *= 0.95F;
  }
  t.brightness = clampf(t.brightness * dim, 0.F, 1.F);
  if (cooler) {  // дощ — холодніше: трохи прибрати червоний, додати синього
    t.r *= 0.90F;
    t.b = std::min(1.F, t.b * 1.05F);
  }
  if (greyer) {  // туман — сіріше: підтягнути канали до середнього (десатурація)
    const float avg = (t.r + t.g + t.b) / 3.F;
    t.r += (avg - t.r) * 0.30F;
    t.g += (avg - t.g) * 0.30F;
    t.b += (avg - t.b) * 0.30F;
    t.w = std::min(1.F, t.w * 1.10F);
  }
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
  if (settings_.weather_link && state_.weather_valid) {
    apply_weather_to_target_locked(t);
    ph += " · погода";
  }
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
    const int64_t now = esp_timer_get_time() / 1000;
    state_.uptime_ms = now;
    arm_storm_locked(now, 30000);  // ~30 с грози з огинаючою наростання→пік→затихання
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
  cJSON_AddStringToObject(d, "weather_city",        settings_.weather_city);
  cJSON_AddBoolToObject(d,   "weather_link",        settings_.weather_link);

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

  it = cJSON_GetObjectItemCaseSensitive(root, "weather_city");
  if (cJSON_IsString(it) && it->valuestring) {
    copy_json_str(settings_.weather_city, sizeof(settings_.weather_city), it);
    dirty_settings_ = true;
  }
  it = cJSON_GetObjectItemCaseSensitive(root, "weather_link");
  if (cJSON_IsBool(it)) { settings_.weather_link = cJSON_IsTrue(it); dirty_settings_ = true; }

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
