find_package(OpenImageIO CONFIG REQUIRED)

autoneg_add_module(adapter_oiio
    SOURCES src/oiio_image_io.cpp
    PUBLIC_DEPS ports
    PRIVATE_DEPS OpenImageIO::OpenImageIO
)
