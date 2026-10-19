# Module und Programme anlegen; Abhängigkeitsgraph für die Architekturprüfung exportieren.
#
#   autoneg_add_module(<name> SOURCES <dateien> [PUBLIC_DEPS <module>] [PRIVATE_DEPS <module>])
#     Legt das statische Ziel autoneg_<name> an. <name> ist der Modulname aus
#     tools/architecture_rules.json (z. B. model, adapter_json, backend_cpu).
#     Abhängigkeiten werden als Modulnamen ohne Präfix angegeben; Fremdbibliotheken
#     als vollständige Ziele (z. B. Qt6::Widgets).
#
#   autoneg_add_program(<ziel> SOURCES <dateien> DEPS <module>)
#     Programm der Kompositionswurzel (autoneg, autoneg-cli).

define_property(GLOBAL PROPERTY AUTONEG_ARCH_TARGETS
    BRIEF_DOCS "Ziele, die die Architekturprüfung auswertet"
    FULL_DOCS "Liste der Paare Modulname|Zielname")

function(_autoneg_resolve_deps out)
    set(result "")
    foreach(dep IN LISTS ARGN)
        if(dep MATCHES "::")
            list(APPEND result ${dep})
        else()
            list(APPEND result autoneg_${dep})
        endif()
    endforeach()
    set(${out} ${result} PARENT_SCOPE)
endfunction()

function(autoneg_add_module name)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;PUBLIC_DEPS;PRIVATE_DEPS")
    set(target autoneg_${name})
    add_library(${target} STATIC ${arg_SOURCES})
    add_library(autoneg::${name} ALIAS ${target})
    target_include_directories(${target} PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
    target_compile_features(${target} PUBLIC cxx_std_23)
    _autoneg_resolve_deps(public_deps ${arg_PUBLIC_DEPS})
    _autoneg_resolve_deps(private_deps ${arg_PRIVATE_DEPS})
    if(public_deps)
        target_link_libraries(${target} PUBLIC ${public_deps})
    endif()
    if(private_deps)
        target_link_libraries(${target} PRIVATE ${private_deps})
    endif()
    autoneg_set_warnings(${target})
    set_property(GLOBAL APPEND PROPERTY AUTONEG_ARCH_TARGETS "${name}|${target}")
endfunction()

function(autoneg_add_program target)
    cmake_parse_arguments(PARSE_ARGV 1 arg "" "" "SOURCES;DEPS")
    add_executable(${target} ${arg_SOURCES})
    _autoneg_resolve_deps(deps ${arg_DEPS})
    target_link_libraries(${target} PRIVATE ${deps})
    autoneg_set_warnings(${target})
    set_property(GLOBAL APPEND PROPERTY AUTONEG_ARCH_TARGETS "${target}|${target}")
endfunction()

# Schreibt die tatsächlich gesetzten Link-Abhängigkeiten aller Ziele nach
# <build>/architecture/targets.json und hängt die Prüfung in Build und CTest ein.
# Ausgewertet werden die Zieleigenschaften, nicht die Angaben an autoneg_add_module,
# damit auch nachträgliche target_link_libraries-Aufrufe erfasst werden.
function(autoneg_finalize_architecture)
    get_property(entries GLOBAL PROPERTY AUTONEG_ARCH_TARGETS)
    set(json "{\n  \"targets\": [")
    set(first TRUE)
    foreach(entry IN LISTS entries)
        string(REPLACE "|" ";" parts "${entry}")
        list(GET parts 0 module)
        list(GET parts 1 target)
        get_target_property(type ${target} TYPE)
        get_target_property(source_dir ${target} SOURCE_DIR)
        get_target_property(link ${target} LINK_LIBRARIES)
        get_target_property(ilink ${target} INTERFACE_LINK_LIBRARIES)
        set(all_deps "")
        foreach(dep IN LISTS link ilink)
            # "::@(...)" sind CMake-interne Verzeichnismarker bei target_link_libraries
            # aus einem anderen Verzeichnis, keine Abhängigkeiten
            if(dep MATCHES "-NOTFOUND$" OR dep STREQUAL "" OR dep MATCHES "^::@")
                continue()
            endif()
            # $<LINK_ONLY:x> und ähnliche Ausdrücke auf den Zielnamen zurückführen
            string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" dep "${dep}")
            if(TARGET ${dep})
                get_target_property(aliased ${dep} ALIASED_TARGET)
                if(aliased)
                    set(dep ${aliased})
                endif()
            endif()
            list(APPEND all_deps "${dep}")
        endforeach()
        list(REMOVE_DUPLICATES all_deps)
        set(dep_json "")
        foreach(dep IN LISTS all_deps)
            if(dep_json)
                string(APPEND dep_json ", ")
            endif()
            string(APPEND dep_json "\"${dep}\"")
        endforeach()
        file(RELATIVE_PATH rel_dir "${PROJECT_SOURCE_DIR}" "${source_dir}")
        if(NOT first)
            string(APPEND json ",")
        endif()
        set(first FALSE)
        string(APPEND json "\n    { \"module\": \"${module}\", \"target\": \"${target}\", \"type\": \"${type}\", \"sourceDir\": \"${rel_dir}\", \"deps\": [${dep_json}] }")
    endforeach()
    string(APPEND json "\n  ]\n}\n")
    set(targets_file "${PROJECT_BINARY_DIR}/architecture/targets.json")
    file(WRITE "${targets_file}" "${json}")

    set(check_command
        ${Python3_EXECUTABLE} "${PROJECT_SOURCE_DIR}/tools/check_architecture.py"
        --root "${PROJECT_SOURCE_DIR}"
        --rules "${PROJECT_SOURCE_DIR}/tools/architecture_rules.json"
        --targets "${targets_file}")

    if(AUTONEG_CHECK_ARCHITECTURE)
        add_custom_target(check_architecture ALL
            COMMAND ${check_command}
            COMMENT "Architekturprüfung (Abhängigkeiten, Zyklen, Fremd-Header)"
            VERBATIM)
    endif()
    if(AUTONEG_BUILD_TESTS)
        add_test(NAME architecture.check COMMAND ${check_command})
        set_tests_properties(architecture.check PROPERTIES LABELS "arch")
    endif()
endfunction()
