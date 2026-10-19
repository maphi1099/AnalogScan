# Testprogramme mit GoogleTest/GoogleMock anlegen.
#
#   autoneg_add_test(<name> LABEL unit|component|e2e SOURCES <dateien> DEPS <module>)
#     Legt autoneg_test_<name> an; jeder TEST() wird einzeln in CTest
#     registriert und trägt das Label der Testebene.

include(GoogleTest)

function(autoneg_add_test name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "LABEL" "SOURCES;DEPS")
    if(NOT arg_LABEL MATCHES "^(unit|component|e2e)$")
        message(FATAL_ERROR "autoneg_add_test(${name}): LABEL muss unit, component oder e2e sein")
    endif()
    set(target autoneg_test_${name})
    add_executable(${target} ${arg_SOURCES})
    _autoneg_resolve_deps(deps ${arg_DEPS})
    # Nur gmock_main linken: Unter Windows (vcpkg, DLLs) enthält gmock.dll eine eigene
    # Kopie von gtest. Mit gtest_main zusätzlich gäbe es zwei Test-Registrierungen,
    # und main() fände keine Tests (BUGS.md B-07). gmock_main zieht gmock und gtest nach.
    target_link_libraries(${target} PRIVATE ${deps} GTest::gmock_main)
    target_compile_definitions(${target} PRIVATE
        AUTONEG_TESTDATA_DIR="${AUTONEG_TESTDATA_DIR}")
    autoneg_set_warnings(${target})
    gtest_discover_tests(${target}
        DISCOVERY_MODE PRE_TEST
        PROPERTIES LABELS ${arg_LABEL})
    # Schutz gegen still verschwundene Tests (BUGS.md B-07): schlägt fehl, wenn das
    # Programm keinen einzigen TEST() registriert hat. Führt selbst keinen Test aus.
    add_test(NAME ${target}.linked
        COMMAND ${target} --gtest_fail_if_no_test_linked --gtest_filter=-*)
    set_tests_properties(${target}.linked PROPERTIES LABELS ${arg_LABEL})
endfunction()
