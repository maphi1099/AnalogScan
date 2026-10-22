# Skalare Referenzimplementierungen der Rechenkernels und ein Backend daraus (nur für Tests).
add_library(autoneg_test_reference STATIC
    src/kernels.cpp
    src/reference_backend.cpp)
target_include_directories(autoneg_test_reference PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(autoneg_test_reference PUBLIC autoneg_ports)
autoneg_set_warnings(autoneg_test_reference)
