# K-16: Nebenläufigkeit unter Last (Dauer über AUTONEG_STRESS_SECONDS)
autoneg_add_test(stress
    LABEL component
    SOURCES stress_test.cpp ${PROJECT_SOURCE_DIR}/apps/autoneg/composition.cpp
    DEPS base model image layout ports pipeline steps analysis application adapter_libraw adapter_oiio
         adapter_lcms adapter_opencv adapter_json adapter_xxhash adapter_spdlog adapter_tbb backend_cpu
         test_synthetic
)
target_include_directories(autoneg_test_stress PRIVATE ${PROJECT_SOURCE_DIR}/apps)
