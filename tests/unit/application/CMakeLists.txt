autoneg_add_test(application
    LABEL unit
    SOURCES commands_test.cpp jobs_test.cpp services_test.cpp
    DEPS application test_fakes test_support
)
