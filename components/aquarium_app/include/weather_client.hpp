#pragma once

#include <cstddef>

namespace aq {

/** Поточна погода з Open-Meteo (без API-ключа). */
struct WeatherData {
  bool valid{false};
  int code{0};            // WMO weather_code
  float cloud_cover{0.F}; // %
  float temp_c{0.F};
  float wind_kmh{0.F};
  bool is_day{true};
};

/** Геокодинг назви міста → координати. `resolved` отримує «Місто, CC».
 *  Потрібен активний Wi-Fi. Повертає true при успіху. */
bool weather_geocode(const char* city, float& lat, float& lon, char* resolved, size_t resolved_len);

/** Поточна погода для координат. Повертає true при успіху. */
bool weather_fetch(float lat, float lon, WeatherData& out);

/** WMO weather_code → короткий укр. опис. */
const char* weather_code_text(int code);

}  // namespace aq
