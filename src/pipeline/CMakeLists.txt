autoneg_add_module(pipeline
    SOURCES
        src/compiled_pipeline.cpp
        src/pipeline_compiler.cpp
        src/pixel_program_builder.cpp
        src/step.cpp
        src/step_registry.cpp
        src/warp_builder.cpp
    PUBLIC_DEPS base model image ports
)
