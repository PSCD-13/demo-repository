// Written with AI assistance for the specific question/context. All code has been reviewed and understood before implementation.

#include "max30102_sensor.hpp"

#include "esp_err.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "max30102_sensor";
static constexpr TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(100);

namespace pscd::sensors
{
    max30102_sensor::max30102_sensor(uint8_t i2c_address)
        : m_i2c_address(i2c_address)
    {
    }

    max30102_sensor::max30102_sensor(i2c_port_t i2c_port, gpio_num_t, gpio_num_t)
        : m_i2c_port(i2c_port)
    {
    }

    bool max30102_sensor::begin(i2c_port_t i2c_port)
    {
        if (m_ready)
        {
            return true;
        }

        ESP_LOGI(TAG, "Starting MAX30102 heart-rate sensor");

        m_i2c_port = i2c_port;

        uint8_t part_id = 0;
        if (!read_reg(REG_PART_ID, part_id))
        {
            ESP_LOGE(TAG, "MAX30102 did not respond");
            m_ready = false;
            return false;
        }

        if (part_id != EXPECTED_PART_ID)
        {
            ESP_LOGE(TAG, "MAX30102 not found. part_id=0x%02X", part_id);
            m_ready = false;
            return false;
        }

        if (!configure_sensor())
        {
            ESP_LOGE(TAG, "MAX30102 configuration failed");
            m_ready = false;
            return false;
        }

        m_ready = true;

        ESP_LOGI(TAG, "MAX30102 ready at address 0x%02X", m_i2c_address);
        return true;
    }

    bool max30102_sensor::begin()
    {
        return begin(m_i2c_port);
    }

    void max30102_sensor::end()
    {
        m_ready = false;
    }

    bool max30102_sensor::is_ready() const
    {
        return m_ready;
    }

    const char *max30102_sensor::name() const
    {
        return "MAX30102";
    }

    int max30102_sensor::available_samples()
    {
        if (!m_ready)
        {
            return 0;
        }

        uint8_t write_pointer = 0;
        uint8_t read_pointer = 0;
        uint8_t overflow_counter = 0;

        if (!read_reg(REG_FIFO_WR_PTR, write_pointer) ||
            !read_reg(REG_FIFO_RD_PTR, read_pointer) ||
            !read_reg(REG_OVF_COUNTER, overflow_counter))
        {
            return 0;
        }

        const int available =
            (static_cast<int>(write_pointer) - static_cast<int>(read_pointer) + FIFO_DEPTH) % FIFO_DEPTH;

        if (available == 0 && overflow_counter > 0)
        {
            ESP_LOGW(TAG, "MAX30102 FIFO overflow detected");
            return FIFO_DEPTH;
        }
        bool clear_fifo();
        return available;
    }

    bool max30102_sensor::read_sample(max30102_sample &sample)
    {
        sample = {};

        int ir = 0;
        int red = 0;

        if (!read_fifo(ir, red))
        {
            return false;
        }

        sample.ir = ir;
        sample.red = red;
        return true;
    }

    bool max30102_sensor::read_fifo(int &ir, int &red)
    {
        ir = 0;
        red = 0;

        if (!m_ready)
        {
            ESP_LOGW(TAG, "Cannot read MAX30102 FIFO, sensor is not ready");
            return false;
        }

        if (available_samples() <= 0)
        {
            return false;
        }

        uint8_t register_address = REG_FIFO_DATA;
        uint8_t data[6] = {0};

        esp_err_t err = i2c_master_write_read_device(
            m_i2c_port,
            m_i2c_address,
            &register_address,
            1,
            data,
            sizeof(data),
            I2C_TIMEOUT);

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "MAX30102 FIFO read failed: %s", esp_err_to_name(err));
            return false;
        }

        // In SpO2 mode the MAX30102 FIFO returns red first, then IR.
        red = ((static_cast<int>(data[0]) << 16) |
               (static_cast<int>(data[1]) << 8) |
               static_cast<int>(data[2])) &
              FIFO_VALUE_MASK;

        ir = ((static_cast<int>(data[3]) << 16) |
              (static_cast<int>(data[4]) << 8) |
              static_cast<int>(data[5])) &
             FIFO_VALUE_MASK;

        return true;
    }

    int max30102_sensor::availableSamples()
    {
        return available_samples();
    }

    void max30102_sensor::readFIFO(int &ir, int &red)
    {
        if (!read_fifo(ir, red))
        {
            ir = 0;
            red = 0;
        }
    }

    bool max30102_sensor::configure_sensor()
    {
        if (!write_reg(REG_MODE_CONFIG, 0x40))
        {
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(100));

        bool ok = true;
        ok = ok && clear_fifo();
        ok = ok && write_reg(REG_MODE_CONFIG, 0x03); // SpO2 mode: red + IR FIFO data.
        ok = ok && write_reg(REG_SPO2_CONFIG, 0x27); // 100 Hz, 411 us pulse, 18-bit ADC.
        ok = ok && write_reg(REG_LED1_PA, 0x24);     // Red LED current.
        ok = ok && write_reg(REG_LED2_PA, 0x24);     // IR LED current.

        return ok;
    }

    bool max30102_sensor::clear_fifo()
    {
        bool ok = true;
        ok = ok && write_reg(REG_FIFO_WR_PTR, 0x00);
        ok = ok && write_reg(REG_OVF_COUNTER, 0x00);
        ok = ok && write_reg(REG_FIFO_RD_PTR, 0x00);
        return ok;
    }

    bool max30102_sensor::write_reg(uint8_t reg, uint8_t value)
    {
        uint8_t data[2] = {reg, value};

        esp_err_t err = i2c_master_write_to_device(
            m_i2c_port,
            m_i2c_address,
            data,
            sizeof(data),
            I2C_TIMEOUT);

        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "MAX30102 register write failed. reg=0x%02X, value=0x%02X, error=%s",
                reg,
                value,
                esp_err_to_name(err));
            return false;
        }

        return true;
    }

    bool max30102_sensor::read_reg(uint8_t reg, uint8_t &value)
    {
        value = 0;

        esp_err_t err = i2c_master_write_read_device(
            m_i2c_port,
            m_i2c_address,
            &reg,
            1,
            &value,
            1,
            I2C_TIMEOUT);

        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "MAX30102 register read failed. reg=0x%02X, error=%s",
                reg,
                esp_err_to_name(err));
            return false;
        }

        return true;
    }

} // namespace pscd::sensors
