autoneg_add_module(analysis
    SOURCES
        src/base_meter.cpp
        src/frame_detector.cpp
        src/planes.cpp
        src/region_map.cpp
        src/tone.cpp
    PUBLIC_DEPS base model image ports
)
