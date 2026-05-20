idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    PRIV_REQUIRES aquarium_app nvs_flash esp_netif esp_event esp_wifi
)
