find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc)

autoneg_add_module(adapter_opencv
    SOURCES src/opencv_vision.cpp
    PUBLIC_DEPS ports
)
# OpenCV-Ziele haben keinen Namensraum (opencv_core); autoneg_add_module würde sie als
# eigene Module (autoneg_opencv_core) deuten, deshalb direkt linken.
target_link_libraries(autoneg_adapter_opencv PRIVATE opencv_core opencv_imgproc)
