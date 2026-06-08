// Written with AI assistance for the specific question/context. All code has been reviewed and understood before implementation.

#include "sd_module.hpp"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#include <Arduino.h>
#include <SPI.h>
#include <SD.h>

#include "esp_log.h"

static const char *TAG = "sd_module";

namespace pscd::storage
{

    sd_module::sd_module(
        int pin_mosi,
        int pin_miso,
        int pin_clk,
        int pin_cs,
        bool format_if_mount_failed)
        : m_pin_mosi(pin_mosi),
          m_pin_miso(pin_miso),
          m_pin_clk(pin_clk),
          m_pin_cs(pin_cs),
          m_format_if_mount_failed(format_if_mount_failed)
    {
    }

    sd_module::~sd_module()
    {
        end();
    }

    bool sd_module::begin()
    {
        if (m_ready)
        {
            ESP_LOGW(TAG, "SD module is already started");
            return true;
        }

        ESP_LOGI(TAG, "Starting SD module using Arduino SD/SPI");

        pinMode(m_pin_cs, OUTPUT);
        digitalWrite(m_pin_cs, HIGH);
        SPI.begin(m_pin_clk, m_pin_miso, m_pin_mosi, m_pin_cs);

        const bool mounted = SD.begin(
            static_cast<uint8_t>(m_pin_cs),
            SPI,
            20000000,
            MOUNT_POINT,
            5,
            m_format_if_mount_failed);

        if (!mounted)
        {
            ESP_LOGE(TAG, "Failed to mount SD card using Arduino SD");
            m_ready = false;
            return false;
        }

        ESP_LOGI(TAG, "SD card mounted");
        ESP_LOGI(TAG, "SD card size: %llu MB", static_cast<unsigned long long>(SD.cardSize() / (1024ULL * 1024ULL)));

        if (!write_csv_header_if_needed())
        {
            ESP_LOGE(TAG, "Could not prepare CSV file");
            end();
            return false;
        }

        if (!load_last_record_id_from_csv())
        {
            ESP_LOGE(TAG, "Could not read last record id");
            end();
            return false;
        }

        m_ready = true;

        ESP_LOGI(TAG, "SD module ready. Last record id: %lu", static_cast<unsigned long>(m_last_record_id));
        return true;
    }

    void sd_module::end()
    {
        if (!m_ready)
        {
            return;
        }

        ESP_LOGI(TAG, "Stopping SD module");
        SD.end();
        m_ready = false;
    }

    bool sd_module::is_ready() const
    {
        return m_ready;
    }

    bool sd_module::append_record(pscd::model::sensor_record &record)
    {
        if (!m_ready)
        {
            ESP_LOGE(TAG, "Cannot append record, SD module is not ready");
            return false;
        }

        if (record.record_id == 0)
        {
            record.record_id = m_last_record_id + 1;
        }

        FILE *file = fopen(MEASUREMENTS_FILE, "a");

        if (file == nullptr)
        {
            ESP_LOGE(TAG, "Could not open measurements file for appending");
            return false;
        }

        int written = fprintf(
            file,
            "%lu,%lu,%d,%u,%d,%.2f,%d,%.2f,%d,%d,%d,%d,%d,%d\n",

            static_cast<unsigned long>(record.record_id),
            static_cast<unsigned long>(record.timestamp_ms),

            record.heart_rate_valid ? 1 : 0,
            static_cast<unsigned int>(record.heart_rate_bpm),

            record.skin_temp_valid ? 1 : 0,
            record.skin_temp_c,

            record.ambient_temp_valid ? 1 : 0,
            record.ambient_temp_c,

            record.motion_valid ? 1 : 0,

            record.workout_mode ? 1 : 0,
            record.manual_log ? 1 : 0,
            record.panic_pressed ? 1 : 0,
            record.fall_detected ? 1 : 0,
            record.abnormal_heart_rate ? 1 : 0);

        if (written < 0)
        {
            ESP_LOGE(TAG, "Failed to write CSV row");
            fclose(file);
            return false;
        }

        fflush(file);

        int fd = fileno(file);
        if (fd >= 0)
        {
            fsync(fd);
        }

        fclose(file);

        if (record.record_id > m_last_record_id)
        {
            m_last_record_id = record.record_id;
        }

        ESP_LOGI(TAG, "Stored record id %lu", static_cast<unsigned long>(record.record_id));
        return true;
    }

    bool sd_module::get_last_sent_id(uint32_t &last_sent_id)
    {
        last_sent_id = 0;

        if (!m_ready)
        {
            ESP_LOGE(TAG, "Cannot read sync state, SD module is not ready");
            return false;
        }

        FILE *file = fopen(SYNC_STATE_FILE, "r");

        if (file == nullptr)
        {
            last_sent_id = 0;
            return true;
        }

        char line[64] = {};

        if (fgets(line, sizeof(line), file) == nullptr)
        {
            fclose(file);
            last_sent_id = 0;
            return true;
        }

        fclose(file);

        unsigned long id = 0;

        int found = sscanf(line, "last_sent_id=%lu", &id);

        if (found == 1)
        {
            last_sent_id = static_cast<uint32_t>(id);
            return true;
        }

        ESP_LOGW(TAG, "sync_state.txt has unexpected contents, using 0");
        last_sent_id = 0;
        return true;
    }

    uint32_t sd_module::get_last_record_id() const
    {
        return m_last_record_id;
    }

    bool sd_module::write_csv_header_if_needed()
    {
        if (file_exists_and_not_empty(MEASUREMENTS_FILE))
        {
            return true;
        }

        FILE *file = fopen(MEASUREMENTS_FILE, "w");

        if (file == nullptr)
        {
            ESP_LOGE(TAG, "Could not create measurements.csv");
            return false;
        }

        fprintf(
            file,
            "record_id,"
            "timestamp_ms,"
            "heart_rate_valid,"
            "heart_rate_bpm,"
            "skin_temp_valid,"
            "skin_temp_c,"
            "ambient_temp_valid,"
            "ambient_temp_c,"
            "motion_valid,"
            "workout_mode,"
            "manual_log,"
            "panic_pressed,"
            "fall_detected,"
            "abnormal_heart_rate\n");

        fflush(file);

        int fd = fileno(file);
        if (fd >= 0)
        {
            fsync(fd);
        }

        fclose(file);

        ESP_LOGI(TAG, "Created measurements.csv with header");
        return true;
    }

    bool sd_module::file_exists_and_not_empty(const char *path)
    {
        struct stat file_stat = {};

        if (stat(path, &file_stat) != 0)
        {
            return false;
        }

        return file_stat.st_size > 0;
    }

    bool sd_module::load_last_record_id_from_csv()
    {
        m_last_record_id = 0;

        FILE *file = fopen(MEASUREMENTS_FILE, "r");

        if (file == nullptr)
        {
            return false;
        }

        char line[256] = {};

        fgets(line, sizeof(line), file);

        while (fgets(line, sizeof(line), file) != nullptr)
        {
            unsigned long id = 0;

            if (sscanf(line, "%lu,", &id) == 1)
            {
                if (id > m_last_record_id)
                {
                    m_last_record_id = static_cast<uint32_t>(id);
                }
            }
        }

        fclose(file);

        return true;
    }

    bool sd_module::parse_csv_line(
        const char *line,
        pscd::model::sensor_record &record)
    {
        unsigned long record_id = 0;
        unsigned long timestamp_ms = 0;

        int heart_rate_valid = 0;
        unsigned int heart_rate_bpm = 0;

        int skin_temp_valid = 0;
        float skin_temp_c = 0.0f;

        int ambient_temp_valid = 0;
        float ambient_temp_c = 0.0f;

        int motion_valid = 0;

        int workout_mode = 0;
        int manual_log = 0;
        int panic_pressed = 0;
        int fall_detected = 0;
        int abnormal_heart_rate = 0;

        int amount_found = sscanf(
            line,
            "%lu,%lu,%d,%u,%d,%f,%d,%f,%d,%d,%d,%d,%d,%d",

            &record_id,
            &timestamp_ms,

            &heart_rate_valid,
            &heart_rate_bpm,

            &skin_temp_valid,
            &skin_temp_c,

            &ambient_temp_valid,
            &ambient_temp_c,

            &motion_valid,

            &workout_mode,
            &manual_log,
            &panic_pressed,
            &fall_detected,
            &abnormal_heart_rate);

        if (amount_found != 14)
        {
            return false;
        }

        record.record_id = static_cast<uint32_t>(record_id);
        record.timestamp_ms = static_cast<uint32_t>(timestamp_ms);

        record.heart_rate_valid = heart_rate_valid != 0;
        record.heart_rate_bpm = static_cast<uint16_t>(heart_rate_bpm);

        record.skin_temp_valid = skin_temp_valid != 0;
        record.skin_temp_c = skin_temp_c;

        record.ambient_temp_valid = ambient_temp_valid != 0;
        record.ambient_temp_c = ambient_temp_c;

        record.motion_valid = motion_valid != 0;

        record.workout_mode = workout_mode != 0;
        record.manual_log = manual_log != 0;
        record.panic_pressed = panic_pressed != 0;
        record.fall_detected = fall_detected != 0;
        record.abnormal_heart_rate = abnormal_heart_rate != 0;

        return true;
    }

    bool sd_module::write_text_file_safely(
        const char *path,
        const char *text)
    {
        FILE *file = fopen(SYNC_STATE_TEMP_FILE, "w");

        if (file == nullptr)
        {
            ESP_LOGE(TAG, "Could not open temp sync file");
            return false;
        }

        fprintf(file, "%s", text);

        fflush(file);

        int fd = fileno(file);
        if (fd >= 0)
        {
            fsync(fd);
        }

        fclose(file);

        ::remove(path);

        int rename_result = rename(SYNC_STATE_TEMP_FILE, path);

        if (rename_result != 0)
        {
            ESP_LOGE(TAG, "Could not rename temp sync file");
            return false;
        }

        return true;
    }

} // namespace pscd::storage
