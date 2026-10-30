autoneg_add_module(analysis
    SOURCES
        base_meter.cpp
        frame_detector.cpp
        planes.cpp
        region_map.cpp
        tone.cpp
    PUBLIC_DEPS base model image ports
)
