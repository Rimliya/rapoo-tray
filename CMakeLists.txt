cmake_minimum_required(VERSION 3.21)
project(rapoo-tray VERSION 1.7.0 LANGUAGES CXX RC)

if(NOT WIN32)
    message(FATAL_ERROR "rapoo-tray is a Windows application.")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(SLINT_STYLE "fluent" CACHE STRING "Slint widget style" FORCE)

find_package(Slint REQUIRED)

add_executable(rapoo-tray WIN32
    src/main.cpp
    src/rapoo_protocol.cpp
    src/device_manager.cpp
    src/osd_window.cpp
    src/tray_menu.cpp
    src/alert_window.cpp
    src/profile_manager.cpp
    src/battery_estimator.cpp
    src/dpi_window.cpp
    src/slint_settings_window.cpp
    src/slint_about_window.cpp
    res/app.rc
)

set_property(TARGET rapoo-tray PROPERTY SLINT_EMBED_RESOURCES embed-files)
slint_target_sources(rapoo-tray ui/settings.slint NAMESPACE RapooUi)

target_include_directories(rapoo-tray PRIVATE src)
target_compile_definitions(rapoo-tray PRIVATE
    UNICODE
    _UNICODE
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _CRT_SECURE_NO_WARNINGS
)
target_link_libraries(rapoo-tray PRIVATE
    Slint::Slint
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

add_custom_command(TARGET rapoo-tray POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:slint_cpp-shared>"
        "$<TARGET_FILE_DIR:rapoo-tray>/slint_cpp.dll"
    COMMENT "Copying Slint runtime"
)

install(TARGETS rapoo-tray RUNTIME DESTINATION .)
install(FILES "$<TARGET_FILE:slint_cpp-shared>" DESTINATION . RENAME slint_cpp.dll)

if(MSVC)
    target_link_options(rapoo-tray PRIVATE /MANIFEST:NO)
endif()

