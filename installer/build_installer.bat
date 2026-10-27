@echo off
setlocal
cd /d "%~dp0"

if not exist ..\dist mkdir ..\dist
if not exist ..\bin\rapoo-tray.exe (
    echo [ERROR] bin\rapoo-tray.exe not found. Run build.bat first.
    exit /b 1
)
for %%F in (rapoo-tray-settings.exe rapoo-tray-settings.exe.config Wpf.Ui.dll Wpf.Ui.Abstractions.dll System.Memory.dll System.Buffers.dll System.Numerics.Vectors.dll System.Runtime.CompilerServices.Unsafe.dll) do (
    if not exist "..\bin\%%F" (
        echo [ERROR] bin\%%F not found. Run build.bat first.
        exit /b 1
    )
)

where g++ >nul 2>nul
if not errorlevel 1 (
    echo [Building Installer Resource...]
    windres --codepage 65001 -i installer.rc -o installer.o -O coff
    if errorlevel 1 exit /b 1

    echo [Compiling rapoo-tray-setup.exe...]
    g++ -O3 -std=c++17 -mwindows -municode -Wl,--subsystem,windows -s -static installer.cpp installer.o -lole32 -lshell32 -luser32 -lgdi32 -ladvapi32 -luuid -o ..\dist\rapoo-tray-setup.exe
    if errorlevel 1 exit /b 1
    if exist installer.o del installer.o
) else (
    if not exist "..\.tools\ziglang\ziglang\zig.exe" (
        echo [ERROR] g++ not found and bundled Zig fallback is unavailable.
        exit /b 1
    )
    echo [Building Installer Resource with Zig fallback...]
    python "..\.tools\buildtools\bin\windres.py" installer.rc -o installer.o -O coff
    if errorlevel 1 exit /b 1
    set "ZIG_GLOBAL_CACHE_DIR=%~dp0..\.tools\zig-cache-global"
    set "ZIG_LOCAL_CACHE_DIR=%~dp0..\.tools\zig-cache-local"
    "..\.tools\ziglang\ziglang\zig.exe" c++ -target x86_64-windows-gnu -std=c++17 -O2 -DUNICODE -D_UNICODE -mwindows -municode installer.cpp installer.o -o ..\dist\rapoo-tray-setup.exe -Wl,--subsystem,windows -lole32 -lshell32 -luser32 -lgdi32 -ladvapi32 -lcomctl32 -luuid
    if errorlevel 1 exit /b 1
    if exist installer.o del installer.o
)

if exist ..\dist\rapoo-tray-setup.exe (
    echo.
    echo [SUCCESS] Installer built: dist\rapoo-tray-setup.exe
    dir ..\dist\rapoo-tray-setup.exe | findstr rapoo-tray-setup.exe
) else (
    echo.
    echo [ERROR] Failed to build installer!
    exit /b 1
)
