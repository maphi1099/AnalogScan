find_package(xxHash CONFIG REQUIRED)

autoneg_add_module(adapter_xxhash
    SOURCES xx_hasher.cpp
    PUBLIC_DEPS ports
    PRIVATE_DEPS xxHash::xxhash
)
