#include "mqtt_ha_bridge.hpp"

#include "api_dispatch.hpp"
#include "device_service.hpp"
#include "model.hpp"

#include <cstdio>
#include <cstring>
#include <string>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_wifi.h"

static const char* TAG = "mqtt_ha";

namespace aq {

namespace {

char s_prefix[48]{"aquarium"};
char s_uid[32]{};
char s_light_set[96]{};
char s_light_state[96]{};
char s_pump_set[96]{};
char s_pump_state[96]{};
char s_disc_light[128]{};
char s_disc_pump[128]{};

void build_topic(char* out, size_t out_sz, const char* suffix) {
  snprintf(out, out_sz, "%s/%s", s_prefix, suffix);
}

void build_unique_id() {
  uint8_t mac[6]{};
  if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
    snprintf(s_uid, sizeof(s_uid), "aquarium_che_%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
  } else {
    snprintf(s_uid, sizeof(s_uid), "aquarium_che");
  }
}

void publish_discovery(esp_mqtt_client_handle_t client) {
  snprintf(s_disc_light, sizeof(s_disc_light), "homeassistant/light/%s/config", s_uid);
  snprintf(s_disc_pump, sizeof(s_disc_pump), "homeassistant/switch/%s_pump/config", s_uid);

  char light_cfg[640]{};
  snprintf(light_cfg, sizeof(light_cfg),
           "{"
           "\"name\":\"Aquarium Light\","
           "\"unique_id\":\"%s\","
           "\"command_topic\":\"%s\","
           "\"state_topic\":\"%s\","
           "\"schema\":\"json\","
           "\"brightness\":true,"
           "\"brightness_scale\":255,"
           "\"payload_on\":\"ON\","
           "\"payload_off\":\"OFF\","
           "\"state_on\":\"ON\","
           "\"state_off\":\"OFF\","
           "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"Aquarium CHE\",\"model\":\"ESP32-C6\"}"
           "}",
           s_uid, s_light_set, s_light_state, s_uid);

  esp_mqtt_client_publish(client, s_disc_light, light_cfg, static_cast<int>(strlen(light_cfg)), 1, 1);
  ESP_LOGI(TAG, "HA discovery: %s", s_disc_light);

  char pump_cfg[512]{};
  snprintf(pump_cfg, sizeof(pump_cfg),
           "{"
           "\"name\":\"Aquarium Pump\","
           "\"unique_id\":\"%s_pump\","
           "\"command_topic\":\"%s\","
           "\"state_topic\":\"%s\","
           "\"payload_on\":\"ON\","
           "\"payload_off\":\"OFF\","
           "\"state_on\":\"ON\","
           "\"state_off\":\"OFF\","
           "\"device\":{\"identifiers\":[\"%s\"],\"name\":\"Aquarium CHE\"}"
           "}",
           s_uid, s_pump_set, s_pump_state, s_uid);

  esp_mqtt_client_publish(client, s_disc_pump, pump_cfg, static_cast<int>(strlen(pump_cfg)), 1, 1);
  ESP_LOGI(TAG, "HA discovery: %s", s_disc_pump);
}

std::string dispatch_cmd(DeviceService* dev, const char* name, const char* extra_json) {
  char buf[256]{};
  if (extra_json && extra_json[0]) {
    snprintf(buf, sizeof(buf), R"({"type":"cmd","name":"%s",%s})", name, extra_json);
  } else {
    snprintf(buf, sizeof(buf), R"({"type":"cmd","name":"%s"})", name);
  }
  return dispatch_ipc_json(buf, dev);
}

void handle_light_set(const char* payload, int len, DeviceService* dev) {
  bool on = false;
  float brightness01 = 1.F;

  if (len == 2 && (memcmp(payload, "ON", 2) == 0 || memcmp(payload, "on", 2) == 0)) {
    on = true;
  } else if (len == 3 && (memcmp(payload, "OFF", 3) == 0 || memcmp(payload, "off", 3) == 0)) {
    on = false;
  } else {
    std::string js(payload, static_cast<size_t>(len));
    cJSON* root = cJSON_Parse(js.c_str());
    if (root) {
      cJSON* st = cJSON_GetObjectItemCaseSensitive(root, "state");
      if (cJSON_IsString(st) && st->valuestring) {
        on = (strcmp(st->valuestring, "ON") == 0 || strcmp(st->valuestring, "on") == 0);
      }
      cJSON* br = cJSON_GetObjectItemCaseSensitive(root, "brightness");
      if (cJSON_IsNumber(br)) {
        brightness01 = static_cast<float>(br->valuedouble) / 255.F;
        if (brightness01 < 0.01F) {
          on = false;
        }
      }
      cJSON_Delete(root);
    }
  }

  if (!on) {
    dispatch_cmd(dev, "set_operation_mode", R"("value":"manual")");
    dispatch_cmd(dev, "set_manual_rgbw",
                 R"("r":0,"g":0,"b":0,"w":0,"brightness":0)");
    ESP_LOGI(TAG, "HA light OFF");
    return;
  }

  dispatch_cmd(dev, "set_operation_mode", R"("value":"manual")");
  char extra[128]{};
  snprintf(extra, sizeof(extra), R"("r":1,"g":1,"b":1,"w":1,"brightness":%.4f)", brightness01);
  dispatch_cmd(dev, "set_manual_rgbw", extra);
  ESP_LOGI(TAG, "HA light ON brightness=%.2f", brightness01);
}

void handle_pump_set(const char* payload, int len, DeviceService* dev) {
  bool on = false;
  if (len >= 2 && (memcmp(payload, "ON", 2) == 0 || memcmp(payload, "on", 2) == 0)) {
    on = true;
  } else if (len >= 3 && (memcmp(payload, "OFF", 3) == 0 || memcmp(payload, "off", 3) == 0)) {
    on = false;
  } else {
    std::string js(payload, static_cast<size_t>(len));
    cJSON* root = cJSON_Parse(js.c_str());
    if (root) {
      cJSON* st = cJSON_GetObjectItemCaseSensitive(root, "state");
      if (cJSON_IsString(st) && st->valuestring) {
        on = (strcmp(st->valuestring, "ON") == 0);
      }
      cJSON_Delete(root);
    }
  }
  char extra[24]{};
  snprintf(extra, sizeof(extra), on ? R"("on":true)" : R"("on":false)");
  dispatch_cmd(dev, "set_pump", extra);
  ESP_LOGI(TAG, "HA pump %s", on ? "ON" : "OFF");
}

}  // namespace

void MqttHaBridge::on_mqtt_connected(esp_mqtt_client_handle_t client, const char* topic_prefix) {
  if (client == nullptr || topic_prefix == nullptr || topic_prefix[0] == '\0') {
    return;
  }
  strncpy(s_prefix, topic_prefix, sizeof(s_prefix) - 1);
  build_unique_id();
  build_topic(s_light_set, sizeof(s_light_set), "light/set");
  build_topic(s_light_state, sizeof(s_light_state), "light/state");
  build_topic(s_pump_set, sizeof(s_pump_set), "pump/set");
  build_topic(s_pump_state, sizeof(s_pump_state), "pump/state");

  esp_mqtt_client_subscribe(client, s_light_set, 1);
  esp_mqtt_client_subscribe(client, s_pump_set, 1);
  ESP_LOGI(TAG, "HA subscribe %s, %s", s_light_set, s_pump_set);

  publish_discovery(client);
}

bool MqttHaBridge::on_mqtt_data(esp_mqtt_client_handle_t client, const char* topic, const char* payload,
                                int payload_len, DeviceService* dev) {
  (void)client;
  if (dev == nullptr || topic == nullptr || payload == nullptr || payload_len <= 0) {
    return false;
  }
  if (strcmp(topic, s_light_set) == 0) {
    handle_light_set(payload, payload_len, dev);
    return true;
  }
  if (strcmp(topic, s_pump_set) == 0) {
    handle_pump_set(payload, payload_len, dev);
    return true;
  }
  return false;
}

void MqttHaBridge::on_device_tick(esp_mqtt_client_handle_t client, DeviceService* dev) {
  if (client == nullptr || dev == nullptr || s_light_state[0] == '\0') {
    return;
  }
  const DeviceState st = dev->snapshot();
  const bool on = st.out_brightness > 0.02F;
  const int br = on ? static_cast<int>(st.out_brightness * 255.F + 0.5F) : 0;

  char state_json[96]{};
  snprintf(state_json, sizeof(state_json), "{\"state\":\"%s\",\"brightness\":%d}", on ? "ON" : "OFF", br);
  esp_mqtt_client_publish(client, s_light_state, state_json, static_cast<int>(strlen(state_json)), 1, 1);

  const char* pump_st = st.pump_on ? "ON" : "OFF";
  esp_mqtt_client_publish(client, s_pump_state, pump_st, static_cast<int>(strlen(pump_st)), 1, 1);
}

}  // namespace aq
