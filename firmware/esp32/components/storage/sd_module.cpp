#include "sd_module.hpp"

#include <cstdio>
#include <cstring>
#include <sys/stat.h>
#include <unistd.h>

#include "esp_log.h"
#include "esp_err.h"
#include "esp_vfs_fat.h"
#include "driver/sdspi_host.h"

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

        ESP_LOGI(TAG, "Starting SD module");

        // 1. Configure SPI bus pins.
        spi_bus_config_t bus_config = {};
        bus_config.mosi_io_num = m_pin_mosi;
        bus_config.miso_io_num = m_pin_miso;
        bus_config.sclk_io_num = m_pin_clk;
        bus_config.quadwp_io_num = -1;
        bus_config.quadhd_io_num = -1;
        bus_config.max_transfer_sz = 1000;

        esp_err_t ret = spi_bus_initialize(
            SPI_HOST_USED,
            &bus_config,
            SDSPI_DEFAULT_DMA);

        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
            return false;
        }

        m_spi_bus_started = true;

        // 2. Configure SD card host.
        sdmmc_host_t host = SDSPI_HOST_DEFAULT();
        host.slot = SPI_HOST_USED;

        // 3. Configure chip select pin.
        sdspi_device_config_t slot_config = SDSPI_DEVICE_CONFIG_DEFAULT();
        slot_config.gpio_cs = static_cast<gpio_num_t>(m_pin_cs);
        slot_config.host_id = SPI_HOST_USED;

        // 4. Configure FatFS mount.
        esp_vfs_fat_sdmmc_mount_config_t mount_config = {};
        mount_config.format_if_mount_failed = m_format_if_mount_failed;
        mount_config.max_files = 5;
        mount_config.allocation_unit_size = 16 * 1024;

        // 5. Mount SD card.
        ret = esp_vfs_fat_sdspi_mount(
            MOUNT_POINT,
            &host,
            &slot_config,
            &mount_config,
            &m_card);

        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "Failed to mount SD card: %s", esp_err_to_name(ret));

            if (m_spi_bus_started)
            {
                spi_bus_free(SPI_HOST_USED);
                m_spi_bus_started = false;
            }

            return false;
        }

        ESP_LOGI(TAG, "SD card mounted");

        if (m_card != nullptr)
        {
            sdmmc_card_print_info(stdout, m_card);
        }

        // 6. Create CSV header if needed.
        if (!write_csv_header_if_needed())
        {
            ESP_LOGE(TAG, "Could not prepare CSV file");
            end();
            return false;
        }

        // 7. Find the last used record id from the CSV file.
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
        if (!m_ready && !m_spi_bus_started)
        {
            return;
        }

        ESP_LOGI(TAG, "Stopping SD module");

        if (m_card != nullptr)
        {
            esp_vfs_fat_sdcard_unmount(MOUNT_POINT, m_card);
            m_card = nullptr;
        }

        if (m_spi_bus_started)
        {
            spi_bus_free(SPI_HOST_USED);
            m_spi_bus_started = false;
        }

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

        // If the controller did not give an id yet, we create one here.
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

        // Keep this very explicit so it is easy to match with the CSV header.
        int written = fprintf(
            file,
            "%lu,%lu,%d,%u,%d,%.2f,%d,%.2f,%d,%.3f,%.3f,%.3f,%d,%d,%d,%d,%d\n",

            static_cast<unsigned long>(record.record_id),
            static_cast<unsigned long>(record.timestamp_ms),

            record.heart_rate_valid ? 1 : 0,
            static_cast<unsigned int>(record.heart_rate_bpm),

            record.skin_temp_valid ? 1 : 0,
            record.skin_temp_c,

            record.ambient_temp_valid ? 1 : 0,
            record.ambient_temp_c,

            record.motion_valid ? 1 : 0,
            record.accel_x,
            record.accel_y,
            record.accel_z,

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

        // Push C buffer to FatFS.
        fflush(file);

        // Try to force it to the SD card.
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
            // This is not a real error.
            // It just means nothing has been sent yet.
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

    bool sd_module::set_last_sent_id(uint32_t record_id)
    {
        if (!m_ready)
        {
            ESP_LOGE(TAG, "Cannot write sync state, SD module is not ready");
            return false;
        }

        char text[64] = {};
        snprintf(
            text,
            sizeof(text),
            "last_sent_id=%lu\n",
            static_cast<unsigned long>(record_id));

        bool ok = write_text_file_safely(SYNC_STATE_FILE, text);

        if (ok)
        {
            ESP_LOGI(TAG, "Updated last_sent_id to %lu", static_cast<unsigned long>(record_id));
        }

        return ok;
    }

    bool sd_module::for_each_unsent_record(
        uint32_t last_sent_id,
        record_callback callback,
        void *user_data)
    {
        if (!m_ready)
        {
            ESP_LOGE(TAG, "Cannot read unsent records, SD module is not ready");
            return false;
        }

        if (callback == nullptr)
        {
            ESP_LOGE(TAG, "Callback is null");
            return false;
        }

        FILE *file = fopen(MEASUREMENTS_FILE, "r");

        if (file == nullptr)
        {
            ESP_LOGE(TAG, "Could not open measurements file");
            return false;
        }

        char line[256] = {};

        // First line is the CSV header.
        // We read and ignore it.
        fgets(line, sizeof(line), file);

        while (fgets(line, sizeof(line), file) != nullptr)
        {
            pscd::model::sensor_record record{};

            if (!parse_csv_line(line, record))
            {
                ESP_LOGW(TAG, "Skipping invalid CSV line");
                continue;
            }

            if (record.record_id <= last_sent_id)
            {
                continue;
            }

            bool callback_ok = callback(record, user_data);

            if (!callback_ok)
            {
                ESP_LOGW(
                    TAG,
                    "Callback failed at record id %lu",
                    static_cast<unsigned long>(record.record_id));

                fclose(file);
                return false;
            }
        }

        fclose(file);
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
            "accel_x,"
            "accel_y,"
            "accel_z,"
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

        // Skip header.
        fgets(line, sizeof(line), file);

        while (fgets(line, sizeof(line), file) != nullptr)
        {
            unsigned long id = 0;

            // The record id is the first value in the line.
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
        float accel_x = 0.0f;
        float accel_y = 0.0f;
        float accel_z = 0.0f;

        int workout_mode = 0;
        int manual_log = 0;
        int panic_pressed = 0;
        int fall_detected = 0;
        int abnormal_heart_rate = 0;

        int amount_found = sscanf(
            line,
            "%lu,%lu,%d,%u,%d,%f,%d,%f,%d,%f,%f,%f,%d,%d,%d,%d,%d",

            &record_id,
            &timestamp_ms,

            &heart_rate_valid,
            &heart_rate_bpm,

            &skin_temp_valid,
            &skin_temp_c,

            &ambient_temp_valid,
            &ambient_temp_c,

            &motion_valid,
            &accel_x,
            &accel_y,
            &accel_z,

            &workout_mode,
            &manual_log,
            &panic_pressed,
            &fall_detected,
            &abnormal_heart_rate);

        if (amount_found != 17)
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
        record.accel_x = accel_x;
        record.accel_y = accel_y;
        record.accel_z = accel_z;

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
        // We write to a temporary file first.
        // Then we rename it to the real file.
        // This is safer than directly overwriting sync_state.txt.

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

        // Remove old file if it exists.
        remove(path);

        int rename_result = rename(SYNC_STATE_TEMP_FILE, path);

        if (rename_result != 0)
        {
            ESP_LOGE(TAG, "Could not rename temp sync file");
            return false;
        }

        return true;
    }

}