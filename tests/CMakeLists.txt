find_package(GTest CONFIG REQUIRED)
include(AutoNegTest)

# Unter ThreadSanitizer spinnen die oneTBB-Worker mit allen logischen Kernen so langsam, dass
# Tests nicht fertig werden (BUGS.md B-12). Testprogramme dort auf 8 CPUs begrenzen.
if(AUTONEG_SANITIZER STREQUAL "thread" AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_program(AUTONEG_TASKSET taskset REQUIRED)
    set(CMAKE_TEST_LAUNCHER ${AUTONEG_TASKSET} -c 0-7)
endif()

add_subdirectory(support)
add_subdirectory(mocks)
add_subdirectory(reference)
add_subdirectory(unit)
add_subdirectory(architecture)
