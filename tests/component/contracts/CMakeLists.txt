# Vertragstests der Ports gegen Fakes und echte Adapter (K-14)
autoneg_add_test(contracts
    LABEL component
    SOURCES contracts_test.cpp
    DEPS adapter_libraw adapter_oiio adapter_lcms adapter_opencv test_fakes test_synthetic
)
