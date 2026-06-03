#include "wifi_station.hpp"

#include <cstring>

#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "sdkconfig.h"

static const char* TAG = "wifi_sta";
static EventGroupHandle_t s_ev;
constexpr int kConnectedBit = BIT0;
static esp_ip4_addr_t s_last_ip{};

static void on_wifi(void*, esp_event_base_t, int32_t id, void* data) {
  if (id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (id == WIFI_EVENT_STA_CONNECTED) {
    auto* ev = static_cast<wifi_event_sta_connected_t*>(data);
    ESP_LOGI(TAG, "З'єднано з AP \"%.*s\" (канал %d)", ev->ssid_len, ev->ssid, ev->channel);
  } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
    auto* ev = static_cast<wifi_event_sta_disconnected_t*>(data);
    ESP_LOGW(TAG, "Відключено від AP, причина=%d. Повторне підключення…", ev->reason);
    xEventGroupClearBits(s_ev, kConnectedBit);
    esp_wifi_connect();
  }
}

static void on_ip(void*, esp_event_base_t, int32_t id, void* data) {
  if (id == IP_EVENT_STA_GOT_IP) {
    auto* ev = static_cast<ip_event_got_ip_t*>(data);
    s_last_ip = ev->ip_info.ip;
    // Видно з консолі: монітор шукає рядок "IP=..."
    ESP_LOGI(TAG, "Wi-Fi GOT IP=" IPSTR " mask=" IPSTR " gw=" IPSTR, IP2STR(&ev->ip_info.ip),
             IP2STR(&ev->ip_info.netmask), IP2STR(&ev->ip_info.gw));
    ESP_LOGI(TAG, "Веб-панель: http://" IPSTR "/  (WS: ws://" IPSTR "/ws, OTA: POST http://" IPSTR "/update)",
             IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.ip), IP2STR(&ev->ip_info.ip));
    xEventGroupSetBits(s_ev, kConnectedBit);
  } else if (id == IP_EVENT_STA_LOST_IP) {
    ESP_LOGW(TAG, "Втрачено IP-адресу");
    xEventGroupClearBits(s_ev, kConnectedBit);
  }
}

void aq::wifi_init_sta() {
  s_ev = xEventGroupCreate();
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&icfg));

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_LOST_IP, &on_ip, nullptr));

  wifi_config_t w = {};
  strlcpy(reinterpret_cast<char*>(w.sta.ssid), CONFIG_AQUARIUM_WIFI_SSID, sizeof(w.sta.ssid));
  strlcpy(reinterpret_cast<char*>(w.sta.password), CONFIG_AQUARIUM_WIFI_PASSWORD, sizeof(w.sta.password));
  // Мінімально WPA-PSK: працює з WPA2/WPA3-Personal. Для WPA3-only ESP-IDF підбере автоматично.
  w.sta.threshold.authmode = WIFI_AUTH_WPA_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &w));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "Підключення до %s…", CONFIG_AQUARIUM_WIFI_SSID);
}

void aq::wifi_wait_connected() {
  xEventGroupWaitBits(s_ev, kConnectedBit, pdFALSE, pdTRUE, portMAX_DELAY);
  ESP_LOGI(TAG, "Wi-Fi підключено, IP=" IPSTR, IP2STR(&s_last_ip));
}
