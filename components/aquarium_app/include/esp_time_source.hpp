#pragma once

#include "interfaces.hpp"

namespace aq {

class EspTimeSource final : public ITimeSource {
 public:
  bool local_time(int& hour, int& minute, int& second) const override;
  bool is_valid() const override;
  bool set_timezone(const char* tz_value) override;
};

}  // namespace aq
