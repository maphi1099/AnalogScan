# Kern-Pipeline mit allen Schritten und echtem CpuBackend (K-08, K-09, K-10)
autoneg_add_test(pipeline_component
    LABEL component
    SOURCES pipeline_component_test.cpp
    DEPS steps backend_cpu test_synthetic
)
