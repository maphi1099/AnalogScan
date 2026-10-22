# Skalare Referenzimplementierungen der Rechenkernels und ein Backend daraus (nur für Tests).
add_library(autoneg_test_reference STATIC
    src/kernels.cpp
    src/reference_backend.cpp)
target_include_directories(autoneg_test_reference PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(autoneg_test_reference PUBLIC autoneg_ports)
autoneg_set_warnings(autoneg_test_reference)
# Die Referenz rechnet große Testbilder (z. B. U-PIP-13 mit 1001 x 777); auch im Debug-Build
# optimiert übersetzen, damit die Unit-Tests unter einer Minute bleiben.
if(AUTONEG_IS_CLANG_CL)
    target_compile_options(autoneg_test_reference PRIVATE $<$<CONFIG:Debug>:/O2>)
else()
    target_compile_options(autoneg_test_reference PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()
