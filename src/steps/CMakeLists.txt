autoneg_add_module(steps
    SOURCES
        multigrade.cpp
        other_steps.cpp
        pointwise_steps.cpp
        registry.cpp
    PUBLIC_DEPS base model image ports pipeline
)
