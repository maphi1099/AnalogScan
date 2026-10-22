find_package(lcms2 CONFIG REQUIRED)

autoneg_add_module(adapter_lcms
    SOURCES src/lcms_color_engine.cpp
    PUBLIC_DEPS ports
    PRIVATE_DEPS lcms2::lcms2
)
