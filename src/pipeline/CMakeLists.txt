autoneg_add_module(pipeline
    SOURCES
        backend_selector.cpp
        compiled_pipeline.cpp
        pipeline_compiler.cpp
        pipeline_executor.cpp
        pixel_program_builder.cpp
        source_provider.cpp
        stage_cache.cpp
        step.cpp
        step_registry.cpp
        warp_builder.cpp
    PUBLIC_DEPS base model image ports
)
