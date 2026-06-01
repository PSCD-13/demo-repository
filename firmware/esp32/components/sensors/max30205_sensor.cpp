#include "max30205_sensor.hpp"

#include "esp_log.h"

static const char *TAG = "max30205_sensor";

namespace pscd::sensors
{

    static constexpr uint8_t TEMPERATURE_REGISTER = 0x00;

    max30205_sensor::max30205_sensor(uint8_t i2c_address)
        : m_i2c_address(i2c_address)
    {
    }

    bool max30205_sensor::begin(i2c_master_bus_handle_t i2c_bus)
    {
        if (m_ready)
        {
            return true;
        }

        ESP_LOGI(TAG, "Starting MAX30205 temperature sensor");

        i2c_device_config_t device_config = {};
        device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device_config.device_address = m_i2c_address;
        device_config.scl_speed_hz = 100000;

        esp_err_t err = i2c_master_bus_add_device(
            i2c_bus,
            &device_config,
            &m_device_handle);

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Could not add MAX30205 to I2C bus: %s", esp_err_to_name(err));
            return false;
        }

        float test_temperature = 0.0f;

        if (!read_celsius(test_temperature))
        {
            ESP_LOGE(TAG, "MAX30205 did not respond correctly");
            i2c_master_bus_rm_device(m_device_handle);
            m_device_handle = nullptr;
            return false;
        }

        m_ready = true;

        ESP_LOGI(TAG, "MAX30205 ready at address 0x%02X", m_i2c_address);
        return true;
    }

    bool max30205_sensor::read_celsius(float &temperature_c)
    {
        if (m_device_handle == nullptr)
        {
            return false;
        }

        uint8_t register_address = TEMPERATURE_REGISTER;
        uint8_t data[2] = {0, 0};

        esp_err_t err = i2c_master_transmit_receive(
            m_device_handle,
            &register_address,
            1,
            data,
            2,
            100);

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
        if (m_device_handle != nullptr)
        {
            i2c_master_bus_rm_device(m_device_handle);
            m_device_handle = nullptr;
        }

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