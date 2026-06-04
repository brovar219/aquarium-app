#include "weather_client.hpp"

#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"

static const char* TAG = "weather";

namespace aq {
namespace {

// GET URL по HTTPS (cert-bundle), тіло у body. Кеп 8 КБ — відповіді Open-Meteo дрібні.
bool http_get_json(const char* url, std::string& body) {
  esp_http_client_config_t cfg = {};
  cfg.url = url;
  cfg.crt_bundle_attach = esp_crt_bundle_attach;
  cfg.timeout_ms = 10000;
  cfg.user_agent = "aquarium-che/1.0";

  esp_http_client_handle_t c = esp_http_client_init(&cfg);
  if (c == nullptr) return false;

  bool ok = false;
  esp_err_t err = esp_http_client_open(c, 0);
  if (err == ESP_OK) {
    esp_http_client_fetch_headers(c);
    const int status = esp_http_client_get_status_code(c);
    if (status == 200) {
      char buf[512];
      body.clear();
      body.reserve(1024);  // відповіді Open-Meteo дрібні — одна аллокація замість серії
      int r;
      while ((r = esp_http_client_read(c, buf, sizeof(buf))) > 0) {
        body.append(buf, static_cast<size_t>(r));
        if (body.size() > 8192) break;
      }
      ok = !body.empty();
    } else {
      ESP_LOGW(TAG, "HTTP %d", status);
    }
  } else {
    ESP_LOGW(TAG, "open: %s", esp_err_to_name(err));
  }
  esp_http_client_close(c);
  esp_http_client_cleanup(c);
  return ok;
}

std::string url_encode(const char* s) {
  static const char* hex = "0123456789ABCDEF";
  std::string o;
  for (const unsigned char* p = reinterpret_cast<const unsigned char*>(s); *p != 0; ++p) {
    const unsigned char ch = *p;
    if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
      o.push_back(static_cast<char>(ch));
    } else {
      o.push_back('%');
      o.push_back(hex[ch >> 4]);
      o.push_back(hex[ch & 0x0F]);
    }
  }
  return o;
}

}  // namespace

bool weather_geocode(const char* city, float& lat, float& lon, char* resolved, size_t resolved_len) {
  if (city == nullptr || city[0] == '\0') return false;

  std::string url =
      "https://geocoding-api.open-meteo.com/v1/search?count=1&language=en&format=json&name=";
  url += url_encode(city);

  std::string body;
  if (!http_get_json(url.c_str(), body)) return false;

  cJSON* root = cJSON_Parse(body.c_str());
  if (root == nullptr) return false;

  bool ok = false;
  cJSON* arr = cJSON_GetObjectItemCaseSensitive(root, "results");
  if (cJSON_IsArray(arr) && cJSON_GetArraySize(arr) > 0) {
    cJSON* r0 = cJSON_GetArrayItem(arr, 0);
    cJSON* la = cJSON_GetObjectItemCaseSensitive(r0, "latitude");
    cJSON* lo = cJSON_GetObjectItemCaseSensitive(r0, "longitude");
    cJSON* nm = cJSON_GetObjectItemCaseSensitive(r0, "name");
    cJSON* cc = cJSON_GetObjectItemCaseSensitive(r0, "country_code");
    if (cJSON_IsNumber(la) && cJSON_IsNumber(lo)) {
      lat = static_cast<float>(la->valuedouble);
      lon = static_cast<float>(lo->valuedouble);
      if (resolved != nullptr && resolved_len > 0) {
        const char* n = cJSON_IsString(nm) ? nm->valuestring : city;
        const char* c = cJSON_IsString(cc) ? cc->valuestring : "";
        snprintf(resolved, resolved_len, "%s%s%s", n, (c[0] != '\0') ? ", " : "", c);
      }
      ok = true;
    }
  }
  cJSON_Delete(root);
  return ok;
}

bool weather_fetch(float lat, float lon, WeatherData& out) {
  char url[256];
  snprintf(url, sizeof(url),
           "https://api.open-meteo.com/v1/forecast?latitude=%.4f&longitude=%.4f"
           "&current=temperature_2m,cloud_cover,weather_code,wind_speed_10m,is_day",
           static_cast<double>(lat), static_cast<double>(lon));

  std::string body;
  if (!http_get_json(url, body)) return false;

  cJSON* root = cJSON_Parse(body.c_str());
  if (root == nullptr) return false;

  bool ok = false;
  cJSON* cur = cJSON_GetObjectItemCaseSensitive(root, "current");
  if (cJSON_IsObject(cur)) {
    cJSON* t = cJSON_GetObjectItemCaseSensitive(cur, "temperature_2m");
    cJSON* cl = cJSON_GetObjectItemCaseSensitive(cur, "cloud_cover");
    cJSON* wc = cJSON_GetObjectItemCaseSensitive(cur, "weather_code");
    cJSON* wd = cJSON_GetObjectItemCaseSensitive(cur, "wind_speed_10m");
    cJSON* idd = cJSON_GetObjectItemCaseSensitive(cur, "is_day");
    if (cJSON_IsNumber(t)) out.temp_c = static_cast<float>(t->valuedouble);
    if (cJSON_IsNumber(cl)) out.cloud_cover = static_cast<float>(cl->valuedouble);
    if (cJSON_IsNumber(wc)) out.code = static_cast<int>(wc->valuedouble);
    if (cJSON_IsNumber(wd)) out.wind_kmh = static_cast<float>(wd->valuedouble);
    if (cJSON_IsNumber(idd)) out.is_day = idd->valuedouble > 0.5;
    out.valid = true;
    ok = true;
  }
  cJSON_Delete(root);
  return ok;
}

const char* weather_code_text(int code) {
  switch (code) {
    case 0: return "Ясно";
    case 1: return "Переважно ясно";
    case 2: return "Мінлива хмарність";
    case 3: return "Похмуро";
    case 45:
    case 48: return "Туман";
    case 51:
    case 53:
    case 55: return "Мряка";
    case 56:
    case 57: return "Крижана мряка";
    case 61:
    case 63:
    case 65: return "Дощ";
    case 66:
    case 67: return "Крижаний дощ";
    case 71:
    case 73:
    case 75: return "Сніг";
    case 77: return "Сніжна крупа";
    case 80:
    case 81:
    case 82: return "Зливи";
    case 85:
    case 86: return "Снігопад";
    case 95: return "Гроза";
    case 96:
    case 99: return "Гроза з градом";
    default: return "—";
  }
}

}  // namespace aq
