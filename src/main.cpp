#include "esp_log.h"
#include "system_controller.hpp"

static const char *TAG = "SD_ONLY_TEST";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Wifi test");

    pscd::app::system_controller controlla;

    controlla.run();

    return;
}