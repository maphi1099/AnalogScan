# Vergleich eigene Kernels gegen OpenCV (Entscheidungsgrundlage, kein CTest)
find_package(OpenCV CONFIG REQUIRED COMPONENTS core imgproc imgcodecs photo)
add_executable(autoneg_cvcompare cvcompare.cpp)
target_link_libraries(autoneg_cvcompare PRIVATE autoneg_adapter_libraw autoneg_backend_cpu opencv_core opencv_imgproc
    opencv_imgcodecs opencv_photo)
autoneg_set_warnings(autoneg_cvcompare)
