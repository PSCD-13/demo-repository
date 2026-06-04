#pragma once

#include "sensor_record.hpp"

#include <cstddef>
#include <cstdint>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace pscd::communication
{

    struct communication_config
    {
        const char *ssid = nullptr;
        const char *password = nullptr;
        const char *pc_ip = nullptr;

        uint16_t tcp_port = 0;
        uint16_t discovery_port = 0;
        uint16_t pc_notify_port = 0;
    };

    class communication_module
    {
    public:
        explicit communication_module(const communication_config &config);

        bool begin();

        bool is_started() const;
        bool is_connected() const;

        void set_latest_record(const pscd::model::sensor_record &record);

        bool notify_data_ready();
        bool notify_data_ready(const char *pc_ip, uint16_t pc_port);

        void get_time_string(char *buffer, size_t buffer_size) const;

        const char* getPc_ip();
        uint16_t getPc_notify_port();

    private:
        static void data_provider_trampoline(char *buffer, size_t buffer_size);
        static void status_trampoline(bool connected, const char *ip_address);

        void write_latest_record_csv(char *buffer, size_t buffer_size);

        communication_config m_config;

        pscd::model::sensor_record m_latest_record{};
        bool m_has_latest_record = false;

        bool m_started = false;
        bool m_connected = false;

        SemaphoreHandle_t m_mutex = nullptr;
    };

} // namespace pscd::communication