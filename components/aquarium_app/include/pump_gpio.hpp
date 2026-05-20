#pragma once

#include "interfaces.hpp"

#include "driver/gpio.h"

namespace aq {

class PumpGpio final : public IPumpOutput {
 public:
  explicit PumpGpio(gpio_num_t pin = GPIO_NUM_7);
  void set_on(bool on) override;
  bool is_on() const override { return on_; }

 private:
  gpio_num_t pin_;
  bool on_{true};
};

}  // namespace aq
