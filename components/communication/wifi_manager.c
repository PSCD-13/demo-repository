// Written with AI assistance for adding Wi-Fi scan debugging to the PSCD ESP32 Wi-Fi manager.
// All code has been reviewed, understood, and adapted before implementation.
#include "wifi_manager.h"
#include <string.h>
#include <stdio.h>
#include <time.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_sntp.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "lwip/netdb.h"

#define TAG "WIFI_MANAGER"

// Global state
static bool g_wifi_connected = false;
static bool g_time_synced = false;
static char g_ip_address[16] = {0};
static wifi_status_cb_t g_status_cb = NULL;
static data_provider_cb_t g_data_provider = NULL;
static uint16_t g_server_port = 0;

// NTP Server
#define NTP_SERVER "pool.ntp.org"

// Private function declarations
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void time_sync_notification_cb(struct timeval *tv);
static void tcp_server_task(void *pvParameters);
static void udp_broadcast_task(void *pvParameters);
static void scan_wifi_networks(void);

static const char *wifi_disconnect_reason_to_text(uint8_t reason)
{
    switch (reason)
    {
    case WIFI_REASON_NO_AP_FOUND:
        return "NO_AP_FOUND: SSID not found, wrong band/channel, or AP unavailable";

    case WIFI_REASON_AUTH_FAIL:
        return "AUTH_FAIL: wrong password or incompatible security settings";

    case WIFI_REASON_ASSOC_FAIL:
        return "ASSOC_FAIL: router rejected association";

    case WIFI_REASON_HANDSHAKE_TIMEOUT:
        return "HANDSHAKE_TIMEOUT: WPA handshake failed";

    case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        return "4WAY_HANDSHAKE_TIMEOUT: WPA key exchange failed";

    case WIFI_REASON_BEACON_TIMEOUT:
        return "BEACON_TIMEOUT: AP signal/timing/channel problem";

    default:
        return "Unknown or less common disconnect reason";
    }
}

// Wi-Fi event handler
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        ESP_LOGI(TAG, "Wi-Fi station started. Scan will run before connecting.");
        // Do not call esp_wifi_connect() here while scan debugging is enabled.
        // wifi_manager_init() starts Wi-Fi, runs a blocking scan, then connects manually.
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        wifi_event_sta_disconnected_t *event =
            (wifi_event_sta_disconnected_t *)event_data;

        g_wifi_connected = false;
        snprintf(g_ip_address, sizeof(g_ip_address), "none");

        ESP_LOGW(TAG,
                 "Wi-Fi disconnected. reason=%u (%s)",
                 event->reason,
                 wifi_disconnect_reason_to_text(event->reason));

        if (g_status_cb)
        {
            g_status_cb(false, NULL);
        }

        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        snprintf(g_ip_address, sizeof(g_ip_address), IPSTR, IP2STR(&event->ip_info.ip));
        g_wifi_connected = true;

        ESP_LOGI(TAG, "Wi-Fi connected, IP: %s", g_ip_address);

        if (g_status_cb)
        {
            g_status_cb(true, g_ip_address);
        }

        esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
        esp_sntp_setservername(0, NTP_SERVER);
        esp_sntp_set_time_sync_notification_cb(time_sync_notification_cb);
        esp_sntp_init();
    }
}
// NTP time sync callback
static void time_sync_notification_cb(struct timeval *tv)
{
    ESP_LOGI(TAG, "NTP time synchronized");
    g_time_synced = true;
}

#define REQUEST_CMD "GET_DATA"

// TCP server task - request-response mode
static void tcp_server_task(void *pvParameters)
{
    // Wait for Wi-Fi connection
    while (!g_wifi_connected)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    vTaskDelay(pdMS_TO_TICKS(500));

    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(g_server_port);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        ESP_LOGE(TAG, "TCP socket creation failed");
        vTaskDelete(NULL);
        return;
    }

    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
    {
        ESP_LOGE(TAG, "TCP bind failed");
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }

    if (listen(server_fd, 1) != 0)
    {
        ESP_LOGE(TAG, "TCP listen failed");
        close(server_fd);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "TCP server started on port %d", g_server_port);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    char rx_buffer[64];
    char tx_buffer[256];

    while (1)
    {
        int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

        if (client_fd >= 0)
        {
            ESP_LOGI(TAG, "Client connected");

            // Receive request from client
            int rx_len = recv(client_fd, rx_buffer, sizeof(rx_buffer) - 1, 0);
            if (rx_len > 0)
            {
                rx_buffer[rx_len] = '\0';
                ESP_LOGI(TAG, "Received request: %s", rx_buffer);

                // Check if it's PC registering its IP
                if (strncmp(rx_buffer, "PC_IP:", 6) == 0)
                {
                    // Parse PC_IP:ip:port format
                    char *ip_start = rx_buffer + 6;
                    char *port_start = strchr(ip_start, ':');
                    if (port_start)
                    {
                        *port_start = '\0';
                        wifi_manager_set_pc_ip(ip_start);
                        ESP_LOGI(TAG, "PC registered: %s:%s", ip_start, port_start + 1);
                    }
                }
                // Check if it's a data request
                else if (strncmp(rx_buffer, REQUEST_CMD, strlen(REQUEST_CMD)) == 0)
                {
                    // Call sensor module data provider callback to get data
                    if (g_data_provider)
                    {
                        g_data_provider(tx_buffer, sizeof(tx_buffer));

                        // Send response
                        int sent = send(client_fd, tx_buffer, strlen(tx_buffer), 0);
                        if (sent > 0)
                        {
                            ESP_LOGI(TAG, "Sent response: %s", tx_buffer);
                        }
                    }
                }
                else
                {
                    ESP_LOGW(TAG, "Unknown request: %s", rx_buffer);
                }
            }

            close(client_fd);
            ESP_LOGI(TAG, "Client disconnected");
        }
    }
}

// UDP broadcast task (for auto-discovery)
static void udp_broadcast_task(void *pvParameters)
{
    uint16_t udp_port = (uint16_t)(uintptr_t)pvParameters;

    while (!g_wifi_connected)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0)
    {
        ESP_LOGE(TAG, "UDP socket creation failed");
        vTaskDelete(NULL);
        return;
    }

    int broadcast_enable = 1;
    setsockopt(udp_socket, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));

    struct sockaddr_in broadcast_addr;
    broadcast_addr.sin_family = AF_INET;
    broadcast_addr.sin_port = htons(udp_port);
    broadcast_addr.sin_addr.s_addr = inet_addr("255.255.255.255");

    char broadcast_msg[64];

    while (1)
    {
        if (g_wifi_connected)
        {
            snprintf(broadcast_msg, sizeof(broadcast_msg), "ESP32_HEALTH:%s:%d", g_ip_address, g_server_port);
            sendto(udp_socket, broadcast_msg, strlen(broadcast_msg), 0,
                   (struct sockaddr *)&broadcast_addr, sizeof(broadcast_addr));
            ESP_LOGI(TAG, "UDP broadcast: %s", broadcast_msg);
        }
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    close(udp_socket);
    vTaskDelete(NULL);
}

// Public function implementations

bool wifi_manager_init(const wifi_manager_config_t *config, wifi_status_cb_t status_cb)
{
    g_status_cb = status_cb;

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }

    // Initialize network
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create Wi-Fi STA
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    // Allow Dutch 2.4 GHz channels while debugging Ziggo networks.
    wifi_country_t country = {
        .cc = "NL",
        .schan = 1,
        .nchan = 13,
        .policy = WIFI_COUNTRY_POLICY_MANUAL};
    ESP_ERROR_CHECK(esp_wifi_set_country(&country));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));

    strncpy((char *)wifi_config.sta.ssid, config->ssid, sizeof(wifi_config.sta.ssid) - 1);
    strncpy((char *)wifi_config.sta.password, config->password, sizeof(wifi_config.sta.password) - 1);

    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;
    wifi_config.sta.channel = 0;
    wifi_config.sta.bssid_set = false;

    ESP_LOGI(TAG, "Configured SSID='%s', length=%u",
             config->ssid,
             (unsigned)strlen(config->ssid));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));

    // Give the Wi-Fi driver a short moment after start, then scan what the ESP32 can see.
    vTaskDelay(pdMS_TO_TICKS(500));
    scan_wifi_networks();

    ESP_LOGI(TAG, "Connecting to configured SSID: '%s'", config->ssid);
    ESP_ERROR_CHECK(esp_wifi_connect());

    // Set timezone (Netherlands/Central European Time CET/CEST)
    setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
    tzset();

    ESP_LOGI(TAG, "Wi-Fi manager initialized, connecting to %s...", config->ssid);
    return true;
}

bool wifi_manager_start_server(uint16_t port, data_provider_cb_t provider_cb)
{
    g_server_port = port;
    g_data_provider = provider_cb;

    if (xTaskCreate(tcp_server_task, "tcp_server", 4096, NULL, 5, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create TCP server task");
        return false;
    }
    return true;
}

bool wifi_manager_start_discovery(uint16_t port)
{
    if (xTaskCreate(udp_broadcast_task, "udp_broadcast", 4096, (void *)(uintptr_t)port, 5, NULL) != pdPASS)
    {
        ESP_LOGE(TAG, "Failed to create UDP broadcast task");
        return false;
    }
    return true;
}

bool wifi_manager_is_connected(void)
{
    return g_wifi_connected;
}

void wifi_manager_get_ip(char *buffer, size_t buffer_size)
{
    if (buffer && buffer_size > 0)
    {
        strncpy(buffer, g_ip_address, buffer_size - 1);
        buffer[buffer_size - 1] = '\0';
    }
}

bool wifi_manager_wait_for_time_sync(uint32_t timeout_ms)
{
    uint32_t elapsed = 0;
    while (!g_time_synced && elapsed < timeout_ms)
    {
        vTaskDelay(pdMS_TO_TICKS(100));
        elapsed += 100;
    }
    return g_time_synced;
}

void wifi_manager_get_time_string(char *buffer, size_t buffer_size)
{
    if (buffer && buffer_size > 0)
    {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(buffer, buffer_size, "%d-%m-%Y %H:%M:%S", &timeinfo);
    }
}

// PC IP storage
static char g_pc_ip[16] = {0};

// Set PC IP address
void wifi_manager_set_pc_ip(const char *pc_ip)
{
    if (pc_ip)
    {
        strncpy(g_pc_ip, pc_ip, sizeof(g_pc_ip) - 1);
        g_pc_ip[sizeof(g_pc_ip) - 1] = '\0';
        ESP_LOGI(TAG, "PC IP set to: %s", g_pc_ip);
    }
}

// Notify PC that data is ready (button trigger)
bool wifi_manager_notify_data_ready(const char *pc_ip, uint16_t pc_port)
{
    if (!g_wifi_connected)
    {
        return false;
    }

    const char *target_ip = (pc_ip && strlen(pc_ip) > 0) ? pc_ip : g_pc_ip;
    if (strlen(target_ip) == 0)
    {
        ESP_LOGE(TAG, "PC IP not set");
        return false;
    }

    int udp_socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udp_socket < 0)
    {
        ESP_LOGE(TAG, "UDP socket creation failed");
        return false;
    }

    struct sockaddr_in pc_addr;
    pc_addr.sin_family = AF_INET;
    pc_addr.sin_port = htons(pc_port);
    inet_pton(AF_INET, target_ip, &pc_addr.sin_addr);

    const char *notify_msg = "ESP32_DATA_READY";
    int sent = sendto(udp_socket, notify_msg, strlen(notify_msg), 0,
                      (struct sockaddr *)&pc_addr, sizeof(pc_addr));
    close(udp_socket);

    if (sent > 0)
    {
        ESP_LOGI(TAG, "Data ready notification sent to %s:%d", target_ip, pc_port);
        return true;
    }

    return false;
}
static void scan_wifi_networks(void)
{
    ESP_LOGI(TAG, "Scanning Wi-Fi networks...");

    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    esp_err_t err = esp_wifi_scan_start(&scan_config, true);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Wi-Fi scan failed: %s", esp_err_to_name(err));
        return;
    }

    uint16_t ap_count = 0;
    err = esp_wifi_scan_get_ap_num(&ap_count);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get AP count: %s", esp_err_to_name(err));
        return;
    }

    ESP_LOGI(TAG, "ESP32 found %u Wi-Fi networks", ap_count);

    if (ap_count == 0)
    {
        return;
    }

    uint16_t max_records = ap_count;
    if (max_records > 30)
    {
        max_records = 30;
    }

    wifi_ap_record_t *ap_records =
        (wifi_ap_record_t *)calloc(max_records, sizeof(wifi_ap_record_t));

    if (ap_records == NULL)
    {
        ESP_LOGE(TAG, "Failed to allocate memory for Wi-Fi scan results");
        return;
    }

    err = esp_wifi_scan_get_ap_records(&max_records, ap_records);
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Failed to get AP records: %s", esp_err_to_name(err));
        free(ap_records);
        return;
    }

    for (int i = 0; i < max_records; i++)
    {
        ESP_LOGI(TAG,
                 "SSID='%s' | RSSI=%d | channel=%u | authmode=%d",
                 (char *)ap_records[i].ssid,
                 ap_records[i].rssi,
                 ap_records[i].primary,
                 ap_records[i].authmode);
    }

    free(ap_records);
}