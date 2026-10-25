@echo off
setlocal
cd /d "%~dp0"

if not exist bin mkdir bin

echo ========================================================
echo   Building rapoo-tray (Release Standalone Executable)
echo ========================================================

where zig >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [Toolchain] Using Zig C++ 0.16+ ...
    zig rc /c 65001 /I res /fo bin\app.o res\app.rc
    if %ERRORLEVEL% neq 0 goto fail
    zig c++ -O3 -s -std=c++17 -municode -mwindows -static -Isrc src\main.cpp src\rapoo_protocol.cpp src\device_manager.cpp src\osd_window.cpp src\tray_menu.cpp src\alert_window.cpp src\profile_manager.cpp src\battery_estimator.cpp src\settings_window.cpp src\dpi_window.cpp bin\app.o -lsetupapi -lhid -luser32 -lgdi32 -lshell32 -ladvapi32 -luxtheme -ldwmapi -lcomdlg32 -lcomctl32 -o bin\rapoo-tray.exe
    if %ERRORLEVEL% neq 0 goto fail
    if exist bin\app.o del bin\app.o
    goto done
)

where cl >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [Toolchain] Using MSVC CL ...
    rc /c 65001 /fo bin\app.res res\app.rc
    if %ERRORLEVEL% neq 0 goto fail
    cl /nologo /O2 /std:c++17 /MT /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN src\main.cpp src\rapoo_protocol.cpp src\device_manager.cpp src\osd_window.cpp src\tray_menu.cpp src\alert_window.cpp src\profile_manager.cpp src\battery_estimator.cpp src\settings_window.cpp src\dpi_window.cpp bin\app.res /Fe:bin\rapoo-tray.exe /link /SUBSYSTEM:WINDOWS setupapi.lib hid.lib user32.lib gdi32.lib shell32.lib advapi32.lib uxtheme.lib dwmapi.lib comdlg32.lib comctl32.lib
    if %ERRORLEVEL% neq 0 goto fail
    if exist bin\app.res del bin\app.res
    if exist *.obj del *.obj
    goto done
)

echo [ERROR] Zig or cl.exe was not found in PATH.
echo Install Zig 0.16+ and make sure "zig" is available in PATH.
exit /b 1

:fail
echo [ERROR] Compilation failed.
if exist bin\app.o del bin\app.o
if exist bin\app.res del bin\app.res
if exist *.obj del *.obj
exit /b 1

:done
if exist bin\rapoo-tray.exe (
    echo.
    echo [SUCCESS] Build completed successfully: bin\rapoo-tray.exe
    echo File size:
    dir bin\rapoo-tray.exe | findstr rapoo-tray.exe
) else (
    echo.
    echo [ERROR] Compilation failed.
    exit /b 1
)
