find_package(libraw CONFIG REQUIRED)

# raw_r: threadsichere Variante der Bibliothek (Instanzen dürfen in verschiedenen Threads laufen)
autoneg_add_module(adapter_libraw
    SOURCES src/libraw_decoder.cpp
    PUBLIC_DEPS ports
    PRIVATE_DEPS libraw::raw_r
)
