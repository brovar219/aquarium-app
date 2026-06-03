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
  /** Викликати з tick-задачі — обробка черги MQTT-команд і публікація стану. */
  static void on_device_tick();
  static void drain_command_queue();

  static void rebuild_topics();
};

}  // namespace aq
