#pragma once

#include <optional>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "interfaces.hpp"

namespace aq {

class Ds18b20TemperatureSensor final : public ITemperatureSensor {
 public:
  Ds18b20TemperatureSensor();

  // Non-blocking: returns latest cached value from background task.
  std::optional<float> read_celsius() override;

 private:
  void init_bus_();
  static void sensor_task(void* arg);
  void run_sensor_loop();

  void* bus_{nullptr};
  void* sensor_{nullptr};
  bool ready_{false};

  SemaphoreHandle_t cache_mutex_{nullptr};
  std::optional<float> cached_temp_c_;
};

}  // namespace aq
