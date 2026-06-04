#pragma once

#include "interfaces.hpp"
#include "model.hpp"
#include "weather_client.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string>

namespace aq {

class DeviceService {
 public:
  DeviceService(ILightOutput& light, IPumpOutput& pump, ITemperatureSensor& temp,
                ISettingsStore& store, ITimeSource& time, const IScheduleEngine& schedule);

  void tick();

  /** Один крок швидкого циклу грози (~15 мс). Повертає true, поки гроза активна,
   *  щоб задача обирала швидку (15 мс) чи холосту (150 мс) каденцію. */
  bool storm_task_step();

  /** Копіює налаштоване місто для погоди у buf (для фонової задачі). */
  void weather_get_city(char* buf, size_t buf_len) const;
  /** Записує свіжий результат погоди у стан (з фонової задачі). */
  void weather_set_result(const WeatherData& wd, const char* resolved_city);

  /** Обробка однієї WS JSON-команди; повертає текст відповіді (state або ack). */
  std::string handle_ws_json(const char* json);

  /** JSON-об'єкт полів налаштувань (malloc — викликати `free()`). */
  char* export_settings_data_json_malloc() const;
  /** Часткове оновлення з JSON-об'єкта; порожній рядок = ОК, інакше код помилки. */
  std::string import_settings_data_json(const char* json_object);

  DeviceState snapshot() const;

 private:
  void apply_auto_light_locked();
  void apply_manual_light_locked();
  void apply_show_guests_locked();
  void apply_manual_moon_locked();
  void drive_storm_fast_locked(int64_t now_ms);
  void arm_storm_locked(int64_t now_ms, int duration_ms);
  void apply_weather_to_target_locked(RgbwTarget& t);
  void refresh_live_state_locked(bool read_sensor);
  uint32_t next_lightning_rand_locked(uint32_t min_value, uint32_t max_value);
  void persist_if_dirty();

  ILightOutput& light_;
  IPumpOutput& pump_;
  ITemperatureSensor& temp_;
  ISettingsStore& store_;
  ITimeSource& time_;
  const IScheduleEngine& schedule_;

  mutable SemaphoreHandle_t mutex_;
  PersistedSettings settings_;
  DeviceState state_;

  bool dirty_settings_{false};
  bool guest_scene_active_{false};
  bool moon_scene_active_{false};
  int64_t feed_until_ms_{0};
  int64_t next_weather_storm_ms_{0};    // коли можна знову завести грозу за погодою
  // ── Модель грози (швидкий цикл, afterglow-затухання) ──
  int64_t lightning_until_ms_{0};       // кінець грози
  int64_t lightning_start_ms_{0};       // початок (для огинаючої наростання/затихання)
  int64_t lightning_next_strike_ms_{0}; // коли почати наступний удар
  int64_t lightning_flash_end_ms_{0};   // коли завершиться поточна видима вспышка
  int64_t lightning_next_spike_ms_{0};  // коли оновити рівень дрожання всередині вспышки
  int lightning_type_{0};               // тип вспышки: 0=стріла, 1=зірниця, 2=далекий відблиск
  float lightning_intensity_{0.F};      // пікова сила поточного удару (далекий/близький)
  float lightning_level_{0.F};          // поточна яскравість, що згасає кожен крок
  uint32_t lightning_rng_{0xA5C31F27U};
};

}  // namespace aq
