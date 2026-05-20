#pragma once

#include <optional>

#include "interfaces.hpp"

namespace aq {

/** Заглушка датчика (повертає порожньо — драйвер DS18B20 можна додати окремо). */
class NullTemperatureSensor final : public ITemperatureSensor {
 public:
  std::optional<float> read_celsius() override { return std::nullopt; }
};

}  // namespace aq
