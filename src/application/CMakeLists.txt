autoneg_add_module(application
    SOURCES
        src/command_bus.cpp
        src/contact_sheet_service.cpp
        src/commands.cpp
        src/jobs.cpp
        src/persistence_queue.cpp
        src/render_service.cpp
        src/roll_service.cpp
        src/services.cpp
        src/thumbnail_service.cpp
    PUBLIC_DEPS base model image ports pipeline steps analysis layout
)
