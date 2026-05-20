#include "api_dispatch.hpp"

#include <cstring>
#include <string>
#include "device_service.hpp"
#include "mqtt_client_hub.hpp"
#include "mqtt_nvs.hpp"

#include "esp_log.h"

static const char* TAG = "api_dispatch";

namespace aq {

static std::string json_string_malloc(cJSON* root) {
  if (root == nullptr) {
    return R"({"type":"error","msg":"oom"})";
  }
  char* raw = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (raw == nullptr) {
    return R"({"type":"error","msg":"oom"})";
  }
  std::string out(raw);
  cJSON_free(raw);
  return out;
}

static std::string handle_get_settings(DeviceService* dev) {
  char* dev_js = dev->export_settings_data_json_malloc();
  if (dev_js == nullptr) {
    return R"({"type":"error","msg":"busy"})";
  }
  cJSON* data = cJSON_Parse(dev_js);
  free(dev_js);
  if (!cJSON_IsObject(data)) {
    cJSON_Delete(data);
    return R"({"type":"error","msg":"json"})";
  }

  MqttNvsConfig mq{};
  const bool have_mqtt = mqtt_nvs_load(mq);

  cJSON* mj = cJSON_CreateObject();
  if (!mj) {
    cJSON_Delete(data);
    return R"({"type":"error","msg":"oom"})";
  }
  if (have_mqtt) {
    cJSON_AddBoolToObject(mj, "enabled", mq.enabled);
    cJSON_AddStringToObject(mj, "broker_host", mq.broker_host);
    cJSON_AddNumberToObject(mj, "port", mq.port);
    cJSON_AddStringToObject(mj, "username", mq.username);
    cJSON_AddBoolToObject(mj, "password_set", mq.password[0] != '\0');
    cJSON_AddStringToObject(mj, "client_id", mq.client_id);
    cJSON_AddStringToObject(mj, "topic_prefix", mq.topic_prefix);
  } else {
    cJSON_AddBoolToObject(mj, "enabled", false);
    cJSON_AddStringToObject(mj, "broker_host", "");
    cJSON_AddNumberToObject(mj, "port", 1883);
    cJSON_AddStringToObject(mj, "username", "");
    cJSON_AddBoolToObject(mj, "password_set", false);
    cJSON_AddStringToObject(mj, "client_id", "");
    cJSON_AddStringToObject(mj, "topic_prefix", "aquarium");
  }
  cJSON_AddBoolToObject(mj, "connected", MqttClientHub::is_connected());
  cJSON_AddItemToObject(data, "mqtt", mj);

  cJSON* root = cJSON_CreateObject();
  if (!root) {
    cJSON_Delete(data);
    return R"({"type":"error","msg":"oom"})";
  }
  cJSON_AddStringToObject(root, "type", "settings");
  cJSON_AddItemToObject(root, "data", data);
  return json_string_malloc(root);
}

static void copy_json_str(char* dest, size_t dest_sz, cJSON* item) {
  if (dest_sz == 0) {
    return;
  }
  dest[0] = '\0';
  if (cJSON_IsString(item) && item->valuestring) {
    strncpy(dest, item->valuestring, dest_sz - 1);
    dest[dest_sz - 1] = '\0';
  }
}

static std::string handle_set_mqtt_config(cJSON* root) {
  MqttNvsConfig cfg{};
  if (!mqtt_nvs_load(cfg)) {
    memset(&cfg, 0, sizeof(cfg));
    cfg.magic = 0x4D545451U;
    cfg.version = 1;
    cfg.port = 1883;
    strncpy(cfg.topic_prefix, "aquarium", sizeof(cfg.topic_prefix) - 1);
  }

  cJSON* it = cJSON_GetObjectItemCaseSensitive(root, "enabled");
  if (cJSON_IsBool(it)) {
    cfg.enabled = cJSON_IsTrue(it);
  }
  it = cJSON_GetObjectItemCaseSensitive(root, "broker_host");
  copy_json_str(cfg.broker_host, sizeof(cfg.broker_host), it);
  it = cJSON_GetObjectItemCaseSensitive(root, "port");
  if (cJSON_IsNumber(it)) {
    const int p = static_cast<int>(it->valuedouble);
    if (p > 0 && p < 65535) {
      cfg.port = static_cast<uint16_t>(p);
    }
  }
  it = cJSON_GetObjectItemCaseSensitive(root, "username");
  copy_json_str(cfg.username, sizeof(cfg.username), it);
  it = cJSON_GetObjectItemCaseSensitive(root, "client_id");
  copy_json_str(cfg.client_id, sizeof(cfg.client_id), it);
  it = cJSON_GetObjectItemCaseSensitive(root, "topic_prefix");
  copy_json_str(cfg.topic_prefix, sizeof(cfg.topic_prefix), it);

  it = cJSON_GetObjectItemCaseSensitive(root, "clear_mqtt_password");
  if (cJSON_IsBool(it) && cJSON_IsTrue(it)) {
    cfg.password[0] = '\0';
  } else {
    it = cJSON_GetObjectItemCaseSensitive(root, "password");
    if (it != nullptr && cJSON_IsString(it) && it->valuestring) {
      copy_json_str(cfg.password, sizeof(cfg.password), it);
    }
  }

  if (!mqtt_nvs_save(cfg)) {
    ESP_LOGW(TAG, "mqtt_nvs_save failed");
    return R"({"type":"error","msg":"mqtt_save"})";
  }
  MqttClientHub::restart_from_nvs();
  return R"({"type":"ack"})";
}

static std::string handle_set_settings(cJSON* root, DeviceService* dev) {
  cJSON* data = cJSON_GetObjectItemCaseSensitive(root, "data");
  if (!cJSON_IsObject(data)) {
    return R"({"type":"error","msg":"need data"})";
  }
  char* payload = cJSON_PrintUnformatted(data);
  if (payload == nullptr) {
    return R"({"type":"error","msg":"oom"})";
  }
  const std::string err = dev->import_settings_data_json(payload);
  cJSON_free(payload);
  if (!err.empty()) {
    cJSON* er = cJSON_CreateObject();
    cJSON_AddStringToObject(er, "type", "error");
    cJSON_AddStringToObject(er, "msg", err.c_str());
    return json_string_malloc(er);
  }
  return R"({"type":"ack"})";
}

std::string dispatch_ipc_json(const char* json, DeviceService* dev) {
  if (json == nullptr || dev == nullptr) {
    return R"({"type":"error","msg":"null"})";
  }

  cJSON* root = cJSON_Parse(json);
  if (!root) {
    return R"({"type":"error","msg":"json"})";
  }

  cJSON* type = cJSON_GetObjectItemCaseSensitive(root, "type");
  const char* ts = cJSON_IsString(type) ? type->valuestring : "";
  if (strcmp(ts, "cmd") != 0) {
    cJSON_Delete(root);
    return R"({"type":"error","msg":"need type:cmd"})";
  }

  cJSON* name = cJSON_GetObjectItemCaseSensitive(root, "name");
  const char* cmd = (cJSON_IsString(name) && name->valuestring) ? name->valuestring : "";

  if (strcmp(cmd, "get_settings") == 0) {
    cJSON_Delete(root);
    return handle_get_settings(dev);
  }

  if (strcmp(cmd, "set_settings") == 0) {
    std::string out = handle_set_settings(root, dev);
    cJSON_Delete(root);
    return out;
  }

  if (strcmp(cmd, "set_mqtt_config") == 0) {
    std::string out = handle_set_mqtt_config(root);
    cJSON_Delete(root);
    return out;
  }

  cJSON_Delete(root);
  return dev->handle_ws_json(json);
}
