// Written with AI assistance for the specific question/context. All code has been reviewed and understood before implementation.

#include "system_controller.hpp"

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BUZZ_PIN GPIO_NUM_33

static const char *TAG = "system_controller";

namespace pscd::app
{

    static pscd::communication::communication_config make_communication_config()
    {
        pscd::communication::communication_config config{};

        config.ssid = "ACSlab";
        config.password = "lab@ACS24";
        config.pc_ip = "145.76.19.92";
        config.tcp_port = 8888;
        config.discovery_port = 9999;
        config.pc_notify_port = 9998;

        return config;
    }

    system_controller::system_controller()
        : m_communication(make_communication_config())
    {
    }

    bool system_controller::begin()
    {
        ESP_LOGI(TAG, "Starting system controller");

        gpio_set_direction(BUZZ_PIN, GPIO_MODE_OUTPUT);
        set_buzzer_output(false);

        gpio_set_direction(static_cast<gpio_num_t>(MODE_BUTTON_PIN), GPIO_MODE_INPUT);
        gpio_set_pull_mode(static_cast<gpio_num_t>(MODE_BUTTON_PIN), GPIO_PULLDOWN_ONLY);

        gpio_set_direction(static_cast<gpio_num_t>(PANIC_BUTTON_PIN), GPIO_MODE_INPUT);

        // GPIO34 op een normale ESP32 heeft geen interne pull-up/pull-down.
        // Omdat jouw panic button active-high is, moet GPIO34 hardwarematig een pulldown hebben.
        gpio_set_pull_mode(static_cast<gpio_num_t>(PANIC_BUTTON_PIN), GPIO_FLOATING);

        m_record_mutex = xSemaphoreCreateMutex();
        m_spi_mutex = xSemaphoreCreateMutex();

        if (m_record_mutex == nullptr || m_spi_mutex == nullptr)
        {
            ESP_LOGE(TAG, "Could not create controller mutexes");
            return false;
        }

        m_i2c_ok = start_i2c_bus();

        if (!m_i2c_ok)
        {
            ESP_LOGE(TAG, "I2C bus failed to start");
            return false;
        }

        m_temperature_ok = m_temperature_sensor.begin(m_i2c_port);

        if (m_temperature_ok)
        {
            ESP_LOGI(TAG, "Temperature sensor started");
        }
        else
        {
            ESP_LOGE(TAG, "Temperature sensor failed");
        }

        m_fall_detect_ok = m_fall_detector.initialize();

        if (m_fall_detect_ok)
        {
            ESP_LOGI(TAG, "Fall detection started");
        }
        else
        {
            ESP_LOGE(TAG, "Fall detection failed");
        }

        m_communication_ok = m_communication.begin();

        if (m_communication_ok)
        {
            ESP_LOGI(TAG, "Wifi communication started");
        }
        else
        {
            ESP_LOGE(TAG, "Wifi communication failed");
        }

        if (m_spi_mutex != nullptr)
        {
            xSemaphoreTake(m_spi_mutex, portMAX_DELAY);
        }

        m_display.setupDisplay();

        if (m_spi_mutex != nullptr)
        {
            xSemaphoreGive(m_spi_mutex);
        }

        if (m_spi_mutex != nullptr)
        {
            xSemaphoreTake(m_spi_mutex, portMAX_DELAY);
        }

        m_sd_ok = m_sd.begin();

        if (m_spi_mutex != nullptr)
        {
            xSemaphoreGive(m_spi_mutex);
        }

        if (m_sd_ok)
        {
            ESP_LOGI(TAG, "SD module started");
        }
        else
        {
            ESP_LOGE(TAG, "SD module failed to start");
            ESP_LOGW(TAG, "System will continue without SD logging");
        }

        // Start MAX30102 as late as possible, so its FIFO does not fill during the rest of boot.
        m_heart_rate_ok = m_heart_rate_sensor.begin(m_i2c_port);

        if (m_heart_rate_ok)
        {
            ESP_LOGI(TAG, "Heart-rate sensor started");
        }
        else
        {
            ESP_LOGE(TAG, "Heart-rate sensor failed");
        }

        if (!m_temperature_ok && !m_heart_rate_ok && !m_fall_detect_ok)
        {
            ESP_LOGE(TAG, "No sensors started, stopping controller");
            return false;
        }

        ESP_LOGI(TAG, "System controller ready");
        return true;
    }

    void system_controller::run()
    {
        if (!begin())
        {
            ESP_LOGE(TAG, "Controller could not start");
            return;
        }

        ESP_LOGI(TAG, "Controller tasks starting");

        xTaskCreate(
            sensor_display_task_entry,
            "sensor_display_task",
            8192,
            this,
            6,
            nullptr);

        xTaskCreate(
            sd_wifi_task_entry,
            "sd_wifi_task",
            8192,
            this,
            4,
            nullptr);

        while (true)
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    void system_controller::sensor_display_task_entry(void *arg)
    {
        auto *controller = static_cast<system_controller *>(arg);
        controller->sensor_display_task();
    }

    void system_controller::sd_wifi_task_entry(void *arg)
    {
        auto *controller = static_cast<system_controller *>(arg);
        controller->sd_wifi_task();
    }

    void system_controller::sensor_display_task()
    {
        ESP_LOGI(TAG, "Sensor/display task started");

        while (true)
        {
            pscd::model::sensor_record record = copy_latest_record();

            fill_timestamp(record);

            read_all_fast_sensors(record);

            update_emergency_edge_and_buzzer(record);

            update_latest_record(record);

            if (m_spi_mutex != nullptr)
            {
                xSemaphoreTake(m_spi_mutex, portMAX_DELAY);
            }

            fill_display(record);

            if (m_spi_mutex != nullptr)
            {
                xSemaphoreGive(m_spi_mutex);
            }

            vTaskDelay(pdMS_TO_TICKS(SENSOR_AND_DISPLAY_PERIOD_MS));
        }
    }

    void system_controller::sd_wifi_task()
    {
        ESP_LOGI(TAG, "SD/WiFi task started");

        while (true)
        {
            pscd::model::sensor_record record = copy_latest_record();

            fill_timestamp(record);

            bool saved = false;

            if (m_spi_mutex != nullptr)
            {
                xSemaphoreTake(m_spi_mutex, portMAX_DELAY);
            }

            saved = save_record(record);

            if (m_spi_mutex != nullptr)
            {
                xSemaphoreGive(m_spi_mutex);
            }

            // Manual log is an event. Keep it pending until it has been included in a saved record.
            if (saved && record.manual_log)
            {
                m_manual_log_pending = false;

                pscd::model::sensor_record updated_record = copy_latest_record();
                updated_record.manual_log = false;
                update_latest_record(updated_record);
            }

            // After trying to store locally, make the same latest record available to Wi-Fi.
            if (m_communication_ok)
            {
                m_communication.set_latest_record(record);
            }

            print_record(record);

            vTaskDelay(pdMS_TO_TICKS(SD_AND_WIFI_PERIOD_MS));
        }
    }

    bool system_controller::start_i2c_bus()
    {
        if (m_i2c_ok)
        {
            return true;
        }

        ESP_LOGI(TAG, "Starting shared I2C bus");

        i2c_config_t config = {};
        config.mode = I2C_MODE_MASTER;
        config.sda_io_num = m_i2c_sda;
        config.scl_io_num = m_i2c_scl;
        config.sda_pullup_en = GPIO_PULLUP_ENABLE;
        config.scl_pullup_en = GPIO_PULLUP_ENABLE;
        config.master.clk_speed = 400000;

        esp_err_t err = i2c_param_config(m_i2c_port, &config);

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Could not configure I2C bus: %s", esp_err_to_name(err));
            return false;
        }

        err = i2c_driver_install(m_i2c_port, I2C_MODE_MASTER, 0, 0, 0);

        if (err != ESP_OK && err != ESP_ERR_INVALID_STATE)
        {
            ESP_LOGE(TAG, "Could not install I2C driver: %s", esp_err_to_name(err));
            return false;
        }

        ESP_LOGI(
            TAG,
            "I2C bus ready. Port=%d SDA=%d SCL=%d",
            static_cast<int>(m_i2c_port),
            static_cast<int>(m_i2c_sda),
            static_cast<int>(m_i2c_scl));

        return true;
    }

    void system_controller::stop_i2c_bus()
    {
        m_temperature_sensor.end();
        m_heart_rate_sensor.end();

        i2c_driver_delete(m_i2c_port);
        m_i2c_ok = false;
    }

    void system_controller::read_all_fast_sensors(pscd::model::sensor_record &record)
    {
        read_buttons(record);
        read_temperature(record);
        read_heart_rate(record);
        read_motion_and_fall(record);

        record.workout_mode = m_workout_mode;
        record.manual_log = m_manual_log_pending;
    }

    void system_controller::read_temperature(pscd::model::sensor_record &record)
    {
        if (!m_temperature_ok)
        {
            record.skin_temp_valid = false;
            return;
        }

        float temperature_c = 0.0f;
        bool ok = m_temperature_sensor.read_celsius(temperature_c);

        if (ok)
        {
            record.skin_temp_valid = true;
            record.skin_temp_c = temperature_c;
        }
        else
        {
            record.skin_temp_valid = false;
            ESP_LOGE(TAG, "Temperature read failed");
        }
    }

    void system_controller::read_heart_rate(pscd::model::sensor_record &record)
    {
        if (!m_heart_rate_ok)
        {
            record.heart_rate_valid = false;
            record.abnormal_heart_rate = false;
            return;
        }

        const int available = m_heart_rate_sensor.availableSamples();

        if (available <= 0)
        {
            return;
        }

        for (int i = 0; i < available; i++)
        {
            int ir = 0;
            int red = 0;

            m_heart_rate_sensor.readFIFO(ir, red);

            if (ir <= 0)
            {
                continue;
            }

            const double bpm = m_heart_rate_processor.update(ir);

            if (bpm <= 0.0)
            {
                continue;
            }

            record.heart_rate_valid = true;
            record.heart_rate_bpm = static_cast<uint16_t>(bpm);

            const double rmssd = m_heart_rate_processor.computeRMSSD();
            const double sdnn = m_heart_rate_processor.computeSDNN();

            const pscd::detection::AlertStatus status =
                m_abnormal_detector.process(rmssd, sdnn);

            const bool abnormal_now = update_abnormal_heart_status(status);

            record.abnormal_heart_rate = abnormal_now;

            if (status == pscd::detection::AlertStatus::NORMAL)
            {
                if (rmssd > 0.0 && sdnn > 0.0)
                {
                    m_abnormal_detector.pushBaseline(rmssd, sdnn);
                }
            }

            if (abnormal_now)
            {
                ESP_LOGE(TAG, "ABNORMAL HEART-RATE DETECTED");
            }
        }
    }

    bool system_controller::update_abnormal_heart_status(pscd::detection::AlertStatus status)
    {
        if (status != pscd::detection::AlertStatus::ALARMING)
        {
            m_abnormal_confirm_count = 0;
            return false;
        }

        const uint8_t required_count =
            m_workout_mode ? ABNORMAL_CONFIRM_WORKOUT_MODE : ABNORMAL_CONFIRM_NORMAL_MODE;

        if (m_abnormal_confirm_count < required_count)
        {
            m_abnormal_confirm_count++;
        }

        return m_abnormal_confirm_count >= required_count;
    }

    void system_controller::read_motion_and_fall(pscd::model::sensor_record &record)
    {
        if (!m_fall_detect_ok)
        {
            record.motion_valid = false;
            record.fall_detected = false;
            return;
        }

        m_fall_detector.updateData();
        record.motion_valid = true;
        record.fall_detected = m_fall_detector.isFalling();
    }

    void system_controller::read_buttons(pscd::model::sensor_record &record)
    {
        const uint32_t now_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000);

        const bool mode_button_down = read_active_high_button(MODE_BUTTON_PIN);
        const bool panic_button_down = read_active_high_button(PANIC_BUTTON_PIN);

        record.panic_pressed = panic_button_down;

        if (mode_button_down && !m_mode_button_was_down)
        {
            m_mode_button_press_start_ms = now_ms;
            m_mode_button_long_handled = false;
        }

        if (mode_button_down && !m_mode_button_long_handled)
        {
            const uint32_t held_ms = now_ms - m_mode_button_press_start_ms;

            if (held_ms >= LONG_PRESS_MS)
            {
                m_workout_mode = !m_workout_mode;
                m_mode_button_long_handled = true;

                ESP_LOGI(TAG, "Workout mode %s", m_workout_mode ? "ON" : "OFF");
            }
        }

        if (!mode_button_down && m_mode_button_was_down)
        {
            if (!m_mode_button_long_handled)
            {
                m_manual_log_pending = true;
                ESP_LOGI(TAG, "Manual log requested");
            }
        }

        m_mode_button_was_down = mode_button_down;

        record.workout_mode = m_workout_mode;
        record.manual_log = m_manual_log_pending;
    }

    bool system_controller::read_active_high_button(int pin) const
    {
        if (pin < 0)
        {
            return false;
        }

        return gpio_get_level(static_cast<gpio_num_t>(pin)) == 1;
    }

    void system_controller::fill_display(pscd::model::sensor_record &record)
    {
        m_display.updateData(record);
        m_display.refreshDisplay();
    }

    void system_controller::fill_timestamp(pscd::model::sensor_record &record)
    {
        int64_t time_us = esp_timer_get_time();
        record.timestamp_ms = static_cast<uint32_t>(time_us / 1000);
    }

    void system_controller::print_record(const pscd::model::sensor_record &record)
    {
        if (record.skin_temp_valid)
        {
            ESP_LOGI(TAG, "Temperature: %.2f C", record.skin_temp_c);
        }
        else
        {
            ESP_LOGI(TAG, "Temperature: invalid");
        }

        if (record.heart_rate_valid)
        {
            ESP_LOGI(TAG, "Heart rate: %u bpm", record.heart_rate_bpm);
        }
        else
        {
            ESP_LOGI(TAG, "Heart rate: invalid");
        }

        ESP_LOGI(
            TAG,
            "fall=%d abnormal_hr=%d panic=%d manual=%d workout=%d",
            record.fall_detected ? 1 : 0,
            record.abnormal_heart_rate ? 1 : 0,
            record.panic_pressed ? 1 : 0,
            record.manual_log ? 1 : 0,
            record.workout_mode ? 1 : 0);
    }

    bool system_controller::save_record(pscd::model::sensor_record &record)
    {
        if (!m_sd_ok)
        {
            return false;
        }

        bool ok = m_sd.append_record(record);

        if (ok)
        {
            ESP_LOGI(
                TAG,
                "Saved record %lu to SD",
                static_cast<unsigned long>(record.record_id));
        }
        else
        {
            ESP_LOGE(TAG, "Could not save record to SD");
        }

        return ok;
    }

    void system_controller::update_latest_record(const pscd::model::sensor_record &record)
    {
        if (m_record_mutex != nullptr)
        {
            xSemaphoreTake(m_record_mutex, portMAX_DELAY);
        }

        m_latest_record = record;

        if (m_record_mutex != nullptr)
        {
            xSemaphoreGive(m_record_mutex);
        }
    }

    pscd::model::sensor_record system_controller::copy_latest_record()
    {
        pscd::model::sensor_record copy{};

        if (m_record_mutex != nullptr)
        {
            xSemaphoreTake(m_record_mutex, portMAX_DELAY);
        }

        copy = m_latest_record;

        if (m_record_mutex != nullptr)
        {
            xSemaphoreGive(m_record_mutex);
        }

        return copy;
    }

    bool system_controller::is_emergency(const pscd::model::sensor_record &record) const
    {
        return record.fall_detected || record.abnormal_heart_rate || record.panic_pressed;
    }

    void system_controller::update_emergency_edge_and_buzzer(const pscd::model::sensor_record &record)
    {
        const bool emergency_now = is_emergency(record);

        if (emergency_now)
        {
            start_buzzer_pattern(record.timestamp_ms);
        }
        update_buzzer(record.timestamp_ms);
    }

    void system_controller::start_buzzer_pattern(uint32_t now_ms)
    {
        m_buzzer_active = true;
        m_buzzer_phase = 0;
        m_buzzer_next_change_ms = now_ms + BUZZER_BEEP_ON_MS;
        set_buzzer_output(true);
    }

    void system_controller::update_buzzer(uint32_t now_ms)
    {
        if (!m_buzzer_active)
        {
            return;
        }

        if (static_cast<int32_t>(now_ms - m_buzzer_next_change_ms) < 0)
        {
            return;
        }

        m_buzzer_phase++;

        if (m_buzzer_phase >= BUZZER_BEEP_COUNT * 2)
        {
            stop_buzzer();
            return;
        }

        const bool buzzer_on = (m_buzzer_phase % 2) == 0;
        set_buzzer_output(buzzer_on);

        if (buzzer_on)
        {
            m_buzzer_next_change_ms = now_ms + BUZZER_BEEP_ON_MS;
        }
        else
        {
            m_buzzer_next_change_ms = now_ms + BUZZER_BEEP_OFF_MS;
        }
    }

    void system_controller::stop_buzzer()
    {
        m_buzzer_active = false;
        m_buzzer_phase = 0;
        m_buzzer_next_change_ms = 0;
        set_buzzer_output(false);
    }

    void system_controller::set_buzzer_output(bool on)
    {
        gpio_set_level(BUZZ_PIN, on ? 1 : 0);
    }

} // namespace pscd::app