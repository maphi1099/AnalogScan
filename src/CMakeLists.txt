# Kern
add_subdirectory(base)
add_subdirectory(model)
add_subdirectory(image)
add_subdirectory(layout)
add_subdirectory(ports)
add_subdirectory(pipeline)
add_subdirectory(steps)
add_subdirectory(analysis)

# Anwendung und Oberfläche
add_subdirectory(application)
add_subdirectory(ui)

# Adapter (je eine Fremdbibliothek) und Rechen-Backend
add_subdirectory(adapters/libraw)
add_subdirectory(adapters/oiio)
add_subdirectory(adapters/lcms)
add_subdirectory(adapters/opencv)
add_subdirectory(adapters/json)
add_subdirectory(adapters/xxhash)
add_subdirectory(adapters/qtprint)
add_subdirectory(adapters/spdlog)
add_subdirectory(adapters/tbb)
add_subdirectory(backends/cpu)

# Nachweis der Architekturprüfung: absichtlicher Verstoß (model darf nicht von ports abhängen)
if(AUTONEG_ARCH_VIOLATION_DEMO)
    target_link_libraries(autoneg_model PUBLIC autoneg_ports)
endif()
