// Written with AI assistance for the specific question/context. All code has been reviewed and understood before implementation.

#include "max30205_sensor.hpp"

#include "esp_log.h"
#include "driver/i2c.h"

static const char *TAG = "max30205_sensor";

namespace pscd::sensors
{
    static constexpr uint8_t TEMPERATURE_REGISTER = 0x00;
    static constexpr TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(100);

    max30205_sensor::max30205_sensor(uint8_t i2c_address)
        : m_i2c_address(i2c_address)
    {
    }

    bool max30205_sensor::begin(i2c_port_t i2c_port)
    {
        if (m_ready)
        {
            return true;
        }

        ESP_LOGI(TAG, "Starting MAX30205 temperature sensor");

        m_i2c_port = i2c_port;

        float test_temperature = 0.0f;

        if (!read_celsius(test_temperature))
        {
            ESP_LOGE(TAG, "MAX30205 did not respond correctly");
            m_ready = false;
            return false;
        }

        m_ready = true;

        ESP_LOGI(TAG, "MAX30205 ready at address 0x%02X", m_i2c_address);
        return true;
    }

    bool max30205_sensor::read_celsius(float &temperature_c)
    {
        uint8_t register_address = TEMPERATURE_REGISTER;
        uint8_t data[2] = {0, 0};

        esp_err_t err = i2c_master_write_read_device(
            m_i2c_port,
            m_i2c_address,
            &register_address,
            1,
            data,
            2,
            I2C_TIMEOUT);

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Temperature read failed: %s", esp_err_to_name(err));
            return false;
        }

        int16_t raw_temperature =
            static_cast<int16_t>((static_cast<uint16_t>(data[0]) << 8) | data[1]);

        temperature_c = static_cast<float>(raw_temperature) * 0.00390625f;

        return true;
    }

    void max30205_sensor::end()
    {
        m_ready = false;
    }

    bool max30205_sensor::is_ready() const
    {
        return m_ready;
    }

    const char *max30205_sensor::name() const
    {
        return "MAX30205";
    }
}