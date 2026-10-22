find_package(TBB CONFIG REQUIRED)
find_package(hwy CONFIG REQUIRED)

autoneg_add_module(backend_cpu
    SOURCES
        src/cpu_backend.cpp
        src/kernels_hwy.cpp
    PUBLIC_DEPS ports
    PRIVATE_DEPS TBB::tbb hwy::hwy hwy::hwy_contrib
)
# Highway findet die Datei für die Mehrfachübersetzung über HWY_TARGET_INCLUDE
target_include_directories(autoneg_backend_cpu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
