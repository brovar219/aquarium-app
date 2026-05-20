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

  RgbwTarget manual_{1.F, 1.F, 1.F, 1.F, 1.F};

  bool dirty_settings_{false};
};

}  // namespace aq
