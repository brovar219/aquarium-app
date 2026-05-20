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

static void on_wifi(void*, esp_event_base_t, int32_t id, void*) {
  if (id == WIFI_EVENT_STA_START) {
    esp_wifi_connect();
  } else if (id == WIFI_EVENT_STA_DISCONNECTED) {
    xEventGroupClearBits(s_ev, kConnectedBit);
    esp_wifi_connect();
  }
}

static void on_ip(void*, esp_event_base_t, int32_t id, void*) {
  if (id == IP_EVENT_STA_GOT_IP) {
    xEventGroupSetBits(s_ev, kConnectedBit);
  }
}

void aq::wifi_init_sta() {
  s_ev = xEventGroupCreate();
  esp_netif_create_default_wifi_sta();

  wifi_init_config_t icfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&icfg));

  ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &on_wifi, nullptr));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_ip, nullptr));

  wifi_config_t w = {};
  strlcpy(reinterpret_cast<char*>(w.sta.ssid), CONFIG_AQUARIUM_WIFI_SSID, sizeof(w.sta.ssid));
  strlcpy(reinterpret_cast<char*>(w.sta.password), CONFIG_AQUARIUM_WIFI_PASSWORD, sizeof(w.sta.password));
  w.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &w));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "Підключення до %s…", CONFIG_AQUARIUM_WIFI_SSID);
}

void aq::wifi_wait_connected() {
  xEventGroupWaitBits(s_ev, kConnectedBit, pdFALSE, pdTRUE, portMAX_DELAY);
  ESP_LOGI(TAG, "Wi-Fi підключено");
}
