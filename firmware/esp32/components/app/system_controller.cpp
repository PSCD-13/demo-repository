#include "system_controller.hpp"

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "system_controller";

namespace pscd::app
{

    system_controller::system_controller()
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

        m_temperature_ok = m_temperature_sensor.begin(m_i2c_bus);

        if (m_temperature_ok)
        {
            ESP_LOGI(TAG, "Temperature sensor started");
        }
        else
        {
            ESP_LOGE(TAG, "Temperature sensor failed");
        }

        m_heart_rate_ok = m_heart_rate_sensor.begin(m_i2c_bus);

        if (m_heart_rate_ok)
        {
            ESP_LOGI(TAG, "Heart-rate sensor started");
        }
        else
        {
            ESP_LOGE(TAG, "Heart-rate sensor failed");
        }

        if (!m_temperature_ok && !m_heart_rate_ok)
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

        ESP_LOGI(TAG, "Controller loop started");

        while (true)
        {
            update_once();

            // MAX30102 read_bpm() already blocks for a short measurement window.
            // This delay is still useful to prevent a very tight loop.
            vTaskDelay(pdMS_TO_TICKS(1000));
        }
    }

    void system_controller::update_once()
    {
        pscd::model::sensor_record record{};

        fill_timestamp(record);

        read_temperature(record);
        read_heart_rate(record);

        print_record(record);

        save_record(record);
    }

    bool system_controller::start_i2c_bus()
    {
        if (m_i2c_bus != nullptr)
        {
            return true;
        }

        ESP_LOGI(TAG, "Starting shared I2C bus");

        i2c_master_bus_config_t bus_config = {};
        bus_config.i2c_port = I2C_NUM_0;
        bus_config.sda_io_num = m_i2c_sda;
        bus_config.scl_io_num = m_i2c_scl;
        bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        bus_config.glitch_ignore_cnt = 7;
        bus_config.flags.enable_internal_pullup = true;

        esp_err_t err = i2c_new_master_bus(&bus_config, &m_i2c_bus);

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Could not create I2C bus: %s", esp_err_to_name(err));
            m_i2c_bus = nullptr;
            return false;
        }

        ESP_LOGI(
            TAG,
            "I2C bus ready. SDA=%d SCL=%d",
            static_cast<int>(m_i2c_sda),
            static_cast<int>(m_i2c_scl));

        return true;
    }

    void system_controller::stop_i2c_bus()
    {
        m_temperature_sensor.end();
        m_heart_rate_sensor.end();

        if (m_i2c_bus != nullptr)
        {
            i2c_del_master_bus(m_i2c_bus);
            m_i2c_bus = nullptr;
        }

        m_i2c_ok = false;
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