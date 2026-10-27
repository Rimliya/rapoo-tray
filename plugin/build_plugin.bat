@echo off
setlocal
cd /d "%~dp0"

if not exist ..\bin mkdir ..\bin

echo ========================================================
echo   Building TrafficMonitor Rapoo Plugin (rapoo-plugin.dll)
echo ========================================================

where g++ >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [Toolchain] Using MinGW GCC/G++ 64-bit

    windres --codepage 65001 -i plugin.rc -o plugin.o -O coff
    if %ERRORLEVEL% neq 0 (
        echo [ERROR] windres failed!
        exit /b 1
    )
    g++ -shared -O3 -std=c++17 -mwindows -municode -s -static rapoo_plugin.cpp plugin.o -lsetupapi -lhid -luser32 -lgdi32 -lshell32 -ladvapi32 -o ..\bin\rapoo-plugin.dll
    if %ERRORLEVEL% neq 0 (
        echo [ERROR] g++ compilation failed!
        exit /b 1
    )
    if exist plugin.o del plugin.o
    goto done
)

if not exist "..\.tools\ziglang\ziglang\zig.exe" (
    echo [ERROR] g++ not found and bundled Zig fallback is unavailable.
    exit /b 1
)

echo [Toolchain] Using bundled Zig fallback
python "..\.tools\buildtools\bin\windres.py" plugin.rc -o plugin.o -O coff
if errorlevel 1 (
    echo [ERROR] resource compilation failed!
    exit /b 1
)
set "ZIG_GLOBAL_CACHE_DIR=%~dp0..\.tools\zig-cache-global"
set "ZIG_LOCAL_CACHE_DIR=%~dp0..\.tools\zig-cache-local"
"..\.tools\ziglang\ziglang\zig.exe" c++ -target x86_64-windows-gnu -shared -O2 -std=c++17 -DUNICODE -D_UNICODE -mwindows -municode rapoo_plugin.cpp plugin.o -o ..\bin\rapoo-plugin.dll -Wl,--subsystem,windows -lsetupapi -lhid -luser32 -lgdi32 -lshell32 -ladvapi32
if errorlevel 1 (
    echo [ERROR] Zig compilation failed!
    exit /b 1
)
if exist plugin.o del plugin.o
goto done

:done
if exist ..\bin\rapoo-plugin.dll (
    echo.
    echo [SUCCESS] Plugin DLL built successfully: bin\rapoo-plugin.dll
    dir ..\bin\rapoo-plugin.dll | findstr rapoo-plugin.dll
) else (
    echo.
    echo [ERROR] DLL not found!
    exit /b 1
)
