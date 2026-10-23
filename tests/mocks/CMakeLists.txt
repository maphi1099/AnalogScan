# Attrappen der Ports für Unit-Tests (nur Header).
add_library(autoneg_test_mocks INTERFACE)
target_include_directories(autoneg_test_mocks INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(autoneg_test_mocks INTERFACE autoneg_ports GTest::gmock)

# Fakes der Ports für Bilddateien und Farbe (vertragstreu, ohne Fremdbibliothek; K-14).
add_library(autoneg_test_fakes STATIC src/fakes.cpp src/fake_repositories.cpp src/fake_task_executor.cpp src/fake_vision.cpp)
target_include_directories(autoneg_test_fakes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(autoneg_test_fakes PUBLIC autoneg_ports)
autoneg_set_warnings(autoneg_test_fakes)
# Fakes rechnen wie die Referenz auch im Debug-Bau optimiert (Laufzeit der Unit-Tests < 1 min)
if(MSVC)
    target_compile_options(autoneg_test_fakes PRIVATE $<$<CONFIG:Debug>:/O2>)
else()
    target_compile_options(autoneg_test_fakes PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()
