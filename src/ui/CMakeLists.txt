find_package(Qt6 CONFIG REQUIRED COMPONENTS Core Gui GuiPrivate Widgets)

# Header mit Q_OBJECT stehen unter include/: als Quellen angeben, damit AUTOMOC sie findet
file(GLOB ui_headers CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/include/autoneg/ui/*.hpp)
autoneg_add_module(ui
    SOURCES
        ${ui_headers}
        src/canvas_tools.cpp
        src/dialogs.cpp
        src/edit_list_model.cpp
        src/gui_application.cpp
        src/image_canvas.cpp
        src/main_window.cpp
        src/panels.cpp

        src/qt_session_bridge.cpp
        src/translations.cpp
        src/ui_controller.cpp
        src/views.cpp
    PUBLIC_DEPS application
    PRIVATE_DEPS Qt6::Core Qt6::Gui Qt6::Widgets
)
# Qt-Typen stehen in den öffentlichen Headern der Oberfläche. QRhi (Bildfläche) ist
# halböffentliche API von QtGui und braucht GuiPrivate (Kompatibilität nur innerhalb einer
# Qt-Nebenversion, deshalb nur in image_canvas.cpp).
target_link_libraries(autoneg_ui PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets PRIVATE Qt6::GuiPrivate)
set_target_properties(autoneg_ui PROPERTIES AUTOMOC ON)
# Qt-Schlüsselwörter (emit, signals, slots) sind Makros und kollidieren mit IStep::emit:
# in der Oberfläche Q_EMIT/Q_SIGNALS/Q_SLOTS verwenden
target_compile_definitions(autoneg_ui PUBLIC QT_NO_KEYWORDS)
