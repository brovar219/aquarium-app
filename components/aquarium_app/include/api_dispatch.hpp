#pragma once

#include <string>

namespace aq {

class DeviceService;

/** Обробка JSON-команд для WebSocket і MQTT (get_settings, set_settings, set_mqtt_config + device). */
std::string dispatch_ipc_json(const char* json, DeviceService* dev);

}  // namespace aq
