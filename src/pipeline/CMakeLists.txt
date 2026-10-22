autoneg_add_module(pipeline
    SOURCES
        src/backend_selector.cpp
        src/compiled_pipeline.cpp
        src/pipeline_compiler.cpp
        src/pipeline_executor.cpp
        src/pixel_program_builder.cpp
        src/source_provider.cpp
        src/stage_cache.cpp
        src/step.cpp
        src/step_registry.cpp
        src/warp_builder.cpp
    PUBLIC_DEPS base model image ports
)
