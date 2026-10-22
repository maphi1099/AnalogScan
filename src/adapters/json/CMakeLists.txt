find_package(nlohmann_json CONFIG REQUIRED)

autoneg_add_module(adapter_json
    SOURCES
        src/atomic_file_writer.cpp
        src/json_codec.cpp
        src/json_config_store.cpp
        src/json_sidecar_store.cpp
    PUBLIC_DEPS ports
    PRIVATE_DEPS nlohmann_json::nlohmann_json
)
