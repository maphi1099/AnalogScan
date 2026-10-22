find_package(GTest CONFIG REQUIRED)
include(AutoNegTest)

# Unter ThreadSanitizer spinnen die oneTBB-Worker mit allen logischen Kernen so langsam, dass
# Tests nicht fertig werden (BUGS.md B-12). Testprogramme dort auf 8 CPUs begrenzen.
if(AUTONEG_SANITIZER STREQUAL "thread" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(AUTONEG_TASKSET taskset REQUIRED)
    # Gutartige Rennen in Fremdcode unterdrücken (tests/tsan.supp, B-19)
    set(CMAKE_TEST_LAUNCHER ${CMAKE_COMMAND} -E env
        "TSAN_OPTIONS=suppressions=${CMAKE_CURRENT_SOURCE_DIR}/tsan.supp" ${AUTONEG_TASKSET} -c 0-7)
endif()

add_subdirectory(support)
add_subdirectory(mocks)
add_subdirectory(reference)
add_subdirectory(unit)
add_subdirectory(component)
add_subdirectory(tools)
add_subdirectory(architecture)
