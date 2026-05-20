#include "pump_gpio.hpp"

#include "esp_log.h"

static const char* TAG = "pump";

namespace aq {

PumpGpio::PumpGpio(gpio_num_t pin) : pin_(pin) {
  gpio_config_t io = {};
  io.pin_bit_mask = 1ULL << static_cast<uint32_t>(pin_);
  io.mode = GPIO_MODE_OUTPUT;
  io.pull_up_en = GPIO_PULLUP_DISABLE;
  io.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io.intr_type = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&io));
  set_on(true);
  ESP_LOGI(TAG, "Помпа на GPIO%d (активний низький)", static_cast<int>(pin_));
}

void PumpGpio::set_on(bool on) {
  on_ = on;
  // Активний низький: увімкнено = 0
  gpio_set_level(pin_, on ? 0 : 1);
}

}  // namespace aq
