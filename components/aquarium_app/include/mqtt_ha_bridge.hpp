#pragma once

#include "mqtt_client.h"

namespace aq {

class DeviceService;

/** Топіки у форматі Home Assistant MQTT Light / Switch + auto-discovery. */
class MqttHaBridge {
 public:
  static void on_mqtt_connected(esp_mqtt_client_handle_t client, const char* topic_prefix);
  static bool on_mqtt_data(esp_mqtt_client_handle_t client, const char* topic, const char* payload,
                             int payload_len, DeviceService* dev);
  static void on_device_tick(esp_mqtt_client_handle_t client, DeviceService* dev);
};

}  // namespace aq
