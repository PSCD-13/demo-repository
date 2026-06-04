# Written with AI assistance for the specific question/context. All code has been reviewed and understood before implementation.

idf_component_register(
    SRCS
        "sd_module.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        models
        sdmmc
        fatfs
        driver
)