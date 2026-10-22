autoneg_add_test(pipeline
    LABEL unit
    SOURCES
        compiler_test.cpp
        executor_test.cpp
    DEPS pipeline test_steps test_reference test_mocks
)
