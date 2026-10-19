autoneg_add_module(application
    SOURCES src/module.cpp
    PUBLIC_DEPS base model image ports pipeline steps analysis layout
)
