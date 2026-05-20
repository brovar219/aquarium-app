#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace aq {

enum class OperationMode : uint8_t { AUTO_24H, MANUAL };

enum class SceneMode : uint8_t {
  AUTO = 0,
  MANUAL = 1,
  STORM = 2,
  FEED = 3,
  MOON_TEMP = 4,
  DEMO = 5,
  SHOW_GUESTS = 6,
};

enum class LightProgram : uint8_t {
  PLANTS_PRO = 0,
  GROW_ONLY = 1,
  VIEW_ONLY = 2,
  FLUVAL_CLASSIC = 3,
  BRIGHT_DAY = 4,
};

struct RgbwTarget {
  float brightness{0.F};  // 0..1 загальна яскравість (як у ESPHome apply_rgbw)
  float r{0.F};
  float g{0.F};
  float b{0.F};
  float w{0.F};
};

struct PersistedSettings {
  uint32_t magic{0xA01A01U};
  uint16_t version{3};

  OperationMode operation_mode{OperationMode::AUTO_24H};
  LightProgram light_program{LightProgram::PLANTS_PRO};

  float max_brightness_pct{100.F};
  float brightness_trim_pct{100.F};
  float moon_brightness_pct{18.F};

  float hour_start{8.F};
  float hour_end{20.F};
  float hour_moon_end{22.F};

  bool acclimation{false};
  bool pump_on{true};

  /** Ручний RGBW (0..1) + яскравість; зберігається в NVS. */
  float manual_r{1.F};
  float manual_g{1.F};
  float manual_b{1.F};
  float manual_w{1.F};
  float manual_brightness{1.F};
};

struct DeviceState {
  OperationMode operation_mode{OperationMode::AUTO_24H};
  SceneMode scene_mode{SceneMode::AUTO};
  LightProgram light_program{LightProgram::PLANTS_PRO};

  std::string phase_label;

  float out_r{0.F};
  float out_g{0.F};
  float out_b{0.F};
  float out_w{0.F};
  float out_brightness{0.F};

  bool pump_on{true};
  bool acclimation{false};
  bool thermal_throttle{false};

  std::optional<float> water_temp_c;

  bool time_valid{false};
  int time_h{0};
  int time_m{0};
  int time_s{0};

  int wifi_rssi{0};
  int64_t uptime_ms{0};
};

std::string light_program_to_api(LightProgram p);
bool light_program_from_api(const char* s, LightProgram& out);

std::string operation_mode_to_api(OperationMode m);
bool operation_mode_from_api(const char* s, OperationMode& out);

}  // namespace aq
