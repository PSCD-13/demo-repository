#include "communication_module.hpp"

extern "C"
{
#include "wifi_manager.h"
}

#include "esp_log.h"

#include <cstdio>
#include <cstring>

namespace pscd::communication
{

    static const char *TAG = "COMM_MODULE";

    // The old C wifi_manager is global-state based, so this wrapper is also
    // effectively single-instance for now.
    static communication_module *s_instance = nullptr;

    communication_module::communication_module(const communication_config &config)
        : m_config(config)
    {
        m_mutex = xSemaphoreCreateMutex();
    }

    //accessosrsresrsrsrs

    const char* communication_module::getPc_ip()
    {
        return this->m_config.pc_ip;
    }

    uint16_t communication_module::getPc_notify_port()
    {
        return this->m_config.pc_notify_port;
    }

    bool communication_module::begin()
    {
        if (m_started)
        {
            return true;
        }

        if (m_config.ssid == nullptr || m_config.password == nullptr)
        {
            ESP_LOGE(TAG, "Missing Wi-Fi SSID/password");
            return false;
        }

        s_instance = this;

        wifi_manager_config_t wifi_config = {};
        wifi_config.ssid = m_config.ssid;
        wifi_config.password = m_config.password;

        if (!wifi_manager_init(&wifi_config, &communication_module::status_trampoline))
        {
            ESP_LOGE(TAG, "wifi_manager_init failed");
            return false;
        }

        // Start TCP server first, so discovery can advertise the correct TCP port.
        if (!wifi_manager_start_server(
                m_config.tcp_port,
                &communication_module::data_provider_trampoline))
        {
            ESP_LOGE(TAG, "Failed to start TCP server");
            return false;
        }

        if (!wifi_manager_start_discovery(m_config.discovery_port))
        {
            ESP_LOGE(TAG, "Failed to start UDP discovery");
            return false;
        }

        m_started = true;
        ESP_LOGI(TAG, "Communication module started");

        return true;
    }

    bool communication_module::is_started() const
    {
        return m_started;
    }

    bool communication_module::is_connected() const
    {
        return wifi_manager_is_connected();
    }

    void communication_module::set_latest_record(const pscd::model::sensor_record &record)
    {
        if (m_mutex != nullptr)
        {
            xSemaphoreTake(m_mutex, portMAX_DELAY);
        }

        m_latest_record = record;
        m_has_latest_record = true;

        if (m_mutex != nullptr)
        {
            xSemaphoreGive(m_mutex);
        }
    }

    bool communication_module::notify_data_ready()
    {
        return notify_data_ready(nullptr, m_config.pc_notify_port);
    }

    bool communication_module::notify_data_ready(const char *pc_ip, uint16_t pc_port)
    {
        return wifi_manager_notify_data_ready(pc_ip, pc_port);
    }

    void communication_module::get_time_string(char *buffer, size_t buffer_size) const
    {
        wifi_manager_get_time_string(buffer, buffer_size);
    }

    void communication_module::data_provider_trampoline(char *buffer, size_t buffer_size)
    {
        if (s_instance == nullptr)
        {
            snprintf(buffer, buffer_size, "ERROR,no_communication_instance");
            return;
        }

        s_instance->write_latest_record_csv(buffer, buffer_size);
    }

    void communication_module::status_trampoline(bool connected, const char *ip_address)
    {
        if (s_instance != nullptr)
        {
            s_instance->m_connected = connected;
        }

        ESP_LOGI(TAG, "Wi-Fi %s, IP: %s",
                 connected ? "connected" : "disconnected",
                 ip_address != nullptr ? ip_address : "none");
    }

    void communication_module::write_latest_record_csv(char *buffer, size_t buffer_size)
    {
        pscd::model::sensor_record copy{};
        bool has_record = false;

        if (m_mutex != nullptr)
        {
            xSemaphoreTake(m_mutex, portMAX_DELAY);
        }

        copy = m_latest_record;
        has_record = m_has_latest_record;

        if (m_mutex != nullptr)
        {
            xSemaphoreGive(m_mutex);
        }

        if (!has_record)
        {
            snprintf(buffer, buffer_size, "ERROR,no_sensor_record_available");
            return;
        }
        snprintf(
            buffer,
            buffer_size,
            "%lu,%lu,%u,%u,%.2f,%u,%.2f,%u,%u,%u,%u,%u",
            static_cast<unsigned long>(copy.record_id),
            static_cast<unsigned long>(copy.timestamp_ms),

            copy.heart_rate_valid ? 1u : 0u,
            copy.heart_rate_bpm,

            copy.skin_temp_c,
            copy.skin_temp_valid ? 1u : 0u,

            copy.ambient_temp_c,
            copy.ambient_temp_valid ? 1u : 0u,

            copy.workout_mode ? 1u : 0u,
            copy.manual_log ? 1u : 0u,
            copy.panic_pressed ? 1u : 0u,
            copy.fall_detected ? 1u : 0u);
    }

} // namespace pscd::communication