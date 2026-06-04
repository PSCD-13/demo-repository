# Written with AI assistance for the specific question/context. All code has been reviewed and understood before implementation.

idf_component_register(
    SRCS
        "communication_module.cpp"
        "wifi_manager.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        models
        esp_wifi
        esp_event
        esp_netif
        nvs_flash
        lwip
)