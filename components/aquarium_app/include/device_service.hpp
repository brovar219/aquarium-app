#pragma once

#include "interfaces.hpp"
#include "model.hpp"

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include <string>

namespace aq {

class DeviceService {
 public:
  DeviceService(ILightOutput& light, IPumpOutput& pump, ITemperatureSensor& temp,
                ISettingsStore& store, ITimeSource& time, const IScheduleEngine& schedule);

  void tick();

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
};

}  // namespace aq
