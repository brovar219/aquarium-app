#include "esp_time_source.hpp"

#include <ctime>
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
  const esp_sntp_sync_status_t st = esp_sntp_get_sync_status();
  if (st != SNTP_SYNC_STATUS_COMPLETED) {
    return false;
  }
  struct timeval tv {};
  if (gettimeofday(&tv, nullptr) != 0) {
    return false;
  }
  struct tm lt {};
  localtime_r(&tv.tv_sec, &lt);
  return lt.tm_year + 1900 >= 2024;
}

}  // namespace aq
