# CpuBackend gegen die OpenCV-Standardfunktionen, die es seit D-21 aufruft
find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc photo)
autoneg_add_test(backend_opencv
    LABEL component
    SOURCES opencv_backend_test.cpp
    DEPS backend_cpu
)
target_link_libraries(autoneg_test_backend_opencv PRIVATE opencv_core opencv_imgproc opencv_photo)
