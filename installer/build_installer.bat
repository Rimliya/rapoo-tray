@echo off
setlocal
cd /d "%~dp0"

if not exist ..\dist mkdir ..\dist
if not exist ..\bin\rapoo-tray.exe (
    echo [ERROR] ..\bin\rapoo-tray.exe not found. Run build.bat first.
    exit /b 1
)

where zig >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [Toolchain] Using Zig C++ 0.16+ ...
    zig rc /c 65001 /fo installer.o installer.rc
    if %ERRORLEVEL% neq 0 goto fail
    zig c++ -O3 -s -std=c++17 -municode -mwindows -static installer.cpp installer.o -lole32 -lshell32 -luser32 -lgdi32 -ladvapi32 -luuid -o ..\dist\rapoo-tray-setup.exe
    if %ERRORLEVEL% neq 0 goto fail
    if exist installer.o del installer.o
    goto done
)

where g++ >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [WARN] Zig not found; using MinGW. Do not publish this build without AV scanning.
    where windres >nul 2>nul
    if %ERRORLEVEL% neq 0 (
        echo [ERROR] windres not found in PATH.
        exit /b 1
    )
    windres --codepage 65001 -i installer.rc -o installer.o -O coff
    if %ERRORLEVEL% neq 0 goto fail
    g++ -O3 -std=c++17 -mwindows -municode -s -static installer.cpp installer.o -lole32 -lshell32 -luser32 -lgdi32 -ladvapi32 -luuid -o ..\dist\rapoo-tray-setup.exe
    if %ERRORLEVEL% neq 0 goto fail
    if exist installer.o del installer.o
    goto done
)

echo [ERROR] Zig or MinGW g++ was not found in PATH.
exit /b 1

:fail
echo [ERROR] Installer compilation failed.
if exist installer.o del installer.o
exit /b 1

:done
if exist ..\dist\rapoo-tray-setup.exe (
    echo.
    echo [SUCCESS] Installer built: dist\rapoo-tray-setup.exe
    dir ..\dist\rapoo-tray-setup.exe | findstr rapoo-tray-setup.exe
) else (
    echo [ERROR] Failed to build installer.
    exit /b 1
)
