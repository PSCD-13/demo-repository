# Written with AI assistance for the specific question/context. All code has been reviewed and understood before implementation.

idf_component_register(
    SRCS
        "display_module.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        models
        framework-arduinoespressif32
)

set(PIO_LIBDEPS_DIR "${CMAKE_SOURCE_DIR}/.pio/libdeps/sparkfun_esp32s2_thing_plus_c")

target_include_directories(${COMPONENT_LIB} PRIVATE
    "${PIO_LIBDEPS_DIR}/TFT_eSPI"
    "${PIO_LIBDEPS_DIR}/TFT_eWidget/src"
    "${PIO_LIBDEPS_DIR}/TFT_eWidget/src/widgets/graph"
    "${PIO_LIBDEPS_DIR}/TFT_eWidget/src/widgets/button"
    "${PIO_LIBDEPS_DIR}/TFT_eWidget/src/widgets/meter"
    "${PIO_LIBDEPS_DIR}/TFT_eWidget/src/widgets/slider"
)