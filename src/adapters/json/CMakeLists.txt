find_package(nlohmann_json CONFIG REQUIRED)

autoneg_add_module(adapter_json
    SOURCES
        atomic_file_writer.cpp
        file_thumbnail_cache.cpp
        json_codec.cpp
        json_config_store.cpp
        json_sidecar_store.cpp
    PUBLIC_DEPS ports
    PRIVATE_DEPS nlohmann_json::nlohmann_json
)
