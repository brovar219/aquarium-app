#include "temperature_ds18b20.hpp"

#include "ds18b20.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/task.h"
#include "onewire_bus.h"

namespace aq {

namespace {
constexpr gpio_num_t kDs18b20Gpio = GPIO_NUM_0;
static const char* TAG = "temp_ds18b20";
}  // namespace

void Ds18b20TemperatureSensor::init_bus_() {
  onewire_bus_handle_t bus = nullptr;
  onewire_bus_config_t bus_config = {
      .bus_gpio_num = kDs18b20Gpio,
      .flags = {.en_pull_up = true},
  };
  onewire_bus_rmt_config_t rmt_config = {.max_rx_bytes = 10};
  if (onewire_new_bus_rmt(&bus_config, &rmt_config, &bus) != ESP_OK) {
    ESP_LOGE(TAG, "1-Wire init failed on GPIO%d", static_cast<int>(kDs18b20Gpio));
    return;
  }

  onewire_device_iter_handle_t iter = nullptr;
  if (onewire_new_device_iter(bus, &iter) != ESP_OK) {
    ESP_LOGE(TAG, "device iter failed");
    return;
  }

  onewire_device_t next_device{};
  ds18b20_device_handle_t sensor = nullptr;
  esp_err_t search = ESP_OK;
  do {
    search = onewire_device_iter_get_next(iter, &next_device);
    if (search == ESP_OK) {
      ds18b20_config_t cfg = {};
      if (ds18b20_new_device_from_enumeration(&next_device, &cfg, &sensor) == ESP_OK) {
        ds18b20_set_resolution(sensor, DS18B20_RESOLUTION_12B);
        bus_ = bus;
        sensor_ = sensor;
        ready_ = true;
        ESP_LOGI(TAG, "DS18B20 found on GPIO%d", static_cast<int>(kDs18b20Gpio));
        break;
      }
    }
  } while (search != ESP_ERR_NOT_FOUND);
  onewire_del_device_iter(iter);

  if (!ready_) {
    ESP_LOGW(TAG, "DS18B20 not found on GPIO%d", static_cast<int>(kDs18b20Gpio));
  }
}

void Ds18b20TemperatureSensor::sensor_task(void* arg) {
  static_cast<Ds18b20TemperatureSensor*>(arg)->run_sensor_loop();
}

void Ds18b20TemperatureSensor::run_sensor_loop() {
  // First read after 2s startup delay, then every 5s.
  vTaskDelay(pdMS_TO_TICKS(2000));
  for (;;) {
    if (ready_ && sensor_ != nullptr) {
      auto* sensor = static_cast<ds18b20_device_handle_t>(sensor_);
      float temp_c = 0.F;
      // Both calls block ~750ms total (12-bit conversion). Fine here — no mutex held.
      if (ds18b20_trigger_temperature_conversion(sensor) == ESP_OK &&
          ds18b20_get_temperature(sensor, &temp_c) == ESP_OK) {
        if (xSemaphoreTake(cache_mutex_, pdMS_TO_TICKS(50)) == pdTRUE) {
          cached_temp_c_ = temp_c;
          xSemaphoreGive(cache_mutex_);
        }
      } else {
        ESP_LOGW(TAG, "read failed");
      }
    }
    vTaskDelay(pdMS_TO_TICKS(5000));
  }
}

Ds18b20TemperatureSensor::Ds18b20TemperatureSensor() {
  cache_mutex_ = xSemaphoreCreateMutex();
  init_bus_();
  xTaskCreate(sensor_task, "ds18b20", 6144, this, 3, nullptr);
}

std::optional<float> Ds18b20TemperatureSensor::read_celsius() {
  if (cache_mutex_ == nullptr) return std::nullopt;
  if (xSemaphoreTake(cache_mutex_, pdMS_TO_TICKS(10)) != pdTRUE) return cached_temp_c_;
  auto val = cached_temp_c_;
  xSemaphoreGive(cache_mutex_);
  return val;
}

}  // namespace aq
