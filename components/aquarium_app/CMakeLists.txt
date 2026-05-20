idf_component_register(
    SRCS
        "src/model.cpp"
        "src/device_service.cpp"
        "src/schedule_engine.cpp"
        "src/rgbw_ledc.cpp"
        "src/pump_gpio.cpp"
        "src/json_state.cpp"
        "src/nvs_store.cpp"
        "src/esp_time_source.cpp"
        "src/wifi_station.cpp"
        "src/http_ws_portal.cpp"
        "src/firmware_http_update.cpp"
        "src/aquarium_app.cpp"
    INCLUDE_DIRS "include"
    EMBED_FILES "web/index.html"
    REQUIRES driver esp_http_server esp_wifi esp_netif esp_event nvs_flash esp_timer json app_update esp_partition)
