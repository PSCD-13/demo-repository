#include "MAX30102.hpp"
#include "esp_log.h"
#include <cstring>

static const char *TAG = "MAX30102";

//constructor

MAX30102::MAX30102(i2c_port_t port, gpio_num_t sda, gpio_num_t scl)
    : port_(port), sda_(sda), scl_(scl) {}


MAX30102::~MAX30102() {
    if (initialized_) {
        i2c_driver_delete(port_);
        ESP_LOGI(TAG, "I2C driver released.");
    }
}
 
MAX30102::MAX30102(MAX30102&& other) noexcept
    : port_(other.port_),
      sda_(other.sda_),
      scl_(other.scl_),
      initialized_(other.initialized_)
{
    other.initialized_ = false;
}
 
MAX30102& MAX30102::operator=(MAX30102&& other) noexcept {
    if (this != &other) {
        // Release our current resource before taking the new one
        if (initialized_) {
            i2c_driver_delete(port_);
        }
        port_ = other.port_;
        sda_ = other.sda_;
        scl_ = other.scl_;
        initialized_ = other.initialized_;
        other.initialized_ = false;
    }
    return *this;
}


// public interface

bool MAX30102::begin() {
    i2c_config_t conf = {};
    conf.mode = I2C_MODE_MASTER;
    conf.sda_io_num = sda_;
    conf.scl_io_num = scl_;
    conf.sda_pullup_en = GPIO_PULLUP_ENABLE;
    conf.scl_pullup_en = GPIO_PULLUP_ENABLE;
    conf.master.clk_speed = 400000;

    ESP_ERROR_CHECK(i2c_param_config(port_, &conf));
    ESP_ERROR_CHECK(i2c_driver_install(port_, I2C_MODE_MASTER, 0, 0, 0));
    initialized_ = true;

    // Give the sensor time to power up
    vTaskDelay(pdMS_TO_TICKS(5000));

    const uint8_t id = readReg(REG_PART_ID);
    if (id != 0x15) {
        ESP_LOGE(TAG, "Sensor not found (part_id=0x%02X)", id);
        return false;
    }

    writeReg(REG_MODE_CONFIG, 0x40);   // Software reset
    vTaskDelay(pdMS_TO_TICKS(100));
    writeReg(REG_MODE_CONFIG, 0x03);   // SpO2 + HR mode
    writeReg(REG_SPO2_CONFIG, 0x27);   // 100 Hz, 411 µs pulse, 18-bit ADC
    writeReg(REG_LED1_PA,     0x24);   // IR LED current
    writeReg(REG_LED2_PA,     0x24);   // Red LED current

    ESP_LOGI(TAG, "Sensor ready. Place finger flat on sensor and hold still.");
    return true;
}

int MAX30102::availableSamples() {
    const int wr = readReg(REG_FIFO_WR_PTR);
    const int rd = readReg(REG_FIFO_RD_PTR);
    return (wr - rd + 32) % 32;
}

void MAX30102::readFIFO(int &ir, int &red) {
    uint8_t d[6] = {};

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, REG_FIFO_DATA, true);
    i2c_master_start(cmd);   // Repeated start
    i2c_master_write_byte(cmd, (ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read(cmd, d, 5, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, &d[5], I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(port_, cmd, I2C_TIMEOUT);
    i2c_cmd_link_delete(cmd);

    ir = ((d[0] << 16) | (d[1] << 8) | d[2]) & 0x3FFFF;
    red = ((d[3] << 16) | (d[4] << 8) | d[5]) & 0x3FFFF;
}

// private helpers

void MAX30102::writeReg(uint8_t reg, uint8_t val) {
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write_byte(cmd, val, true);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(port_, cmd, I2C_TIMEOUT);
    i2c_cmd_link_delete(cmd);
}

uint8_t MAX30102::readReg(uint8_t reg) {
    uint8_t data = 0;
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);   // Repeated start
    i2c_master_write_byte(cmd, (ADDR << 1) | I2C_MASTER_READ, true);
    i2c_master_read_byte(cmd, &data, I2C_MASTER_LAST_NACK);
    i2c_master_stop(cmd);
    i2c_master_cmd_begin(port_, cmd, I2C_TIMEOUT);
    i2c_cmd_link_delete(cmd);
    return data;
}
