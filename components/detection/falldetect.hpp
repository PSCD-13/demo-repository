#ifndef FALLDETECT_HPP
#define FALLDETECT_HPP

#include "../sensors/MPU6050.h"

#define MPU6050_ADDRESS 0x68

namespace pscd::detection
{
    struct accelgyro_record
    {
        int16_t ax, ay, az;
        int16_t gx, gy, gz;
    };

    class FallDetect
    {
    public:
        bool initialize();
        accelgyro_record getRaw();
        bool isFalling();
        void updateData();

    private:
        MPU6050 mpu = MPU6050(MPU6050_ADDRESS);
        accelgyro_record data;
    };
}

#endif