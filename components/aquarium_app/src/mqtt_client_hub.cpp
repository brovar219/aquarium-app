#include "mqtt_client_hub.hpp"

#include "api_dispatch.hpp"
#include "device_service.hpp"
#include "json_state.hpp"
#include "mqtt_nvs.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "mqtt_client.h"

static const char* TAG = "mqtt_hub";

namespace aq {

namespace {

DeviceService* s_dev{nullptr};
esp_mqtt_client_handle_t s_mqtt{nullptr};
MqttNvsConfig s_cfg{};
bool s_mqtt_connected{false};
int64_t s_last_state_publish_us{0};
constexpr int64_t kStatePublishIntervalUs{4'000'000};  // 4 s

char s_uri[160]{};
char s_cmd_topic[128]{};
char s_state_topic[128]{};
char s_reply_topic[128]{};
char s_lwt_topic[128]{};

void rebuild_topics() {
  s_cmd_topic[0] = s_state_topic[0] = s_reply_topic[0] = s_lwt_topic[0] = '\0';
  const char* p = s_cfg.topic_prefix[0] ? s_cfg.topic_prefix : "aquarium";
  snprintf(s_cmd_topic, sizeof(s_cmd_topic), "%s/cmd", p);
  snprintf(s_state_topic, sizeof(s_state_topic), "%s/state", p);
  snprintf(s_reply_topic, sizeof(s_reply_topic), "%s/reply", p);
  snprintf(s_lwt_topic, sizeof(s_lwt_topic), "%s/status", p);
}

void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
  (void)handler_args;
  (void)base;
  auto* ev = static_cast<esp_mqtt_event_t*>(event_data);
  esp_mqtt_client_handle_t client = ev->client;

  switch (static_cast<esp_mqtt_event_id_t>(event_id)) {
    case MQTT_EVENT_CONNECTED: {
      s_mqtt_connected = true;
      ESP_LOGI(TAG, "MQTT connected");
      rebuild_topics();
      if (s_cmd_topic[0]) {
        const int mid = esp_mqtt_client_subscribe(client, s_cmd_topic, 1);
        ESP_LOGI(TAG, "subscribe %s mid=%d", s_cmd_topic, mid);
      }
      if (s_lwt_topic[0]) {
        const char online[] = "online";
        esp_mqtt_client_publish(client, s_lwt_topic, online, static_cast<int>(strlen(online)), 1, 1);
      }
      break;
    }
    case MQTT_EVENT_DISCONNECTED:
      s_mqtt_connected = false;
      ESP_LOGW(TAG, "MQTT disconnected");
      break;
    case MQTT_EVENT_DATA: {
      if (s_dev == nullptr || ev->topic_len <= 0 || ev->data_len <= 0) {
        break;
      }
      char topic[128]{};
      const size_t tl = static_cast<size_t>(ev->topic_len);
      if (tl >= sizeof(topic)) {
        break;
      }
      memcpy(topic, ev->topic, tl);
      topic[tl] = '\0';

      rebuild_topics();
      if (strcmp(topic, s_cmd_topic) != 0) {
        break;
      }

      std::string payload(static_cast<size_t>(ev->data_len), '\0');
      memcpy(payload.data(), ev->data, static_cast<size_t>(ev->data_len));

      const std::string reply = dispatch_ipc_json(payload.c_str(), s_dev);
      if (s_reply_topic[0] && !reply.empty()) {
        esp_mqtt_client_publish(client, s_reply_topic, reply.data(), reply.size(), 1, 0);
      }
      break;
    }
    default:
      break;
  }
}

bool build_uri_from_cfg() {
  if (s_cfg.broker_host[0] == '\0') {
    return false;
  }
  if (s_cfg.port == 0) {
    s_cfg.port = 1883;
  }
  snprintf(s_uri, sizeof(s_uri), "mqtt://%s:%u", s_cfg.broker_host, static_cast<unsigned>(s_cfg.port));
  return true;
}

}  // namespace

void MqttClientHub::init(DeviceService* dev) {
  s_dev = dev;
}

void MqttClientHub::start_from_nvs() {
  stop();
  if (!mqtt_nvs_load(s_cfg)) {
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.magic = 0x4D545451U;
    s_cfg.version = 1;
    s_cfg.port = 1883;
    strncpy(s_cfg.topic_prefix, "aquarium", sizeof(s_cfg.topic_prefix) - 1);
  }
  if (!s_cfg.enabled || !build_uri_from_cfg()) {
    ESP_LOGI(TAG, "MQTT вимкнено або broker не заданий");
    return;
  }

  rebuild_topics();

  esp_mqtt_client_config_t mqtt_cfg{};
  mqtt_cfg.broker.address.uri = s_uri;
  if (s_cfg.username[0]) {
    mqtt_cfg.credentials.username = s_cfg.username;
  }
  if (s_cfg.password[0]) {
    mqtt_cfg.credentials.authentication.password = s_cfg.password;
  }

  uint8_t mac[6]{};
  char default_id[32]{};
  if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
    snprintf(default_id, sizeof(default_id), "aq-%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4],
             mac[5]);
  } else {
    snprintf(default_id, sizeof(default_id), "aquarium-device");
  }
  mqtt_cfg.credentials.client_id = (s_cfg.client_id[0] != '\0') ? s_cfg.client_id : default_id;

  if (s_lwt_topic[0]) {
    mqtt_cfg.session.last_will.topic = s_lwt_topic;
    mqtt_cfg.session.last_will.msg = "offline";
    mqtt_cfg.session.last_will.msg_len = static_cast<int>(strlen("offline"));
    mqtt_cfg.session.last_will.qos = 1;
    mqtt_cfg.session.last_will.retain = 1;
  }

  s_mqtt = esp_mqtt_client_init(&mqtt_cfg);
  if (s_mqtt == nullptr) {
    ESP_LOGE(TAG, "esp_mqtt_client_init failed");
    return;
  }
  esp_mqtt_client_register_event(s_mqtt, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), mqtt_event_handler,
                                 nullptr);
  esp_err_t e = esp_mqtt_client_start(s_mqtt);
  if (e != ESP_OK) {
    ESP_LOGE(TAG, "esp_mqtt_client_start: %s", esp_err_to_name(e));
    esp_mqtt_client_destroy(s_mqtt);
    s_mqtt = nullptr;
  }
}

void MqttClientHub::restart_from_nvs() {
  start_from_nvs();
}

void MqttClientHub::stop() {
  if (s_mqtt) {
    esp_mqtt_client_stop(s_mqtt);
    esp_mqtt_client_destroy(s_mqtt);
    s_mqtt = nullptr;
  }
  s_mqtt_connected = false;
}

bool MqttClientHub::is_connected() {
  return s_mqtt != nullptr && s_mqtt_connected;
}

void MqttClientHub::on_device_tick() {
  if (s_mqtt == nullptr || s_dev == nullptr) {
    return;
  }
  if (!s_mqtt_connected) {
    return;
  }
  const int64_t now = esp_timer_get_time();
  if (now - s_last_state_publish_us < kStatePublishIntervalUs) {
    return;
  }
  rebuild_topics();
  if (s_state_topic[0] == '\0') {
    return;
  }
  char* js = state_to_json_malloc(s_dev->snapshot());
  if (js == nullptr) {
    return;
  }
  esp_mqtt_client_publish(s_mqtt, s_state_topic, js, static_cast<int>(strlen(js)), 0, 0);
  free(js);
  s_last_state_publish_us = now;
}

}  // namespace aq
