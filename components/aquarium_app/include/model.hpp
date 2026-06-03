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
  // 1, 2 were GROW_ONLY / VIEW_ONLY — kept as numeric values for NVS compat,
  // schedule engine maps them to PLANTS_PRO.
  FLUVAL_CLASSIC = 3,
  BRIGHT_DAY = 4,
  CUSTOM = 5,
};

struct RgbwTarget {
  float brightness{0.F};
  float r{0.F};
  float g{0.F};
  float b{0.F};
  float w{0.F};
};

/** RGBW colour for one time-of-day phase (0..1 each). */
struct PhaseColors {
  float r{1.F};
  float g{1.F};
  float b{1.F};
  float w{1.F};
};

/** User-editable colours for the CUSTOM light program. */
struct CustomPhases {
  // Peak-of-sunrise and sunset colour (warm, red-orange)
  PhaseColors dawn{1.0F, 0.42F, 0.18F, 0.28F};
  // Full midday colour
  PhaseColors day{1.0F, 0.92F, 0.78F, 1.0F};
  // Very dim moonlight
  PhaseColors moon{0.04F, 0.08F, 0.45F, 0.32F};
};

/** One saved manual preset slot (P1–P4). */
struct ManualPreset {
  float r{1.F};
  float g{1.F};
  float b{1.F};
  float w{1.F};
  float brightness{1.F};
  bool valid{false};
  uint8_t _pad[3]{};
};

struct PersistedSettings {
  uint32_t magic{0xA01A01U};
  uint16_t version{5};

  OperationMode operation_mode{OperationMode::AUTO_24H};
  LightProgram light_program{LightProgram::PLANTS_PRO};

  float max_brightness_pct{70.F};
  float brightness_trim_pct{100.F};
  float moon_brightness_pct{4.F};

  float hour_start{9.F};
  float hour_end{17.F};
  float hour_moon_end{18.F};

  bool acclimation{false};
  bool pump_on{true};

  char timezone_country[16]{"ua"};
  char timezone_posix[64]{"EET-2EEST,M3.5.0/3,M10.5.0/4"};
  SceneMode resume_scene_mode{SceneMode::AUTO};

  float manual_r{1.F};
  float manual_g{1.F};
  float manual_b{1.F};
  float manual_w{1.F};
  float manual_brightness{1.F};

  // v5 additions — zero-initialised when loading older NVS blobs
  CustomPhases custom_phases{};
  ManualPreset presets[4]{};
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
