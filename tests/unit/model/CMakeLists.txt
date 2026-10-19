autoneg_add_test(model
    LABEL unit
    SOURCES
        edit_history_test.cpp
        edit_list_test.cpp
        values_test.cpp
    DEPS model test_support
)
