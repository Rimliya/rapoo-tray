cmake_minimum_required(VERSION 3.21)
project(rapoo-tray VERSION 1.14.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "rapoo-tray is a Windows application.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_executable(rapoo-tray WIN32
    src/main.cpp
    src/rapoo_protocol.cpp
    src/device_manager.cpp
    src/osd_window.cpp
    src/osd_config.cpp
    src/quick_panel.cpp
    src/tray_menu.cpp
    src/alert_window.cpp
    src/battery_alert_policy.cpp
    src/battery_history.cpp
    src/profile_manager.cpp
    src/battery_estimator.cpp
    src/dpi_window.cpp
    src/settings_launcher.cpp
    src/ui_bridge.cpp
    src/about_launcher.cpp
    res/app.rc
)

target_include_directories(rapoo-tray PRIVATE src)
target_compile_definitions(rapoo-tray PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
target_link_libraries(rapoo-tray PRIVATE
    setupapi
    hid
    user32
    gdi32
    shell32
    advapi32
    uxtheme
    dwmapi
    comdlg32
    comctl32
    winhttp
    version
)

set_target_properties(rapoo-tray PROPERTIES
    OUTPUT_NAME "rapoo-tray"
    VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
)

foreach(config DEBUG RELEASE RELWITHDEBINFO MINSIZEREL)
    set_target_properties(rapoo-tray PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY_${config} "${CMAKE_SOURCE_DIR}/bin"
    )
endforeach()

install(TARGETS rapoo-tray RUNTIME DESTINATION .)

include(CTest)
if(BUILD_TESTING)
    add_executable(rapoo-tray-tests
        tests/native_tests.cpp
        src/osd_config.cpp
        src/battery_alert_policy.cpp
        src/battery_history.cpp
    )
    target_include_directories(rapoo-tray-tests PRIVATE src)
    target_compile_definitions(rapoo-tray-tests PRIVATE
        UNICODE
        _UNICODE
        WIN32_LEAN_AND_MEAN
        NOMINMAX
        _CRT_SECURE_NO_WARNINGS
    )
    target_link_libraries(rapoo-tray-tests PRIVATE user32 shell32 advapi32)
    if(MSVC)
        target_compile_options(rapoo-tray-tests PRIVATE /utf-8)
    endif()
    add_test(NAME native-logic COMMAND rapoo-tray-tests)
endif()

if(MSVC)
    target_compile_options(rapoo-tray PRIVATE /utf-8 /bigobj)
    target_link_options(rapoo-tray PRIVATE /MANIFEST:NO)
else()
    # MinGW/Clang-Zig do not translate CMake's WIN32 executable flag into a
    # Windows GUI subsystem. Keep wWinMain and suppress the console window.
    target_link_options(rapoo-tray PRIVATE -municode -Wl,--subsystem,windows)
endif()
