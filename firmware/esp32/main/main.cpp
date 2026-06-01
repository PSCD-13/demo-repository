#include "sd_module.hpp"
#include "sensor_record.hpp"
#include "esp_log.h"

static const char *TAG = "SD_ONLY_TEST";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "sd only test starting");

    pscd::storage::sd_module sd_card;

    if(!sd_card.begin())
    {
        ESP_LOGE(TAG, "sd couldnt start");
        return;
    }

    pscd::model::sensor_record record;
    record.timestamp_ms = 1234;
    record.skin_temp_c = 21;
    record.skin_temp_valid = true;
    record.heart_rate_bpm = 90;

    if(sd_card.append_record(record))
    {
        ESP_LOGI(TAG, "sd write worked");
    }
    else
    {
        ESP_LOGI(TAG, "sd write failed");
    }
}