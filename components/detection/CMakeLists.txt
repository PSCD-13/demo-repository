# Written with AI assistance for the specific question/context. All code has been reviewed and understood before implementation.

idf_component_register(
    SRCS
        "AbnormalDetector.cpp"
        "HeartRateProcessor.cpp"
        "falldetect.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        models
        sensors
)