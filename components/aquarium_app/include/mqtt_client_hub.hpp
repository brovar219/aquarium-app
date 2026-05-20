#pragma once

#include "mqtt_nvs.hpp"

namespace aq {

class DeviceService;

/** MQTT: підписка на {prefix}/cmd, публікація стану та відповідей. */
class MqttClientHub {
 public:
  static void init(DeviceService* dev);
  static void start_from_nvs();
  static void restart_from_nvs();
  static void stop();

  static bool is_connected();
  static void on_device_tick();

 private:
  static void rebuild_topics();
};

}  // namespace aq
