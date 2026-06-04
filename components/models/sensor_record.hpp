#pragma once

#include <cstdint>

namespace pscd::model
{

    struct sensor_record
    {
        uint32_t record_id = 0;
        uint32_t timestamp_ms = 0;

        bool heart_rate_valid = false;
        uint16_t heart_rate_bpm = 0;

        bool skin_temp_valid = false;
        float skin_temp_c = 0.0f;

        bool ambient_temp_valid = false;
        float ambient_temp_c = 0.0f;

        bool motion_valid = false;
        float accel_x = 0.0f;
        float accel_y = 0.0f;
        float accel_z = 0.0f;

        bool workout_mode = false;
        bool manual_log = false;
        bool panic_pressed = false;

        bool fall_detected = false;
        bool abnormal_heart_rate = false;
    };

}