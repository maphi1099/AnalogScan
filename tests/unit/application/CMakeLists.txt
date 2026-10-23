autoneg_add_test(application
    LABEL unit
    SOURCES commands_test.cpp contact_sheet_test.cpp jobs_test.cpp roll_thumbnail_test.cpp services_test.cpp
    DEPS application test_fakes test_mocks test_support
)
