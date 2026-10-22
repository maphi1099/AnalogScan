autoneg_add_module(steps
    SOURCES
        src/multigrade.cpp
        src/other_steps.cpp
        src/pointwise_steps.cpp
        src/registry.cpp
    PUBLIC_DEPS base model image ports pipeline
)
