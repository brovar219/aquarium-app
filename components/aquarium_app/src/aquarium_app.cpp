#include "aquarium_app.hpp"

#include "device_service.hpp"
#include "esp_event.h"
#include "esp_log.h"
#include "firmware_http_update.hpp"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_time_source.hpp"
#include "http_ws_portal.hpp"
#include "mqtt_client_hub.hpp"
#include "nvs_flash.h"
#include "nvs_store.hpp"
#include "pump_gpio.hpp"
#include "rgbw_ledc.hpp"
#include "schedule_engine.hpp"
#include "temperature_null.hpp"
#include "wifi_station.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* TAG = "aquarium_app";

namespace aquarium_app {

void start() {
  ESP_ERROR_CHECK(nvs_flash_init());
  aq::ota_mark_app_valid_if_needed();
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());

  aq::wifi_init_sta();
  aq::wifi_wait_connected();

  esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
  esp_sntp_setservername(0, "pool.ntp.org");
  esp_sntp_init();

  static aq::NvsSettingsStore store;
  static aq::RgbwLedc leds;
  static aq::PumpGpio pump;
  static aq::NullTemperatureSensor temp_sensor;
  static aq::ScheduleEngine schedule;
  static aq::EspTimeSource time_source;
  static aq::DeviceService device(leds, pump, temp_sensor, store, time_source, schedule);

  aq::MqttClientHub::init(&device);

  httpd_handle_t server = nullptr;
  ESP_ERROR_CHECK(aq::HttpWsPortal::start(&server, &device));
  ESP_LOGI(TAG, "Сервіс запущено. Відкрийте http://<IP>/");

  aq::MqttClientHub::start_from_nvs();

  xTaskCreate(
      [](void* arg) {
        auto* svc = static_cast<aq::DeviceService*>(arg);
        for (;;) {
          vTaskDelay(pdMS_TO_TICKS(500));
          svc->tick();
          aq::MqttClientHub::on_device_tick();
          aq::HttpWsPortal::queue_state_broadcast();
        }
      },
      "aq_tick", 8192, &device, 5, nullptr);
}

}  // namespace aquarium_app
