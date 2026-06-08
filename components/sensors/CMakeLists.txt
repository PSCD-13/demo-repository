# Written with AI assistance for the specific question/context. All code has been reviewed and understood before implementation.

idf_component_register(
    SRCS
        "max30102_sensor.cpp"
        "max30205_sensor.cpp"
        "MPU6050.cpp"
        "I2Cdev.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        models
        framework-arduinoespressif32
)