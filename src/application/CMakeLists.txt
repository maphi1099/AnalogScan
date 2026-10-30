autoneg_add_module(application
    SOURCES
        command_bus.cpp
        contact_sheet_service.cpp
        commands.cpp
        jobs.cpp
        persistence_queue.cpp
        presets.cpp
        render_service.cpp
        roll_service.cpp
        services.cpp
        thumbnail_service.cpp
    PUBLIC_DEPS base model image ports pipeline steps analysis layout
)
