#pragma once
#include <cstdint>
#include "driver/i2c.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

/// @brief class for MAX30102
class MAX30102 {
public:

    /// @brief sensor creator
    /// @param port esp-idf i2c port number
    /// @param sda GPIO number for SDA
    /// @param scl GPIO number for SCL
    MAX30102(i2c_port_t port, gpio_num_t sda, gpio_num_t scl);


    /// @brief destructor, releases the i2c driver if begin() was already called
    ~MAX30102();
 

    /// @brief deletes the option to copy, as a hardware port cannot be shared
    MAX30102(const MAX30102&) = delete;
    MAX30102& operator=(const MAX30102&) = delete;
 

    /// @brief transfers ownership from one MAX30102 object to another
    /// @param other the other MAX30102 object
    MAX30102(MAX30102&& other) noexcept;
    MAX30102& operator=(MAX30102&& other) noexcept;
 

    /// @brief initializes the sensor and its i2c
    /// @return true on success, false if initialization failed
    bool begin();


    /// @brief checks the number of unread samples in the sensors buffer
    /// @return number of unread samples in sensors buffer
    int availableSamples();


    /// @brief reads one sample pair from the sensors buffer
    /// @param ir IR channel value
    /// @param red RED channel value
    void readFIFO(int &ir, int &red);

private:
    i2c_port_t port_;
    gpio_num_t sda_;
    gpio_num_t scl_;
    bool initialized_;

    void writeReg(uint8_t reg, uint8_t val);
    uint8_t readReg(uint8_t reg);

    // Register map
    static constexpr uint8_t ADDR = 0x57;
    static constexpr uint8_t REG_FIFO_WR_PTR = 0x04;
    static constexpr uint8_t REG_FIFO_RD_PTR = 0x06;
    static constexpr uint8_t REG_FIFO_DATA = 0x07;
    static constexpr uint8_t REG_MODE_CONFIG = 0x09;
    static constexpr uint8_t REG_SPO2_CONFIG = 0x0A;
    static constexpr uint8_t REG_LED1_PA = 0x0C;
    static constexpr uint8_t REG_LED2_PA = 0x0D;
    static constexpr uint8_t REG_PART_ID = 0xFF;
    static constexpr TickType_t I2C_TIMEOUT = pdMS_TO_TICKS(100);
};
