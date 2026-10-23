# K-18 / E-49: Leistungsmessung mit der echten Komposition (Release-Bau, kein CTest)
add_executable(autoneg_bench bench.cpp ${PROJECT_SOURCE_DIR}/apps/shared/composition.cpp)
target_include_directories(autoneg_bench PRIVATE ${PROJECT_SOURCE_DIR}/apps/shared/include)
target_link_libraries(autoneg_bench PRIVATE autoneg_application autoneg_adapter_libraw autoneg_adapter_oiio
    autoneg_adapter_lcms autoneg_adapter_opencv autoneg_adapter_json autoneg_adapter_xxhash autoneg_adapter_spdlog
    autoneg_adapter_tbb autoneg_backend_cpu autoneg_test_synthetic)
if(WIN32)
    target_link_libraries(autoneg_bench PRIVATE psapi)
endif()
autoneg_set_warnings(autoneg_bench)
