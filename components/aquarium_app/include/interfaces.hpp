#pragma once

#include "model.hpp"

#include <optional>
#include <string>

namespace aq {

/** Керування RGBW виходом (LEDC тощо). */
class ILightOutput {
 public:
  virtual ~ILightOutput() = default;
  virtual void set_rgbw(float r, float g, float b, float w, float brightness01) = 0;
  virtual void turn_off() = 0;
};

/** Помпа (реле / транзистор). */
class IPumpOutput {
 public:
  virtual ~IPumpOutput() = default;
  virtual void set_on(bool on) = 0;
  virtual bool is_on() const = 0;
};

/** Температура води (DS18B20 тощо). */
class ITemperatureSensor {
 public:
  virtual ~ITemperatureSensor() = default;
  virtual std::optional<float> read_celsius() = 0;
};

/** Збереження налаштувань (NVS). */
class ISettingsStore {
 public:
  virtual ~ISettingsStore() = default;
  virtual bool load(PersistedSettings& out) = 0;
  virtual bool save(const PersistedSettings& in) = 0;
};

/** Джерело локального часу після SNTP. */
class ITimeSource {
 public:
  virtual ~ITimeSource() = default;
  virtual bool local_time(int& hour, int& minute, int& second) const = 0;
  virtual bool is_valid() const = 0;
  virtual bool set_timezone(const char* tz_value) = 0;
};

/** Обчислення цільового кольору для авто-режиму. */
class IScheduleEngine {
 public:
  virtual ~IScheduleEngine() = default;
  virtual bool compute(float hour_decimal, const PersistedSettings& s, bool thermal_throttle,
                       std::string& phase_out, RgbwTarget& rgbw_out) const = 0;
};

}  // namespace aq
