# Datenhaltung (U-DAT-01..15): echte temporäre Dateien, Prüfsumme als Fake (T-03)
autoneg_add_test(data
    LABEL unit
    SOURCES data_test.cpp
    DEPS adapter_json test_support
)
