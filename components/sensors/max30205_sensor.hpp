#pragma once

#include <cstdint>

#include "sensor.hpp"
#include "driver/i2c.h"

namespace pscd::sensors
{

    class max30205_sensor : public sensor
    {
    public:
        explicit max30205_sensor(uint8_t i2c_address = 0x48);

        bool begin(i2c_port_t i2c_port) override;
        void end() override;
        bool is_ready() const override;
        const char *name() const override;

        bool read_celsius(float &temperature_c);

    private:
        uint8_t m_i2c_address;
        i2c_port_t m_i2c_port = I2C_NUM_0;
        bool m_ready = false;
    };

}