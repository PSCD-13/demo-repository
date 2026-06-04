#include "system_controller.hpp"

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

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

        m_sd_ok = m_sd.begin();

        if (m_sd_ok)
        {
            ESP_LOGI(TAG, "SD module started");
        }
        else
        {
            ESP_LOGE(TAG, "SD module failed to start");
            ESP_LOGW(TAG, "System will continue without SD logging");
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

        m_heart_rate_ok = m_heart_rate_sensor.begin(m_i2c_port);

        if (m_heart_rate_ok)
        {
            ESP_LOGI(TAG, "Heart-rate sensor started");
        }
        else
        {
            ESP_LOGE(TAG, "Heart-rate sensor failed");
        }

        // display doesnt return anything in failure, there isnt a lot to check here.
        m_display.setupDisplay();

        if (!m_temperature_ok && !m_heart_rate_ok)
        {
            ESP_LOGE(TAG, "No sensors started, stopping controller");
            // return false;
        }

        m_communication_ok = m_communication.begin();

        if (!m_communication_ok)
        {
            ESP_LOGE(TAG, "Wifi connection failed");
        }
        else
        {
            ESP_LOGI(TAG, "Wifi connection succeeded");
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

        ESP_LOGI(TAG, "Controller loop started");

        while (true)
        {
            update_once();

            // MAX30102 read_bpm() already blocks for a short measurement window.
            // This delay is still useful to prevent a very tight loop.
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }

    void system_controller::update_once()
    {
        pscd::model::sensor_record record{};

        fill_timestamp(record);
        read_temperature(record);
        read_heart_rate(record);
        
        m_communication.set_latest_record(record);

        fill_display(record);

        print_record(record);

        save_record(record);
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
            return;
        }

        uint16_t bpm = 0;

        bool ok = m_heart_rate_sensor.read_bpm(bpm);

        if (ok)
        {
            record.heart_rate_valid = true;
            record.heart_rate_bpm = bpm;
        }
        else
        {
            record.heart_rate_valid = false;
            ESP_LOGW(TAG, "Heart-rate read failed");
        }
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
    }

    void system_controller::save_record(pscd::model::sensor_record &record)
    {
        if (!m_sd_ok)
        {
            return;
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
    }

}