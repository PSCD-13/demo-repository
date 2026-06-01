#include "wifi_manager.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "freertos/FreeRTOS.h"

// Wi-Fi Configuration
#define WIFI_SSID     "ACSlab"
#define WIFI_PASSWORD "lab@ACS24"
#define TCP_PORT      8888
#define UDP_PORT      9999

// Sensor data provider callback - implemented by other modules to provide data
void sensor_data_provider(char* buffer, size_t buffer_size) {
    // Body temperature: 36.0 - 37.5
    float bodyTemp = 36.0f + (rand() % 16) / 10.0f;
    // External temperature: 20.0 - 30.0
    float externalTemp = 20.0f + (rand() % 100) / 10.0f;
    // Heart rate: 60 - 100
    int heartRate = 60 + rand() % 41;

    // Get current time
    char time_str[20];
    wifi_manager_get_time_string(time_str, sizeof(time_str));

    // Format: bodyTemp,externalTemp,time,heartRate
    snprintf(buffer, buffer_size, "%.1f,%.1f,%s,%d",
             bodyTemp, externalTemp, time_str, heartRate);
}

// Wi-Fi status callback
void wifi_status_handler(bool connected, const char* ip_address) {
    if (connected) {
        printf("Wi-Fi connected, IP: %s\n", ip_address);
    } else {
        printf("Wi-Fi disconnected\n");
    }
}

extern "C" void app_main() {
    // Initialize random seed
    srand(time(NULL));

    // Wi-Fi configuration
    wifi_manager_config_t wifi_cfg = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD
    };

    // 1. Initialize Wi-Fi manager
    wifi_manager_init(&wifi_cfg, wifi_status_handler);

    // 2. Wait for Wi-Fi connection
    printf("Waiting for Wi-Fi connection...\n");
    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    // 3. Wait for NTP time sync (max 10 seconds)
    printf("Waiting for NTP time synchronization...\n");
    wifi_manager_wait_for_time_sync(10000);

    // 4. Start UDP broadcast service (for PC auto-discovery)
    wifi_manager_start_discovery(UDP_PORT);

    // 5. Start TCP server with sensor data callback
    wifi_manager_start_server(TCP_PORT, sensor_data_provider);

    printf("System initialization complete, waiting for client connection...\n");

    // ============================================
    // Easy Call API for button module integration
    // ============================================
    // Other modules should implement button detection like this:
    //
    // while (1) {
    //     bool button_pressed = check_button_pressed();  // Button module function
    //     if (button_pressed) {
    //         bool success = wifi_manager_notify_data_ready(NULL, 9998);
    //         if (success) {
    //             printf("Data ready notification sent to PC\n");
    //         }
    //         vTaskDelay(pdMS_TO_TICKS(500));  // Debounce delay
    //     }
    //     vTaskDelay(pdMS_TO_TICKS(100));  // Polling interval
    // }
    //
    // Note: check_button_pressed() is implemented by the button module,
    // not by this Wi-Fi module.

    // Main loop - Wi-Fi module keeps running
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        wifi_manager_notify_data_ready(NULL, 9998);
    }
}
