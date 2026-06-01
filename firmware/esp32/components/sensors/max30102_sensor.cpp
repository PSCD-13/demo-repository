#include "max30102_sensor.hpp"

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "max30102_sensor";

namespace pscd::sensors
{

    // MAX30102 register addresses
    static constexpr uint8_t REG_INTERRUPT_STATUS_1 = 0x00;
    static constexpr uint8_t REG_FIFO_WRITE_POINTER = 0x04;
    static constexpr uint8_t REG_OVERFLOW_COUNTER = 0x05;
    static constexpr uint8_t REG_FIFO_READ_POINTER = 0x06;
    static constexpr uint8_t REG_FIFO_DATA = 0x07;
    static constexpr uint8_t REG_FIFO_CONFIG = 0x08;
    static constexpr uint8_t REG_MODE_CONFIG = 0x09;
    static constexpr uint8_t REG_SPO2_CONFIG = 0x0A;
    static constexpr uint8_t REG_LED1_PA = 0x0C; // Red LED
    static constexpr uint8_t REG_LED2_PA = 0x0D; // IR LED
    static constexpr uint8_t REG_PART_ID = 0xFF;

    max30102_sensor::max30102_sensor(uint8_t i2c_address)
        : m_i2c_address(i2c_address)
    {
    }

    bool max30102_sensor::begin(i2c_master_bus_handle_t i2c_bus)
    {
        if (m_ready)
        {
            return true;
        }

        ESP_LOGI(TAG, "Starting MAX30102 heart-rate sensor");

        i2c_device_config_t device_config = {};
        device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
        device_config.device_address = m_i2c_address;
        device_config.scl_speed_hz = 400000;

        esp_err_t err = i2c_master_bus_add_device(
            i2c_bus,
            &device_config,
            &m_device_handle);

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "Could not add MAX30102 to I2C bus: %s", esp_err_to_name(err));
            return false;
        }

        uint8_t part_id = 0;

        if (!read_register(REG_PART_ID, part_id))
        {
            ESP_LOGE(TAG, "Could not read MAX30102 part ID");
            i2c_master_bus_rm_device(m_device_handle);
            m_device_handle = nullptr;
            return false;
        }

        ESP_LOGI(TAG, "MAX30102 part ID: 0x%02X", part_id);

        if (!reset_sensor())
        {
            ESP_LOGE(TAG, "MAX30102 reset failed");
            i2c_master_bus_rm_device(m_device_handle);
            m_device_handle = nullptr;
            return false;
        }

        if (!configure_sensor())
        {
            ESP_LOGE(TAG, "MAX30102 configuration failed");
            i2c_master_bus_rm_device(m_device_handle);
            m_device_handle = nullptr;
            return false;
        }

        m_ready = true;

        ESP_LOGI(TAG, "MAX30102 ready at address 0x%02X", m_i2c_address);
        return true;
    }

    void max30102_sensor::end()
    {
        if (m_device_handle != nullptr)
        {
            i2c_master_bus_rm_device(m_device_handle);
            m_device_handle = nullptr;
        }

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

    bool max30102_sensor::reset_sensor()
    {
        if (!write_register(REG_MODE_CONFIG, 0x40))
        {
            return false;
        }

        vTaskDelay(pdMS_TO_TICKS(100));

        return true;
    }

    bool max30102_sensor::configure_sensor()
    {
        // Reset FIFO pointers
        if (!write_register(REG_FIFO_WRITE_POINTER, 0x00))
            return false;
        if (!write_register(REG_OVERFLOW_COUNTER, 0x00))
            return false;
        if (!write_register(REG_FIFO_READ_POINTER, 0x00))
            return false;

        // FIFO config:
        // sample average = 4, FIFO rollover disabled, almost full = 15
        if (!write_register(REG_FIFO_CONFIG, 0x4F))
            return false;

        // Mode config:
        // 0x03 = SpO2 mode, uses Red + IR
        if (!write_register(REG_MODE_CONFIG, 0x03))
            return false;

        // SpO2 config:
        // ADC range + sample rate + pulse width.
        // This is a common simple setting for demos.
        if (!write_register(REG_SPO2_CONFIG, 0x27))
            return false;

        // LED brightness/current.
        // If signal is too weak, increase these carefully.
        if (!write_register(REG_LED1_PA, 0x24))
            return false; // Red
        if (!write_register(REG_LED2_PA, 0x24))
            return false; // IR

        // Clear interrupt status by reading it.
        uint8_t dummy = 0;
        read_register(REG_INTERRUPT_STATUS_1, dummy);

        return true;
    }

    bool max30102_sensor::read_bpm(uint16_t &bpm)
    {
        bpm = 0;

        if (!m_ready)
        {
            ESP_LOGE(TAG, "MAX30102 not ready");
            return false;
        }

        // Simple first algorithm:
        // Collect IR samples and count rising threshold crossings.
        // This is not a medical-grade algorithm, but useful for first integration.

        static constexpr int SAMPLE_COUNT = 120;
        static constexpr int SAMPLE_DELAY_MS = 20;

        uint32_t ir_values[SAMPLE_COUNT] = {};
        uint32_t red_dummy = 0;
        uint32_t ir = 0;

        uint32_t min_ir = 0xFFFFFFFF;
        uint32_t max_ir = 0;

        for (int i = 0; i < SAMPLE_COUNT; i++)
        {
            if (!read_fifo_sample(red_dummy, ir))
            {
                ESP_LOGE(TAG, "Could not read FIFO sample");
                return false;
            }

            ir_values[i] = ir;

            if (ir < min_ir)
            {
                min_ir = ir;
            }

            if (ir > max_ir)
            {
                max_ir = ir;
            }

            vTaskDelay(pdMS_TO_TICKS(SAMPLE_DELAY_MS));
        }

        uint32_t range = max_ir - min_ir;

        ESP_LOGI(
            TAG,
            "IR min=%lu max=%lu range=%lu",
            static_cast<unsigned long>(min_ir),
            static_cast<unsigned long>(max_ir),
            static_cast<unsigned long>(range));

        // Very rough finger detection.
        // If this fails while your finger is on the sensor, lower the limits.
        if (max_ir < 50000 || range < 1000)
        {
            ESP_LOGW(TAG, "No clear finger / pulse signal detected");
            return false;
        }

        uint32_t threshold = min_ir + (range / 2);

        bool was_above = false;
        int beat_count = 0;

        int first_beat_index = -1;
        int last_beat_index = -1;

        int last_detected_index = -1000;

        for (int i = 0; i < SAMPLE_COUNT; i++)
        {
            bool is_above = ir_values[i] > threshold;

            // Rising edge detection
            if (is_above && !was_above)
            {
                int samples_since_last = i - last_detected_index;

                // Debounce: ignore impossible fast beats.
                // 15 samples * 20 ms = 300 ms.
                if (samples_since_last > 15)
                {
                    beat_count++;

                    if (first_beat_index < 0)
                    {
                        first_beat_index = i;
                    }

                    last_beat_index = i;
                    last_detected_index = i;
                }
            }

            was_above = is_above;
        }

        if (beat_count < 2)
        {
            ESP_LOGW(TAG, "Not enough beats detected");
            return false;
        }

        int sample_difference = last_beat_index - first_beat_index;
        int time_difference_ms = sample_difference * SAMPLE_DELAY_MS;

        if (time_difference_ms <= 0)
        {
            return false;
        }

        int calculated_bpm = ((beat_count - 1) * 60000) / time_difference_ms;

        if (calculated_bpm < 40 || calculated_bpm > 220)
        {
            ESP_LOGW(TAG, "Calculated BPM outside expected range: %d", calculated_bpm);
            return false;
        }

        bpm = static_cast<uint16_t>(calculated_bpm);

        ESP_LOGI(TAG, "Heart rate: %u bpm", bpm);

        return true;
    }

    bool max30102_sensor::write_register(uint8_t reg, uint8_t value)
    {
        if (m_device_handle == nullptr)
        {
            return false;
        }

        uint8_t data[2] = {reg, value};

        esp_err_t err = i2c_master_transmit(
            m_device_handle,
            data,
            sizeof(data),
            1000);

        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Write register 0x%02X failed: %s",
                reg,
                esp_err_to_name(err));

            return false;
        }

        return true;
    }

    bool max30102_sensor::read_register(uint8_t reg, uint8_t &value)
    {
        if (m_device_handle == nullptr)
        {
            return false;
        }

        esp_err_t err = i2c_master_transmit_receive(
            m_device_handle,
            &reg,
            1,
            &value,
            1,
            1000);

        if (err != ESP_OK)
        {
            ESP_LOGE(
                TAG,
                "Read register 0x%02X failed: %s",
                reg,
                esp_err_to_name(err));

            return false;
        }

        return true;
    }

    bool max30102_sensor::read_fifo_sample(uint32_t &red, uint32_t &ir)
    {
        red = 0;
        ir = 0;

        if (m_device_handle == nullptr)
        {
            return false;
        }

        uint8_t reg = REG_FIFO_DATA;
        uint8_t data[6] = {};

        esp_err_t err = i2c_master_transmit_receive(
            m_device_handle,
            &reg,
            1,
            data,
            sizeof(data),
            1000);

        if (err != ESP_OK)
        {
            ESP_LOGE(TAG, "FIFO read failed: %s", esp_err_to_name(err));
            return false;
        }

        red =
            ((static_cast<uint32_t>(data[0]) & 0x03) << 16) |
            (static_cast<uint32_t>(data[1]) << 8) |
            static_cast<uint32_t>(data[2]);

        ir =
            ((static_cast<uint32_t>(data[3]) & 0x03) << 16) |
            (static_cast<uint32_t>(data[4]) << 8) |
            static_cast<uint32_t>(data[5]);

        return true;
    }

}