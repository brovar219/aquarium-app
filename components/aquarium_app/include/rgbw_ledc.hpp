#pragma once

#include "interfaces.hpp"

namespace aq {

/** RGBW через LEDC (інвертована яскравість на виході, γ≈2.2). */
class RgbwLedc final : public ILightOutput {
 public:
  RgbwLedc();
  void set_rgbw(float r, float g, float b, float w, float brightness01) override;
  void turn_off() override;

 private:
  void set_one(int idx, float level01_gamma_input);
  static constexpr int kPwmBits = 13;
  static constexpr uint32_t kMaxDuty = (1U << kPwmBits) - 1;
};

}  // namespace aq
