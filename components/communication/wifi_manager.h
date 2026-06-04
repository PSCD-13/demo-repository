#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C"
{
#endif

    // Wi-Fi configuration structure
    typedef struct
    {
        const char *ssid;
        const char *password;
    } wifi_manager_config_t;

    // Data provider callback type - sensor module provides data through this callback
    typedef void (*data_provider_cb_t)(char *buffer, size_t buffer_size);

    // Connection status callback type
    typedef void (*wifi_status_cb_t)(bool connected, const char *ip_address);

    /**
     * @brief Initialize Wi-Fi manager
     * @param config Wi-Fi configuration (SSID and password)
     * @param status_cb Connection status callback (can be NULL)
     * @return true on success, false on failure
     */
    bool wifi_manager_init(const wifi_manager_config_t *config, wifi_status_cb_t status_cb);

    /**
     * @brief Start TCP server and wait for client connections
     * @param port TCP port number
     * @param provider_cb Data provider callback function (implemented by sensor module)
     * @return true on success, false on failure
     */
    bool wifi_manager_start_server(uint16_t port, data_provider_cb_t provider_cb);

    /**
     * @brief Start UDP broadcast service (for auto-discovery)
     * @param port UDP broadcast port number
     * @return true on success, false on failure
     */
    bool wifi_manager_start_discovery(uint16_t port);

    /**
     * @brief Check if Wi-Fi is connected
     * @return true if connected, false if not
     */
    bool wifi_manager_is_connected(void);

    /**
     * @brief Get current IP address
     * @param buffer Buffer to store IP address
     * @param buffer_size Buffer size
     */
    void wifi_manager_get_ip(char *buffer, size_t buffer_size);

    /**
     * @brief Wait for NTP time synchronization to complete
     * @param timeout_ms Timeout in milliseconds
     * @return true if sync successful, false if timeout or failure
     */
    bool wifi_manager_wait_for_time_sync(uint32_t timeout_ms);

    /**
     * @brief Get current time string (format: DD-MM-YYYY HH:MM:SS)
     * @param buffer Buffer to store time
     * @param buffer_size Buffer size
     */
    void wifi_manager_get_time_string(char *buffer, size_t buffer_size);

    /**
     * @brief Notify PC that data is ready (call this when button is pressed)
     * @param pc_ip PC IP address (discovered via UDP)
     * @param pc_port PC UDP port for receiving notification
     * @return true on success, false on failure
     */
    bool wifi_manager_notify_data_ready(const char *pc_ip, uint16_t pc_port);

    /**
     * @brief Set PC IP address (called when PC is discovered)
     * @param pc_ip PC IP address string
     */
    void wifi_manager_set_pc_ip(const char *pc_ip);

#ifdef __cplusplus
}
#endif

#endif // WIFI_MANAGER_H