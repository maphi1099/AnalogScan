find_package(TBB CONFIG REQUIRED)
find_package(hwy CONFIG REQUIRED)
find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc photo)

autoneg_add_module(backend_cpu
    SOURCES
        cpu_backend.cpp
        kernels_hwy.cpp
    PUBLIC_DEPS ports
    PRIVATE_DEPS TBB::tbb hwy::hwy hwy::hwy_contrib
)
# Highway findet die Datei für die Mehrfachübersetzung über HWY_TARGET_INCLUDE
target_include_directories(autoneg_backend_cpu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# Standardfunktionen (Unschärfe, Rauschminderung, Drehen, Verkleinern) aus OpenCV (D-21);
# OpenCV-Ziele ohne Namensraum, deshalb direkt linken (wie adapter_opencv)
target_link_libraries(autoneg_backend_cpu PRIVATE opencv_core opencv_imgproc opencv_photo)
