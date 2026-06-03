#include "rgbw_ledc.hpp"

#include <algorithm>
#include <cmath>

#include "driver/ledc.h"
#include "esp_log.h"

static const char* TAG = "rgbw";

namespace aq {

// Порядок як у ESPHome: B=3 G=4 R=5 W=6
static constexpr int kGpio[4] = {5, 4, 3, 6};
static constexpr ledc_channel_t kCh[4] = {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2,
                                          LEDC_CHANNEL_3};

RgbwLedc::RgbwLedc() {
  // ESP32-C6 LEDC: PLL_DIV_CLK = 80 МГц. Для 13-біт (8192 кроки) частота × резолюція
  // не повинна перевищувати 80 МГц. 12 кГц × 8192 ≈ 98 МГц — недосяжно.
  // 5 кГц × 8192 ≈ 41 МГц — підходить і повністю поза чутним діапазоном.
  ledc_timer_config_t t = {};
  t.speed_mode = LEDC_LOW_SPEED_MODE;
  t.duty_resolution = LEDC_TIMER_13_BIT;
  t.timer_num = LEDC_TIMER_0;
  t.freq_hz = 5000;
  t.clk_cfg = LEDC_AUTO_CLK;
  ESP_ERROR_CHECK(ledc_timer_config(&t));

  for (int i = 0; i < 4; ++i) {
    ledc_channel_config_t ch = {};
    ch.gpio_num = kGpio[i];
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.channel = kCh[i];
    ch.timer_sel = LEDC_TIMER_0;
    ch.duty = kMaxDuty;
    ch.hpoint = 0;
    ch.flags.output_invert = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&ch));
  }
  ESP_LOGI(TAG, "LEDC RGBW ініціалізовано");
}

void RgbwLedc::set_one(int idx, float x) {
  x = std::min(1.F, std::max(0.F, x));
  const float g = std::pow(x, 2.2F);
  const uint32_t duty = static_cast<uint32_t>(g * static_cast<float>(kMaxDuty));
  ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, kCh[idx], duty));
  ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, kCh[idx]));
}

void RgbwLedc::set_rgbw(float r, float g, float b, float w, float brightness01) {
  const float br = std::min(1.F, std::max(0.F, brightness01));
  set_one(0, br * r);
  set_one(1, br * g);
  set_one(2, br * b);
  set_one(3, br * w);
}

void RgbwLedc::turn_off() {
  for (int i = 0; i < 4; ++i) {
    set_one(i, 0.F);
  }
}

}  // namespace aq
