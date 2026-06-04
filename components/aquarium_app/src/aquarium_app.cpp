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
#include "temperature_ds18b20.hpp"
#include "weather_client.hpp"
#include "wifi_station.hpp"

#include <cstring>

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

  static aq::NvsSettingsStore store;
  aq::PersistedSettings boot_settings{};
  if (!store.load(boot_settings)) {
    boot_settings = aq::PersistedSettings{};
  }
  static aq::RgbwLedc leds;
  static aq::PumpGpio pump;
  static aq::Ds18b20TemperatureSensor temp_sensor;
  static aq::ScheduleEngine schedule;
  static aq::EspTimeSource time_source;
  time_source.set_timezone(boot_settings.timezone_posix);
  esp_sntp_init();
  static aq::DeviceService device(leds, pump, temp_sensor, store, time_source, schedule);

  aq::MqttClientHub::init(&device);

  httpd_handle_t server = nullptr;
  ESP_ERROR_CHECK(aq::HttpWsPortal::start(&server, &device));
  ESP_LOGI(TAG, "Сервіс запущено. Відкрийте http://<IP>/");

  aq::MqttClientHub::start_from_nvs();

  xTaskCreate(
      [](void* arg) {
        auto* svc = static_cast<aq::DeviceService*>(arg);
        unsigned tick_n = 0;
        for (;;) {
          vTaskDelay(pdMS_TO_TICKS(100));
          svc->tick();
          aq::MqttClientHub::drain_command_queue();
          // MQTT/WS достатньо оновлювати раз на ~1 с, а сам світ рахуємо частіше для швидкої реакції.
          if ((++tick_n % 10) == 0) {
            aq::MqttClientHub::on_device_tick();
            aq::HttpWsPortal::queue_state_broadcast();
          }
        }
      },
      "aq_tick", 8192, &device, 5, nullptr);

  // Окрема швидка задача грози: для природного мерехтіння блискавки потрібні
  // спалахи 20–50 мс, що недосяжно при тіку 100 мс. Поки гроза активна —
  // каденція 15 мс; у спокої задача майже спить (150 мс). Пріоритет вищий за
  // aq_tick, щоб дрож не «плив». Світлом під час грози керує лише ця задача.
  xTaskCreate(
      [](void* arg) {
        auto* svc = static_cast<aq::DeviceService*>(arg);
        for (;;) {
          const bool active = svc->storm_task_step();
          vTaskDelay(pdMS_TO_TICKS(active ? 8 : 150));
        }
      },
      "aq_storm", 4096, &device, 6, nullptr);

  // Фонова задача погоди (Open-Meteo, без API-ключа). Геокодимо місто у
  // координати (тільки коли місто змінилось), далі тягнемо поточну погоду.
  // Успіх → опитуємо раз на 20 хв; помилка/немає міста → ретрай через 60 с.
  xTaskCreate(
      [](void* arg) {
        auto* svc = static_cast<aq::DeviceService*>(arg);
        char city[48] = {0};
        char last_city[48] = {0};
        char resolved[48] = {0};
        float lat = 0.F, lon = 0.F;
        bool have_loc = false;
        for (;;) {
          bool ok = false;
          svc->weather_get_city(city, sizeof(city));
          if (city[0] != '\0') {
            if (strcmp(city, last_city) != 0) {
              have_loc = aq::weather_geocode(city, lat, lon, resolved, sizeof(resolved));
              if (have_loc) {
                strncpy(last_city, city, sizeof(last_city) - 1);
                last_city[sizeof(last_city) - 1] = '\0';
              }
            }
            if (have_loc) {
              aq::WeatherData wd;
              if (aq::weather_fetch(lat, lon, wd)) {
                svc->weather_set_result(wd, resolved);
                ok = true;
              }
            }
          }
          vTaskDelay(pdMS_TO_TICKS(ok ? (20 * 60 * 1000) : 60 * 1000));
        }
      },
      "aq_weather", 8192, &device, 4, nullptr);
}

}  // namespace aquarium_app
