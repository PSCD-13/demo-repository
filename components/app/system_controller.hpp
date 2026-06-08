#include "driver/gpio.h"
#include "driver/i2c.h"

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "AbnormalDetector.hpp"
#include "HeartRateProcessor.hpp"
#include "communication_module.hpp"
#include "display_module.hpp"
#include "falldetect.hpp"
#include "max30102_sensor.hpp"
#include "max30205_sensor.hpp"
#include "sd_module.hpp"
#include "sensor_record.hpp"

namespace pscd::app
{

    class system_controller
    {
    public:
        system_controller();

        bool begin();
        void run();

    private:
        static constexpr uint32_t SENSOR_AND_DISPLAY_PERIOD_MS = 100; // 10 Hz.
        static constexpr uint32_t SD_AND_WIFI_PERIOD_MS = 750;

        static constexpr uint32_t LONG_PRESS_MS = 1200;

        static constexpr uint32_t BUZZER_BEEP_ON_MS = 100;
        static constexpr uint32_t BUZZER_BEEP_OFF_MS = 100;
        static constexpr int BUZZER_BEEP_COUNT = 3;

        static constexpr int MODE_BUTTON_PIN = 25;
        static constexpr int PANIC_BUTTON_PIN = 34;

        static constexpr uint8_t ABNORMAL_CONFIRM_NORMAL_MODE = 1;
        static constexpr uint8_t ABNORMAL_CONFIRM_WORKOUT_MODE = 4;

        static void sensor_display_task_entry(void *arg);
        static void sd_wifi_task_entry(void *arg);

        void sensor_display_task();
        void sd_wifi_task();

        bool start_i2c_bus();
        void stop_i2c_bus();

        void read_all_fast_sensors(pscd::model::sensor_record &record);
        void read_temperature(pscd::model::sensor_record &record);
        void read_heart_rate(pscd::model::sensor_record &record);
        void read_motion_and_fall(pscd::model::sensor_record &record);
        void read_buttons(pscd::model::sensor_record &record);
        bool read_active_high_button(int pin) const;

        bool update_abnormal_heart_status(pscd::detection::AlertStatus status);

        void fill_display(pscd::model::sensor_record &record);
        void fill_timestamp(pscd::model::sensor_record &record);
        void print_record(const pscd::model::sensor_record &record);
        bool save_record(pscd::model::sensor_record &record);

        void update_latest_record(const pscd::model::sensor_record &record);
        pscd::model::sensor_record copy_latest_record();

        bool is_emergency(const pscd::model::sensor_record &record) const;
        void update_emergency_edge_and_buzzer(const pscd::model::sensor_record &record);
        void start_buzzer_pattern(uint32_t now_ms);
        void update_buzzer(uint32_t now_ms);
        void stop_buzzer();
        void set_buzzer_output(bool on);

        pscd::storage::sd_module m_sd;
        pscd::communication::communication_module m_communication;
        pscd::display::display_module m_display;

        pscd::sensors::max30205_sensor m_temperature_sensor;
        pscd::MAX30102 m_heart_rate_sensor;

        pscd::detection::FallDetect m_fall_detector;
        pscd::detection::HeartRateProcessor m_heart_rate_processor;
        pscd::detection::AbnormalDetector m_abnormal_detector;

        i2c_port_t m_i2c_port = I2C_NUM_0;
        gpio_num_t m_i2c_sda = GPIO_NUM_21;
        gpio_num_t m_i2c_scl = GPIO_NUM_22;

        bool m_sd_ok = false;
        bool m_i2c_ok = false;
        bool m_temperature_ok = false;
        bool m_heart_rate_ok = false;
        bool m_fall_detect_ok = false;
        bool m_communication_ok = false;

        SemaphoreHandle_t m_record_mutex = nullptr;
        SemaphoreHandle_t m_spi_mutex = nullptr;

        pscd::model::sensor_record m_latest_record{};

        bool m_workout_mode = false;
        bool m_manual_log_pending = false;

        bool m_mode_button_was_down = false;
        bool m_mode_button_long_handled = false;
        uint32_t m_mode_button_press_start_ms = 0;

        uint8_t m_abnormal_confirm_count = 0;

        bool m_buzzer_active = false;
        int m_buzzer_phase = 0;
        uint32_t m_buzzer_next_change_ms = 0;
    };

} // namespace pscd::app