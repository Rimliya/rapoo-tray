@echo off
setlocal
cd /d "%~dp0"

if not exist ..\bin mkdir ..\bin

echo ========================================================
echo   Building TrafficMonitor Rapoo Plugin
echo ========================================================

where zig >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [Toolchain] Using Zig C++ 0.16+ ...
    zig rc /c 65001 /fo plugin.o plugin.rc
    if %ERRORLEVEL% neq 0 goto fail
    zig c++ -shared -O3 -s -std=c++17 -municode -static rapoo_plugin.cpp plugin.o -lsetupapi -lhid -luser32 -lgdi32 -lshell32 -ladvapi32 -o ..\bin\rapoo-plugin.dll
    if %ERRORLEVEL% neq 0 goto fail
    if exist plugin.o del plugin.o
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
    windres --codepage 65001 -i plugin.rc -o plugin.o -O coff
    if %ERRORLEVEL% neq 0 goto fail
    g++ -shared -O3 -std=c++17 -mwindows -municode -s -static rapoo_plugin.cpp plugin.o -lsetupapi -lhid -luser32 -lgdi32 -lshell32 -ladvapi32 -o ..\bin\rapoo-plugin.dll
    if %ERRORLEVEL% neq 0 goto fail
    if exist plugin.o del plugin.o
    goto done
)

echo [ERROR] Zig or MinGW g++ was not found in PATH.
exit /b 1

:fail
echo [ERROR] Plugin compilation failed.
if exist plugin.o del plugin.o
exit /b 1

:done
if exist ..\bin\rapoo-plugin.dll (
    echo.
    echo [SUCCESS] Plugin built: bin\rapoo-plugin.dll
    dir ..\bin\rapoo-plugin.dll | findstr rapoo-plugin.dll
) else (
    echo [ERROR] Failed to build plugin.
    exit /b 1
)
