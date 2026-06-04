#include "schedule_engine.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace aq {
namespace {

float lerp(float a, float b, float t) {
  t = t < 0.F ? 0.F : (t > 1.F ? 1.F : t);
  return a + (b - a) * t;
}

// ──────────────────────────────────────────────────────────────────
// FLUVAL CLASSIC  —  now uses configurable hs/he/hm
// ──────────────────────────────────────────────────────────────────
void compute_fluval(float hour, float mult, float moon,
                    float hs, float he, float hm,
                    std::string& phase, RgbwTarget& o) {
  const float rise_len  = 1.5F;
  const float set_len   = 1.0F;
  const float rise_end  = std::min(he, hs + rise_len);
  const float set_start = std::max(hs, he - set_len);
  // Нічне світло переходить північ (див. compute_plants).
  const float hm2 = (hm > he) ? hm : hm + 24.F;
  const float hr  = (hour < hs) ? hour + 24.F : hour;

  float b = 0.F, r = 0.F, g = 0.F, bl = 0.F, w = 0.F;

  if (hour >= hs && hour < he) {
    if (hour < rise_end) {
      phase = "Схід";
      float t = (hour - hs) / std::max(0.05F, rise_end - hs);
      b = mult * lerp(0.05F, 0.35F, t);
      r = 1.F; g = 0.35F; bl = 0.15F; w = 0.2F;
    } else if (hour < set_start) {
      phase = "День";
      b = mult;
      r = 1.F; g = 0.92F; bl = 0.78F; w = 1.F;
    } else {
      phase = "Захід";
      float t = (hour - set_start) / std::max(0.05F, he - set_start);
      b = mult * lerp(0.45F, 0.15F, t);
      r = 1.F; g = 0.5F; bl = 0.2F; w = 0.35F;
    }
  } else if (hr >= he && hr < hm2 && moon > 0.001F) {
    phase = "Нічне світло";
    b = moon; r = 0.04F; g = 0.08F; bl = 0.45F; w = 0.32F;
  } else {
    phase = "Ніч";
  }

  o.brightness = b; o.r = r; o.g = g; o.b = bl; o.w = w;
}

// ──────────────────────────────────────────────────────────────────
// BRIGHT DAY  —  now uses configurable hs/he/hm
// ──────────────────────────────────────────────────────────────────
void compute_bright_day(float hour, float mult, float moon,
                        float hs, float he, float hm,
                        std::string& phase, RgbwTarget& o) {
  const float rise_len  = 1.0F;
  const float set_len   = 1.5F;
  const float rise_end  = std::min(he, hs + rise_len);
  const float set_start = std::max(hs, he - set_len);
  // Нічне світло переходить північ (див. compute_plants).
  const float hm2 = (hm > he) ? hm : hm + 24.F;
  const float hr  = (hour < hs) ? hour + 24.F : hour;

  float b = 0.F, r = 0.F, g = 0.F, bl = 0.F, w = 0.F;

  if (hour >= hs && hour < he) {
    if (hour < rise_end) {
      phase = "Схід";
      float t = (hour - hs) / std::max(0.05F, rise_end - hs);
      b = mult * lerp(0.1F, 0.7F, t);
      r = 1.F; g = 0.4F; bl = 0.2F; w = 0.3F;
    } else if (hour < set_start) {
      phase = "Пік";
      b = mult; r = 1.F; g = 0.95F; bl = 0.8F; w = 1.F;
    } else {
      phase = "Захід";
      float t = (hour - set_start) / std::max(0.05F, he - set_start);
      b = mult * lerp(1.F, 0.2F, t);
      r = 1.F; g = 0.6F; bl = 0.3F; w = 0.5F;
    }
  } else if (hr >= he && hr < hm2 && moon > 0.001F) {
    phase = "Нічне світло";
    b = moon; r = 0.04F; g = 0.08F; bl = 0.45F; w = 0.32F;
  } else {
    phase = "Ніч";
  }

  o.brightness = b; o.r = r; o.g = g; o.b = bl; o.w = w;
}

// ──────────────────────────────────────────────────────────────────
// PLANTS PRO  —  unchanged, uses hs/he/hm
// ──────────────────────────────────────────────────────────────────
void compute_plants(float hour, float mult, float moon,
                    float hs, float he, float hm,
                    std::string& phase, RgbwTarget& o) {
  if (he <= hs + 2.F) he = hs + 10.F;
  // Нічне світло може переходити північ: якщо hm <= he, трактуємо hm як
  // «наступного ранку» (+24). Рання ніч (до світанку) теж рахується «після
  // опівночі», щоб місяць горів від заходу до ранкової години (напр. до 07:00).
  const float hm2 = (hm > he) ? hm : hm + 24.F;
  const float hr  = (hour < hs) ? hour + 24.F : hour;

  const float rise_len  = 40.F / 60.F;
  const float set_len   = 40.F / 60.F;
  const float rise_end  = std::min(he, hs + rise_len);
  const float set_start = std::max(hs, he - set_len);

  float b = 0.F, r = 0.F, g = 0.F, bl = 0.F, w = 0.F;

  if (hour >= hs && hour < he) {
    if (hour < rise_end) {
      phase = "Схід";
      float t = (hour - hs) / std::max(0.05F, rise_end - hs);
      // Тепле янтарне світло сходу (~2300K) → денний рослинний спектр.
      // Синій майже у нулі на старті: низьке сонце = довгі хвилі, синь розсіяна.
      // W тримаємо низьким, бо білий світлодіод холодний (6500K) і додав би
      // зайвої синяви у теплу фазу.
      b  = mult * lerp(0.04F, 1.F, t);
      r  = 1.F;
      g  = lerp(0.35F, 0.55F, t);
      bl = lerp(0.05F, 1.F,  t);
      w  = lerp(0.15F, 0.85F, t);
    } else if (hour < set_start) {
      phase = "День";
      // Рослинний буст: повний червоний (~660нм) + повний синій (~450нм),
      // приглушений зелений, холодний білий 85% — піковий ріст, рожевуватий тон.
      b = mult; r = 1.F; g = 0.55F; bl = 1.F; w = 0.85F;
    } else {
      phase = "Захід";
      float t = (hour - set_start) / std::max(0.05F, he - set_start);
      // Дзеркало сходу, але глибший червоний (захід зазвичай червоніший).
      // Синій гасне у нуль, W майже вимкнено — лишається чистий янтар.
      b  = mult * lerp(1.F, 0.06F, t);
      r  = 1.F;
      g  = lerp(0.55F, 0.30F, t);
      bl = lerp(1.F,  0.04F, t);
      w  = lerp(0.85F, 0.10F, t);
    }
  } else if (hr >= he && hr < hm2 && moon > 0.001F) {
    phase = "Нічне світло";
    // Глибоке синє «місячне срібло»; W низький, щоб не розбавляти синь у сірість.
    b = moon; r = 0.05F; g = 0.10F; bl = 0.55F; w = 0.18F;
  } else {
    phase = "Ніч";
  }

  o.brightness = b; o.r = r; o.g = g; o.b = bl; o.w = w;
}

// ──────────────────────────────────────────────────────────────────
// CUSTOM  —  user-defined colours per phase
// ──────────────────────────────────────────────────────────────────
void compute_custom(float hour, float mult, float moon,
                    float hs, float he, float hm,
                    const CustomPhases& cp,
                    std::string& phase, RgbwTarget& o) {
  if (he <= hs + 2.F) he = hs + 10.F;
  // Нічне світло переходить північ (див. compute_plants).
  const float hm2 = (hm > he) ? hm : hm + 24.F;
  const float hr  = (hour < hs) ? hour + 24.F : hour;

  const float rise_len  = 40.F / 60.F;
  const float set_len   = 40.F / 60.F;
  const float rise_end  = std::min(he, hs + rise_len);
  const float set_start = std::max(hs, he - set_len);

  float b = 0.F, r = 0.F, g = 0.F, bl = 0.F, w = 0.F;

  if (hour >= hs && hour < he) {
    if (hour < rise_end) {
      phase = "Схід";
      float t = (hour - hs) / std::max(0.05F, rise_end - hs);
      // ramp from dawn (dim) → day colours
      b = mult * lerp(0.05F, 1.F, t);
      r = lerp(cp.dawn.r * 0.4F, cp.day.r, t);
      g = lerp(cp.dawn.g * 0.3F, cp.day.g, t);
      bl= lerp(cp.dawn.b * 0.2F, cp.day.b, t);
      w = lerp(cp.dawn.w * 0.3F, cp.day.w, t);
    } else if (hour < set_start) {
      phase = "День";
      b = mult; r = cp.day.r; g = cp.day.g; bl = cp.day.b; w = cp.day.w;
    } else {
      phase = "Захід";
      float t = (hour - set_start) / std::max(0.05F, he - set_start);
      // fade day → dawn colours with dimming
      b = mult * lerp(1.F, 0.08F, t);
      r = lerp(cp.day.r, cp.dawn.r, t);
      g = lerp(cp.day.g, cp.dawn.g * 0.5F, t);
      bl= lerp(cp.day.b, cp.dawn.b * 0.3F, t);
      w = lerp(cp.day.w, cp.dawn.w * 0.2F, t);
    }
  } else if (hr >= he && hr < hm2 && moon > 0.001F) {
    phase = "Нічне світло";
    b = moon; r = cp.moon.r; g = cp.moon.g; bl = cp.moon.b; w = cp.moon.w;
  } else {
    phase = "Ніч";
  }

  o.brightness = b; o.r = r; o.g = g; o.b = bl; o.w = w;
}

}  // namespace

bool ScheduleEngine::compute(float hour, const PersistedSettings& s, bool thermal_throttle,
                             std::string& phase, RgbwTarget& out) const {
  float mult = (s.max_brightness_pct / 100.F) * (s.brightness_trim_pct / 100.F);
  if (thermal_throttle) mult *= 0.5F;
  if (s.acclimation)    mult *= 0.5F;
  mult = std::min(1.F, std::max(0.F, mult));

  float moon = std::min(30.F, std::max(0.F, s.moon_brightness_pct)) / 100.F;
  if (thermal_throttle) moon *= 0.8F;

  const float hs = s.hour_start;
  const float he = s.hour_end;
  const float hm = s.hour_moon_end;

  switch (s.light_program) {
    case LightProgram::FLUVAL_CLASSIC:
      compute_fluval(hour, mult, moon, hs, he, hm, phase, out);
      break;
    case LightProgram::BRIGHT_DAY:
      compute_bright_day(hour, mult, moon, hs, he, hm, phase, out);
      break;
    case LightProgram::CUSTOM:
      compute_custom(hour, mult, moon, hs, he, hm, s.custom_phases, phase, out);
      break;
    default:
      // PLANTS_PRO (0) and legacy 1/2
      compute_plants(hour, mult, moon, hs, he, hm, phase, out);
      break;
  }
  return true;
}

}  // namespace aq
