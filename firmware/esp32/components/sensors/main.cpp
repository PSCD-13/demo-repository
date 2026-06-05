#include "MAX30102.hpp"
#include "HeartRateProcessor.hpp"
#include "AbnormalDetector.hpp"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "main";

// Hardware pin configuration
static constexpr gpio_num_t SDA_PIN = GPIO_NUM_21;
static constexpr gpio_num_t SCL_PIN = GPIO_NUM_22;

extern "C" void app_main(void) {
    MAX30102 sensor(I2C_NUM_0, SDA_PIN, SCL_PIN);
    HeartRateProcessor hrProcessor;
    AbnormalDetector detector;

    if (!sensor.begin()) {
        ESP_LOGE(TAG, "Sensor init failed.");
        vTaskDelete(nullptr);
        return;
    }

    while (true) {
        const int available = sensor.availableSamples();

        for (int i = 0; i < available; i++) {
            int ir = 0;
            int red = 0;
            sensor.readFIFO(ir, red);

            const double bpm = hrProcessor.update(ir);
            if (bpm <= 0.0) { // Not enough data yet
                continue;
            }

            const double rmssd = hrProcessor.computeRMSSD();
            const double sdnn = hrProcessor.computeSDNN();

            ESP_LOGI(TAG, "BPM: %.1f | RMSSD: %.2f | SDNN: %.2f", bpm, rmssd, sdnn);

            const AlertStatus status = detector.process(rmssd, sdnn);

            if (status == AlertStatus::NORMAL) {
                if (rmssd > 0.0 && sdnn > 0.0) {
                    detector.pushBaseline(rmssd, sdnn);
                } 
            }
            else if (status == AlertStatus::SUSPICIOUS) {
                ESP_LOGW(TAG, "Suspicious heart-rate, count: %d/%d", detector.getConsecCount(), AbnormalDetector::THRESHOLD);
            }
            else if (status == AlertStatus::ALARMING) {
                ESP_LOGE(TAG, "ABNORMAL HEART-RATE DETECTED !!!");
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
