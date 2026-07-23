cmake_minimum_required(VERSION 3.20)
project(PedalBoard VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

# Find Qt6
find_package(Qt6 COMPONENTS Widgets Core Gui Network REQUIRED)

# Find PkgConfig packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(JACK REQUIRED IMPORTED_TARGET jack)
pkg_check_modules(LILV REQUIRED IMPORTED_TARGET lilv-0)
pkg_check_modules(SUIL REQUIRED IMPORTED_TARGET suil-0)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)

# Main executable target
add_executable(PedalBoard
    src/main.cpp
    src/audio/AudioEngine.cpp
    src/audio/AudioEngine.h
    src/audio/AudioNode.h
    src/audio/LV2Host.cpp
    src/audio/LV2Host.h
    src/audio/VST3Host.cpp
    src/audio/VST3Host.h
    src/audio/CLAPHost.cpp
    src/audio/CLAPHost.h
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/NodeCanvas.cpp
    src/ui/NodeCanvas.h
    src/ui/NodeWidget.cpp
    src/ui/NodeWidget.h
    src/ui/PortWidget.cpp
    src/ui/PortWidget.h
    src/ui/ConnectionWire.cpp
    src/ui/ConnectionWire.h
    src/ui/PlusButtonWidget.cpp
    src/ui/PlusButtonWidget.h
    src/ui/GridRow.h
    src/ui/Tone3000Dialog.cpp
    src/ui/Tone3000Dialog.h
    src/ui/ModelDetailsDialog.cpp
    src/ui/ModelDetailsDialog.h
    src/ui/GtkUIHelper.cpp
    src/ui/GtkUIHelper.h
    src/ui/RoutingHandleItem.cpp
    src/ui/RoutingHandleItem.h
    src/ui/AboutDialog.cpp
    src/ui/AboutDialog.h
)

target_include_directories(PedalBoard PRIVATE src src/audio)
target_compile_options(PedalBoard PRIVATE -g -rdynamic)
target_link_options(PedalBoard PRIVATE -rdynamic)
target_link_libraries(PedalBoard PRIVATE
    Qt6::Widgets
    Qt6::Network
    PkgConfig::JACK
    PkgConfig::LILV
    PkgConfig::SUIL
    PkgConfig::X11
    dl
)
