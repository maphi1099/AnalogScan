find_package(Qt6 CONFIG REQUIRED COMPONENTS Gui Widgets PrintSupport)

autoneg_add_module(adapter_qtprint
    SOURCES src/qt_document_renderer.cpp
    PUBLIC_DEPS ports layout
    PRIVATE_DEPS Qt6::Gui Qt6::Widgets Qt6::PrintSupport
)
