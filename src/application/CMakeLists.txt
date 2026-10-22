autoneg_add_module(application
    SOURCES
        src/command_bus.cpp
        src/commands.cpp
        src/jobs.cpp
        src/persistence_queue.cpp
        src/render_service.cpp
        src/services.cpp
    PUBLIC_DEPS base model image ports pipeline steps analysis layout
)
