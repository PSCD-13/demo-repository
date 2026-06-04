#pragma once

#include <cstdint>

#include "sensor.hpp"
#include "driver/i2c.h"

namespace pscd::sensors
{

    class max30102_sensor : public sensor
    {
    public:
        explicit max30102_sensor(uint8_t i2c_address = 0x57);

        bool begin(i2c_port_t i2c_port) override;
        void end() override;
        bool is_ready() const override;
        const char *name() const override;

        bool read_bpm(uint16_t &bpm);

    private:
        bool write_register(uint8_t reg, uint8_t value);
        bool read_register(uint8_t reg, uint8_t &value);
        bool read_fifo_sample(uint32_t &red, uint32_t &ir);

        bool reset_sensor();
        bool configure_sensor();

    private:
        uint8_t m_i2c_address;
        i2c_port_t m_i2c_port = I2C_NUM_0;
        bool m_ready = false;
    };

}