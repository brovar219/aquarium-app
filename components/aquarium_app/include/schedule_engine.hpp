#pragma once

#include "interfaces.hpp"

namespace aq {

class ScheduleEngine final : public IScheduleEngine {
 public:
  bool compute(float hour, const PersistedSettings& s, bool thermal_throttle, std::string& phase_out,
               RgbwTarget& out) const override;
};

}  // namespace aq
