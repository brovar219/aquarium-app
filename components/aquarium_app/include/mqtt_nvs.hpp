#pragma once

#include <cstdint>

namespace aq {

/** Збережена конфігурація MQTT (окремий NVS blob, не змінює PersistedSettings). */
struct MqttNvsConfig {
  uint32_t magic{0x4D545451U};  // 'MTQ1'
  uint16_t version{1};
  bool enabled{false};
  uint16_t port{1883};
  char broker_host[96]{};
  char username[48]{};
  char password[80]{};
  char client_id[32]{};
  char topic_prefix[48]{"aquarium"};
};

bool mqtt_nvs_load(MqttNvsConfig& out);
bool mqtt_nvs_save(const MqttNvsConfig& in);

}  // namespace aq
