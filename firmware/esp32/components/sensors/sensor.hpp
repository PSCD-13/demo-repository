#pragma once

#include "driver/i2c_master.h"

namespace pscd::sensors {

class sensor {
public:
    virtual ~sensor() = default;

    virtual bool begin(i2c_master_bus_handle_t i2c_bus) = 0;
    virtual void end() = 0;
    virtual bool is_ready() const = 0;

    virtual const char* name() const = 0;
};

}