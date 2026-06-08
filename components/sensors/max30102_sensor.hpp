// Written with AI assistance for the specific question/context. All code has been reviewed and understood before implementation.

#pragma once

#include "driver/gpio.h"
#include "driver/i2c.h"

#include <cstdint>

namespace pscd::sensors
{
    struct max30102_sample
    {
        int ir = 0;
        int red = 0;
    };

    class max30102_sensor
    {
    public:
        explicit max30102_sensor(uint8_t i2c_address = DEFAULT_I2C_ADDRESS);

        // Compatibility constructor for older code that con        bool clear_fifo();tructed the sensor
        // with I2C port and pins. The pins are intentionally not used here,
        // because the system controller owns the shared I2C bus setup.
        max30102_sensor(i2c_port_t i2c_port, gpio_num_t sda_pin, gpio_num_t scl_pin);

        bool begin(i2c_port_t i2c_port);

        // Compatibility begin for older code. Prefer begin(i2c_port_t), like max30205_sensor.
        bool begin();

        void end();
        bool is_ready() const;
        const char *name() const;

        int available_samples();
        bool read_sample(max30102_sample &sample);
        bool read_fifo(int &ir, int &red);

        // Compatibility wrappers for the previous MAX30102 interface.
        int availableSamples();
        void readFIFO(int &ir, int &red);

        bool clear_fifo();

    private:
        static constexpr uint8_t DEFAULT_I2C_ADDRESS = 0x57;
        static constexpr uint8_t EXPECTED_PART_ID = 0x15;

        static constexpr uint8_t REG_FIFO_WR_PTR = 0x04;
        static constexpr uint8_t REG_OVF_COUNTER = 0x05;
        static constexpr uint8_t REG_FIFO_RD_PTR = 0x06;
        static constexpr uint8_t REG_FIFO_DATA = 0x07;
        static constexpr uint8_t REG_MODE_CONFIG = 0x09;
        static constexpr uint8_t REG_SPO2_CONFIG = 0x0A;
        static constexpr uint8_t REG_LED1_PA = 0x0C;
        static constexpr uint8_t REG_LED2_PA = 0x0D;
        static constexpr uint8_t REG_PART_ID = 0xFF;

        static constexpr int FIFO_DEPTH = 32;
        static constexpr int FIFO_VALUE_MASK = 0x3FFFF;

        bool configure_sensor();

        bool write_reg(uint8_t reg, uint8_t value);
        bool read_reg(uint8_t reg, uint8_t &value);

        uint8_t m_i2c_address = DEFAULT_I2C_ADDRESS;
        i2c_port_t m_i2c_port = I2C_NUM_0;
        bool m_ready = false;
    };

} // namespace pscd::sensors

namespace pscd
{
    // Compatibility aliases for older code that used pscd::MAX30102.
    using MAX30102 = pscd::sensors::max30102_sensor;
    using max30102_sample = pscd::sensors::max30102_sample;
}
