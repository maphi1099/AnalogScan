autoneg_add_test(backend_cpu
    LABEL unit
    SOURCES cpu_backend_test.cpp
    DEPS backend_cpu test_reference test_fakes
)
