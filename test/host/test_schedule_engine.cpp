// Host unit tests for aq::ScheduleEngine (pure logic, no ESP-IDF).
// Build: see test/host/CMakeLists.txt  —  runs under CI on x86 with g++.
//
// Locks in the day/sunrise/sunset/night-light behaviour, the brightness
// scaling, and the cross-midnight night-light window (regression guard for
// the fix that lets the moon run from sunset past midnight to morning).

#include <cassert>
#include <cmath>
#include <cstdio>
#include <string>

#include "schedule_engine.hpp"

using namespace aq;

static int g_checks = 0;
static int g_fails = 0;

static void check(bool cond, const char* what) {
  ++g_checks;
  if (!cond) {
    ++g_fails;
    std::printf("  FAIL: %s\n", what);
  }
}

static bool near(float a, float b, float eps = 0.005F) { return std::fabs(a - b) <= eps; }

static PersistedSettings base_settings() {
  PersistedSettings s{};  // NSDMI defaults: day 9-17, moon_end 18, day 70%, moon 4%
  s.light_program = LightProgram::PLANTS_PRO;
  return s;
}

static RgbwTarget compute_at(float hour, const PersistedSettings& s, std::string& phase,
                             bool thermal = false) {
  ScheduleEngine eng;
  RgbwTarget out{};
  eng.compute(hour, s, thermal, phase, out);
  return out;
}

int main() {
  // 1) Midday with default schedule → "День" at full configured brightness (70%).
  {
    auto s = base_settings();
    std::string ph;
    auto o = compute_at(12.0F, s, ph);
    check(ph == "День", "noon phase is День");
    check(near(o.brightness, 0.70F), "noon brightness = max_brightness 70%");
    check(near(o.r, 1.0F) && near(o.b, 1.0F), "noon plant boost R=B=1");
  }

  // 2) Deep night (03:00) with default schedule → "Ніч", lights off.
  {
    auto s = base_settings();
    std::string ph;
    auto o = compute_at(3.0F, s, ph);
    check(ph == "Ніч", "3am phase is Ніч");
    check(near(o.brightness, 0.0F), "3am brightness = 0");
  }

  // 3) Brightness scales with max_brightness_pct and thermal throttle.
  {
    auto s = base_settings();
    s.max_brightness_pct = 50.F;
    std::string ph;
    check(near(compute_at(12.0F, s, ph).brightness, 0.50F), "max 50% → noon 0.50");
    check(near(compute_at(12.0F, s, ph, true).brightness, 0.25F), "thermal halves brightness");
  }

  // 4) Sunrise window right after start hour → "Схід".
  {
    auto s = base_settings();
    std::string ph;
    compute_at(9.2F, s, ph);
    check(ph == "Схід", "09:12 phase is Схід");
  }

  // 5) Cross-midnight night light: day 9-21, moon until 07:00, 20%.
  {
    auto s = base_settings();
    s.hour_start = 9.F;
    s.hour_end = 21.F;
    s.hour_moon_end = 7.F;   // next morning → window wraps past midnight
    s.moon_brightness_pct = 20.F;

    std::string ph;
    auto evening = compute_at(23.0F, s, ph);
    check(ph == "Нічне світло", "23:00 is Нічне світло (after sunset)");
    check(near(evening.brightness, 0.20F), "23:00 moon brightness = 20%");

    auto after_midnight = compute_at(2.0F, s, ph);
    check(ph == "Нічне світло", "02:00 still Нічне світло (across midnight)");
    check(near(after_midnight.brightness, 0.20F), "02:00 moon brightness = 20%");

    auto morning = compute_at(8.0F, s, ph);
    check(ph == "Ніч", "08:00 is Ніч (past moon_end 07:00)");

    auto day = compute_at(12.0F, s, ph);
    check(ph == "День", "12:00 is День inside day window");
  }

  // 6) Moon brightness is capped at 30% (raised from the old 8% limit).
  {
    auto s = base_settings();
    s.hour_start = 9.F; s.hour_end = 21.F; s.hour_moon_end = 7.F;
    s.moon_brightness_pct = 50.F;  // over the cap
    std::string ph;
    check(near(compute_at(23.0F, s, ph).brightness, 0.30F), "moon capped at 30%");
  }

  if (g_fails == 0) {
    std::printf("test_schedule_engine: OK (%d checks)\n", g_checks);
    return 0;
  }
  std::printf("test_schedule_engine: %d/%d FAILED\n", g_fails, g_checks);
  return 1;
}
