#include "esp_time_source.hpp"

#include <ctime>
#include <cstdlib>
#include <sys/time.h>

#include "esp_sntp.h"

namespace aq {

bool EspTimeSource::local_time(int& hour, int& minute, int& second) const {
  struct timeval tv {};
  if (gettimeofday(&tv, nullptr) != 0) {
    return false;
  }
  struct tm lt {};
  localtime_r(&tv.tv_sec, &lt);
  hour = lt.tm_hour;
  minute = lt.tm_min;
  second = lt.tm_sec;
  return true;
}

bool EspTimeSource::is_valid() const {
  // НЕ покладаємось на esp_sntp_get_sync_status(): статус SNTP_SYNC_STATUS_COMPLETED
  // у ESP-IDF тримається лише мить після синхронізації, а далі скидається в RESET,
  // тож перевірка статусу робила час «невалідним» назавжди навіть після вдалого синку
  // (пристрій застрягав на запасній годині 14:00 = вічний день, не вимикався вночі).
  // Канонічна перевірка «годинник виставлено» — рік у системному часі.
  struct timeval tv {};
  if (gettimeofday(&tv, nullptr) != 0) {
    return false;
  }
  struct tm lt {};
  localtime_r(&tv.tv_sec, &lt);
  return lt.tm_year + 1900 >= 2024;
}

bool EspTimeSource::set_timezone(const char* tz_value) {
  if (tz_value == nullptr || tz_value[0] == '\0') {
    return false;
  }
  setenv("TZ", tz_value, 1);
  tzset();
  return true;
}

}  // namespace aq
