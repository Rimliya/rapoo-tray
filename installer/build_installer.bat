@echo off
setlocal
cd /d "%~dp0"

if not exist ..\dist mkdir ..\dist
if not exist ..\bin\rapoo-tray.exe (
    echo [ERROR] bin\rapoo-tray.exe not found. Run build.bat first.
    exit /b 1
)
if not exist ..\bin\slint_cpp.dll (
    echo [ERROR] bin\slint_cpp.dll not found. Run build.bat first.
    exit /b 1
)

echo [Building Installer Resource...]
windres --codepage 65001 -i installer.rc -o installer.o -O coff

echo [Compiling rapoo-tray-setup.exe...]
g++ -O3 -std=c++17 -mwindows -municode -Wl,--subsystem,windows -s -static installer.cpp installer.o -lole32 -lshell32 -luser32 -lgdi32 -ladvapi32 -luuid -o ..\dist\rapoo-tray-setup.exe

if exist installer.o del installer.o

if exist ..\dist\rapoo-tray-setup.exe (
    echo.
    echo [SUCCESS] Installer built: dist\rapoo-tray-setup.exe
    dir ..\dist\rapoo-tray-setup.exe | findstr rapoo-tray-setup.exe
) else (
    echo.
    echo [ERROR] Failed to build installer!
    exit /b 1
)
