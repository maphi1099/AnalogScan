# Compiler-Warnungen (als Fehler) und Sanitizer für alle eigenen Ziele.

# clang-cl versteht MSVC-Optionen, die übrigen Compiler GCC-Optionen.
if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    set(AUTONEG_IS_CLANG_CL TRUE)
else()
    set(AUTONEG_IS_CLANG_CL FALSE)
endif()

function(autoneg_set_warnings target)
    if(AUTONEG_IS_CLANG_CL)
        target_compile_options(${target} PRIVATE /W4 /WX /permissive- /utf-8
            -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Wold-style-cast
            -Woverloaded-virtual -Wconversion -Wimplicit-fallthrough)
        # UTF-8-Quellen, keine min/max-Makros aus windows.h
        target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror
            -Wshadow -Wnon-virtual-dtor -Wold-style-cast -Wcast-align -Woverloaded-virtual
            -Wconversion -Wnull-dereference -Wimplicit-fallthrough)
    endif()
endfunction()

if(AUTONEG_SANITIZER)
    if(AUTONEG_IS_CLANG_CL)
        message(FATAL_ERROR "Sanitizer sind nur für die Linux-Presets vorgesehen")
    endif()
    if(AUTONEG_SANITIZER STREQUAL "address")
        set(_autoneg_san_flags -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
    elseif(AUTONEG_SANITIZER STREQUAL "thread")
        set(_autoneg_san_flags -fsanitize=thread -fno-omit-frame-pointer)
    else()
        message(FATAL_ERROR "Unbekannter Sanitizer: ${AUTONEG_SANITIZER} (erlaubt: address, thread)")
    endif()
    add_compile_options(${_autoneg_san_flags})
    add_link_options(${_autoneg_san_flags})
endif()
