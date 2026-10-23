find_package(spdlog CONFIG REQUIRED)

autoneg_add_module(adapter_spdlog
    SOURCES src/spd_logger.cpp
    PUBLIC_DEPS ports
    PRIVATE_DEPS spdlog::spdlog
)
