cmake_minimum_required(VERSION 3.20)
project(RigRoom VERSION 0.11.0 LANGUAGES CXX)

option(RIGROOM_DEV_BUILD "Append a development suffix to the application version" ON)
option(RIGROOM_ENABLE_RT_METRICS "Collect realtime callback timing diagnostics" OFF)
set(RIGROOM_VERSION_STRING "${PROJECT_VERSION}")
set(RIGROOM_IS_DEV_BUILD false)
if(RIGROOM_DEV_BUILD)
    string(APPEND RIGROOM_VERSION_STRING "-dev")
    set(RIGROOM_IS_DEV_BUILD true)
endif()

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Version.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/Version.h
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Find Qt6
find_package(Qt6 COMPONENTS Widgets Core Gui Network REQUIRED)

# Find PkgConfig packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(JACK REQUIRED IMPORTED_TARGET jack)
pkg_check_modules(LILV REQUIRED IMPORTED_TARGET lilv-0)
pkg_check_modules(SUIL REQUIRED IMPORTED_TARGET suil-0)
pkg_check_modules(X11 REQUIRED IMPORTED_TARGET x11)
pkg_check_modules(SECRET REQUIRED IMPORTED_TARGET libsecret-1)

# Main executable target
add_executable(RigRoom
    src/main.cpp
    src/CredentialStore.cpp
    src/CredentialStore.h
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
    src/ui/InspectorComponents.cpp
    src/ui/InspectorComponents.h
    src/ui/NodeCanvas.cpp
    src/ui/NodeCanvas.h
    src/ui/CanvasMetrics.h
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
    src/ui/Tone3000ImageLoader.cpp
    src/ui/Tone3000ImageLoader.h
    src/ui/ModelDetailsDialog.cpp
    src/ui/ModelDetailsDialog.h
    src/ui/GtkUIHelper.cpp
    src/ui/GtkUIHelper.h
    src/ui/RoutingHandleItem.cpp
    src/ui/RoutingHandleItem.h
    src/ui/AboutDialog.cpp
    src/ui/AboutDialog.h
    src/ui/PresetBrowser.cpp
    src/ui/FootswitchTile.cpp
    src/ui/FootswitchTile.h
    src/ui/PresetBrowser.h
    src/preset/PresetLibrary.cpp
    src/preset/PresetLibrary.h
    src/preset/SceneModel.cpp
    src/preset/SceneModel.h
    src/control/RigController.cpp
    src/control/RigController.h
    src/control/MidiMap.cpp
    src/control/MidiMap.h
    src/control/MidiRouter.cpp
    src/control/MidiRouter.h
    src/ui/resources.qrc
)

target_include_directories(RigRoom PRIVATE src src/audio ${CMAKE_CURRENT_BINARY_DIR})
target_compile_options(RigRoom PRIVATE -rdynamic)
if(RIGROOM_ENABLE_RT_METRICS)
    target_compile_definitions(RigRoom PRIVATE RIGROOM_ENABLE_RT_METRICS)
endif()
target_link_options(RigRoom PRIVATE -rdynamic)
target_link_libraries(RigRoom PRIVATE
    Qt6::Widgets
    Qt6::Network
    PkgConfig::JACK
    PkgConfig::LILV
    PkgConfig::SUIL
    PkgConfig::X11
    PkgConfig::SECRET
    dl
)

include(CTest)
if(BUILD_TESTING)
    add_executable(RigRoomRoutingTests
        tests/RoutingTests.cpp
        src/audio/AudioEngine.cpp
        src/audio/AudioEngine.h
        src/audio/AudioNode.h
        src/ui/NodeCanvas.cpp
        src/ui/NodeCanvas.h
        src/ui/CanvasMetrics.h
        src/ui/NodeWidget.cpp
        src/ui/NodeWidget.h
        src/ui/PortWidget.cpp
        src/ui/PortWidget.h
        src/ui/ConnectionWire.cpp
        src/ui/ConnectionWire.h
        src/ui/PlusButtonWidget.cpp
        src/ui/PlusButtonWidget.h
        src/ui/RoutingHandleItem.cpp
        src/ui/RoutingHandleItem.h
        src/ui/GridRow.h
    )
    target_include_directories(RigRoomRoutingTests PRIVATE src src/audio ${CMAKE_CURRENT_BINARY_DIR})
    target_compile_options(RigRoomRoutingTests PRIVATE -rdynamic -UNDEBUG)
    target_link_options(RigRoomRoutingTests PRIVATE -rdynamic)
    target_link_libraries(RigRoomRoutingTests PRIVATE Qt6::Widgets PkgConfig::JACK)
    add_test(NAME RigRoomRoutingTests COMMAND RigRoomRoutingTests)
    set_tests_properties(RigRoomRoutingTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(RigRoomPresetTests
        tests/PresetTests.cpp
        src/preset/PresetLibrary.cpp
        src/preset/PresetLibrary.h
        src/preset/SceneModel.cpp
        src/preset/SceneModel.h
        src/audio/AudioNode.h
    )
    target_include_directories(RigRoomPresetTests PRIVATE src)
    target_compile_options(RigRoomPresetTests PRIVATE -UNDEBUG)
    target_link_libraries(RigRoomPresetTests PRIVATE Qt6::Core)
    add_test(NAME RigRoomPresetTests COMMAND RigRoomPresetTests)

    add_executable(RigRoomMidiTests
        tests/MidiTests.cpp
        src/control/MidiMap.cpp
        src/control/MidiMap.h
    )
    target_include_directories(RigRoomMidiTests PRIVATE src)
    target_compile_options(RigRoomMidiTests PRIVATE -UNDEBUG)
    target_link_libraries(RigRoomMidiTests PRIVATE Qt6::Core)
    add_test(NAME RigRoomMidiTests COMMAND RigRoomMidiTests)
endif()

include(GNUInstallDirs)
install(TARGETS RigRoom RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES packaging/org.rigroom.RigRoom.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
install(FILES packaging/org.rigroom.RigRoom.appdata.xml DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
install(FILES packaging/org.rigroom.RigRoom.png DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
install(FILES LICENSE COPYRIGHT THIRD_PARTY_NOTICES.md DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/RigRoom)
