#include "schedule_engine.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace aq {
namespace {

float lerp(float a, float b, float t) {
  if (t < 0.F) {
    t = 0.F;
  }
  if (t > 1.F) {
    t = 1.F;
  }
  return a + (b - a) * t;
}

void compute_fluval(float hour, float mult, float moon, std::string& phase, RgbwTarget& o) {
  float b = 0.F;
  float r = 0.F;
  float g = 0.F;
  float bl = 0.F;
  float w = 0.F;
  if (hour < 7.F) {
    phase = "Ніч";
    b = 0.F;
  } else if (hour < 8.5F) {
    phase = "Схід";
    float t = (hour - 7.F) / 1.5F;
    b = mult * lerp(0.05F, 0.35F, t);
    r = 1.F;
    g = 0.35F;
    bl = 0.15F;
    w = 0.2F;
  } else if (hour < 11.F) {
    phase = "Ранок";
    float t = (hour - 8.5F) / 2.5F;
    b = mult * lerp(0.35F, 0.85F, t);
    r = 1.F;
    g = 0.88F;
    bl = 0.7F;
    w = 0.85F;
  } else if (hour < 14.F) {
    phase = "Пік (полудень)";
    b = mult;
    r = 1.F;
    g = 0.92F;
    bl = 0.78F;
    w = 1.F;
  } else if (hour < 17.F) {
    phase = "День";
    b = mult * 0.88F;
    r = 1.F;
    g = 0.9F;
    bl = 0.75F;
    w = 0.95F;
  } else if (hour < 19.5F) {
    phase = "Вечір";
    float t = (hour - 17.F) / 2.5F;
    b = mult * lerp(0.88F, 0.45F, t);
    r = 1.F;
    g = 0.8F;
    bl = 0.55F;
    w = 0.7F;
  } else if (hour < 20.5F) {
    phase = "Захід";
    float t = (hour - 19.5F) / 1.F;
    b = mult * lerp(0.45F, 0.15F, t);
    r = 1.F;
    g = 0.5F;
    bl = 0.2F;
    w = 0.35F;
  } else if (hour < 23.F) {
    phase = "Місяць";
    b = moon;
    r = 0.F;
    g = 0.05F;
    bl = 1.F;
    w = 0.F;
  } else {
    phase = "Ніч";
    b = 0.F;
  }
  o.brightness = b;
  o.r = r;
  o.g = g;
  o.b = bl;
  o.w = w;
}

void compute_bright_day(float hour, float mult, float moon, std::string& phase, RgbwTarget& o) {
  float b = 0.F;
  float r = 0.F;
  float g = 0.F;
  float bl = 0.F;
  float w = 0.F;
  if (hour < 10.F || hour >= 22.F) {
    phase = "Ніч";
    b = 0.F;
  } else if (hour < 11.F) {
    phase = "Схід";
    float t = hour - 10.F;
    b = mult * lerp(0.1F, 0.7F, t);
    r = 1.F;
    g = 0.4F;
    bl = 0.2F;
    w = 0.3F;
  } else if (hour < 17.F) {
    phase = "Пік";
    b = mult;
    r = 1.F;
    g = 0.95F;
    bl = 0.8F;
    w = 1.F;
  } else if (hour < 18.5F) {
    phase = "Захід";
    float t = (hour - 17.F) / 1.5F;
    b = mult * lerp(1.F, 0.2F, t);
    r = 1.F;
    g = 0.6F;
    bl = 0.3F;
    w = 0.5F;
  } else {
    phase = "Місяць";
    b = moon;
    r = 0.F;
    g = 0.05F;
    bl = 1.F;
    w = 0.F;
  }
  o.brightness = b;
  o.r = r;
  o.g = g;
  o.b = bl;
  o.w = w;
}

void compute_plants(float hour, float mult, float moon, float hs, float he, float hm, bool grow_mode,
                    bool view_mode, std::string& phase, RgbwTarget& o) {
  if (he <= hs + 2.F) {
    he = hs + 10.F;
  }
  if (hm <= he) {
    hm = he + 2.F;
  }
  if (hm > 24.F) {
    hm = 24.F;
  }

  float day_len = he - hs;
  float dawn_end = hs + 1.F;
  float peak1 = hs + day_len * 0.30F;
  float siesta_b = hs + day_len * 0.42F;
  float siesta_e = siesta_b + 1.F;
  float peak2 = hs + day_len * 0.58F;
  float eve_b = he - 1.5F;
  float sunset_b = he - 0.5F;

  float b = 0.F;
  float r = 0.F;
  float g = 0.F;
  float bl = 0.F;
  float w = 0.F;

  if (hour < hs || hour >= hm) {
    phase = "Ніч";
    b = 0.F;
  } else if (hour < dawn_end) {
    phase = "Схід";
    float t = (hour - hs) / (dawn_end - hs);
    b = mult * lerp(0.12F, 0.55F, t);
    r = lerp(0.85F, 0.55F, t);
    g = 0.22F;
    bl = lerp(0.08F, 0.45F, t);
    w = lerp(0.95F, 0.75F, t);
  } else if (hour < peak1) {
    phase = "Ранок";
    float t = (hour - dawn_end) / (peak1 - dawn_end);
    b = mult * lerp(0.55F, 0.92F, t);
    r = 0.92F;
    g = 0.78F;
    bl = 0.82F;
    w = 1.F;
  } else if (hour < siesta_b) {
    phase = "Пік";
    b = mult;
    r = 1.F;
    g = 0.82F;
    bl = 0.95F;
    w = 1.F;
  } else if (hour < siesta_e) {
    phase = "Обід";
    b = mult * 0.48F;
    r = 0.65F;
    g = 1.F;
    bl = 0.45F;
    w = 0.5F;
  } else if (hour < peak2) {
    phase = "День";
    b = mult * 0.9F;
    r = 0.95F;
    g = 0.8F;
    bl = 0.85F;
    w = 0.95F;
  } else if (hour < eve_b) {
    phase = "Вечір";
    b = mult * 0.82F;
    r = 0.98F;
    g = 0.72F;
    bl = 0.55F;
    w = 0.75F;
  } else if (hour < sunset_b) {
    phase = "Захід";
    float t = (hour - eve_b) / (sunset_b - eve_b);
    b = mult * lerp(0.82F, 0.5F, t);
    r = 1.F;
    g = lerp(0.72F, 0.58F, t);
    bl = lerp(0.35F, 0.12F, t);
    w = lerp(0.6F, 0.2F, t);
  } else if (hour < he) {
    phase = "Захід";
    float t = (hour - sunset_b) / (he - sunset_b);
    b = mult * lerp(0.5F, 0.35F, t);
    r = 1.F;
    g = 0.55F;
    bl = 0.08F;
    w = 0.15F;
  } else {
    phase = "Місяць";
    b = moon;
    r = 0.03F;
    g = 0.05F;
    bl = 1.F;
    w = 0.F;
  }

  if (grow_mode && b > 0.01F) {
    r = std::min(1.F, r * 1.12F);
    g *= 0.72F;
    bl = std::min(1.F, bl * 1.08F);
  }
  if (view_mode && b > 0.01F) {
    g = std::min(1.F, g * 1.18F);
    r *= 0.88F;
    bl *= 0.9F;
  }

  o.brightness = b;
  o.r = r;
  o.g = g;
  o.b = bl;
  o.w = w;
}

}  // namespace

bool ScheduleEngine::compute(float hour, const PersistedSettings& s, bool thermal_throttle,
                             std::string& phase, RgbwTarget& out) const {
  float mult = (s.max_brightness_pct / 100.F) * (s.brightness_trim_pct / 100.F);
  if (thermal_throttle) {
    mult *= 0.5F;
  }
  if (s.acclimation) {
    mult *= 0.5F;
  }
  mult = std::min(1.F, std::max(0.02F, mult));
  float moon_pct = s.moon_brightness_pct;
  if (moon_pct < 5.F) {
    moon_pct = 18.F;
  }
  float moon = std::min(0.4F, moon_pct / 100.F) * mult;

  switch (s.light_program) {
    case LightProgram::FLUVAL_CLASSIC:
      compute_fluval(hour, mult, moon, phase, out);
      break;
    case LightProgram::BRIGHT_DAY:
      compute_bright_day(hour, mult, moon, phase, out);
      break;
    case LightProgram::PLANTS_PRO:
      compute_plants(hour, mult, moon, s.hour_start, s.hour_end, s.hour_moon_end, false, false, phase,
                     out);
      break;
    case LightProgram::GROW_ONLY:
      compute_plants(hour, mult, moon, s.hour_start, s.hour_end, s.hour_moon_end, true, false, phase,
                     out);
      break;
    case LightProgram::VIEW_ONLY:
      compute_plants(hour, mult, moon, s.hour_start, s.hour_end, s.hour_moon_end, false, true, phase,
                     out);
      break;
    default:
      compute_plants(hour, mult, moon, s.hour_start, s.hour_end, s.hour_moon_end, false, false, phase,
                     out);
      break;
  }
  return true;
}

}  // namespace aq
