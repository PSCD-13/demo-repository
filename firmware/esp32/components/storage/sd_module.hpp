#pragma once

#include <cstdint>

#include "driver/gpio.h"
#include "driver/spi_common.h"
#include "sdmmc_cmd.h"

#include "sensor_record.hpp"

namespace pscd::storage
{

    class sd_module
    {
    public:
        // Callback type used when reading unsent records.
        // Return true if sending/handling the record worked.
        // Return false if it failed and the loop should stop.
        using record_callback = bool (*)(const pscd::model::sensor_record &record, void *user_data);

        // Default SPI pins.
        // Change these if your wiring is different.
        sd_module(
            int pin_mosi = 23,
            int pin_miso = 19,
            int pin_clk = 18,
            int pin_cs = 5,
            bool format_if_mount_failed = false);

        ~sd_module();

        // Do not copy this class. It owns the SD card connection.
        sd_module(const sd_module &) = delete;
        sd_module &operator=(const sd_module &) = delete;

        // Mount SD card and prepare files.
        bool begin();

        // Unmount SD card.
        void end();

        bool is_ready() const;

        // Stores a record in measurements.csv.
        // If record.record_id == 0, this function gives it the next id.
        bool append_record(pscd::model::sensor_record &record);

        // Reads last successfully sent record id from sync_state.txt.
        // If the file does not exist yet, last_sent_id becomes 0.
        bool get_last_sent_id(uint32_t &last_sent_id);

        // Writes last successfully sent record id to sync_state.txt.
        // Call this only after Wi-Fi/server confirms the record was received.
        bool set_last_sent_id(uint32_t record_id);

        // Reads measurements.csv and gives every unsent record to a callback.
        // Usually the callback will send the record over Wi-Fi.
        bool for_each_unsent_record(
            uint32_t last_sent_id,
            record_callback callback,
            void *user_data);

        // Useful for debugging.
        uint32_t get_last_record_id() const;

    private:
        bool write_csv_header_if_needed();
        bool file_exists_and_not_empty(const char *path);
        bool load_last_record_id_from_csv();

        bool parse_csv_line(
            const char *line,
            pscd::model::sensor_record &record);

        bool write_text_file_safely(
            const char *path,
            const char *text);

    private:
        int m_pin_mosi;
        int m_pin_miso;
        int m_pin_clk;
        int m_pin_cs;

        bool m_format_if_mount_failed = false;
        bool m_ready = false;
        bool m_spi_bus_started = false;

        sdmmc_card_t *m_card = nullptr;

        uint32_t m_last_record_id = 0;

        static constexpr spi_host_device_t SPI_HOST_USED = SPI2_HOST;

        static constexpr const char *MOUNT_POINT = "/sdcard";
        static constexpr const char *MEASUREMENTS_FILE = "/sdcard/measurements.csv";
        static constexpr const char *SYNC_STATE_FILE = "/sdcard/sync_state.txt";
        static constexpr const char *SYNC_STATE_TEMP_FILE = "/sdcard/sync_state.tmp";
    };

}