#pragma once

#include "driver/i2c.h"
#include "driver/gpio.h"

#include "sensor_record.hpp"
#include "sd_module.hpp"

#include "max30205_sensor.hpp"
#include "max30102_sensor.hpp"

#include "communication_module.hpp"

#include "display_module.hpp"

namespace pscd::app
{

    class system_controller
    {
    public:
        system_controller();

        bool begin();
        void run();

        void update_once();

    private:
        bool start_i2c_bus();
        void stop_i2c_bus();

        void fill_timestamp(pscd::model::sensor_record &record);

        void read_temperature(pscd::model::sensor_record &record);
        void read_heart_rate(pscd::model::sensor_record &record);

        void fill_display(pscd::model::sensor_record &record);

        void save_record(pscd::model::sensor_record &record);
        void print_record(const pscd::model::sensor_record &record);

    private:
        // Shared I2C bus for all I2C sensors.
        i2c_port_t m_i2c_port = I2C_NUM_0;
        bool m_i2c_ok = false;

        // Modules
        pscd::storage::sd_module m_sd;
        pscd::sensors::max30205_sensor m_temperature_sensor;
        pscd::sensors::max30102_sensor m_heart_rate_sensor;
        pscd::communication::communication_module m_communication;
        pscd::display::display_module m_display;


        // Module status
        bool m_sd_ok = false;
        bool m_temperature_ok = false;
        bool m_heart_rate_ok = false;
        bool m_communication_ok = false;

        // Default I2C pins
        gpio_num_t m_i2c_sda = GPIO_NUM_21;
        gpio_num_t m_i2c_scl = GPIO_NUM_22;
    };

}