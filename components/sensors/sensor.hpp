#pragma once

#include "driver/i2c.h"

namespace pscd::sensors {

class sensor {
public:
    virtual ~sensor() = default;

    virtual bool begin(i2c_port_t i2c_port) = 0;
    virtual void end() = 0;
    virtual bool is_ready() const = 0;

    virtual const char* name() const = 0;
};

}