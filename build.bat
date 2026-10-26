@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo ========================================================
echo   Building rapoo-tray (Slint Fluent settings UI)
echo ========================================================

set "SLINT_SDK=%~dp0.tools\slint-prefix"
if defined SLINT_SDK_OVERRIDE set "SLINT_SDK=%SLINT_SDK_OVERRIDE%"
if not exist "%SLINT_SDK%\lib\cmake\Slint\SlintConfig.cmake" (
    echo [ERROR] Slint C++ SDK not found at "%SLINT_SDK%".
    echo Download the Slint 1.18.1 Windows MSVC C++ package and install it there,
    echo or set SLINT_SDK_OVERRIDE to an existing SDK directory.
    exit /b 1
)

where cmake >nul 2>nul
if errorlevel 1 (
    echo [ERROR] CMake 3.21 or newer was not found in PATH.
    exit /b 1
)

where cl >nul 2>nul
if errorlevel 1 (
    set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
    if exist "%VSWHERE%" (
        for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSROOT=%%I"
    )
    if defined VSROOT if exist "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" (
        call "%VSROOT%\VC\Auxiliary\Build\vcvars64.bat" >nul
    )
)

where cl >nul 2>nul
if errorlevel 1 (
    echo [ERROR] MSVC x64 toolchain not found.
    echo Install Visual Studio Build Tools with the Desktop development with C++ workload.
    exit /b 1
)

if not exist bin mkdir bin
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="%SLINT_SDK%" -DSLINT_STYLE=fluent
if errorlevel 1 goto failed
cmake --build build --config Release
if errorlevel 1 goto failed

if not exist bin\rapoo-tray.exe goto failed
if not exist bin\slint_cpp.dll goto failed

echo.
echo [SUCCESS] bin\rapoo-tray.exe
echo [NOTE] slint_cpp.dll must remain next to rapoo-tray.exe.
exit /b 0

:failed
echo.
echo [ERROR] Build failed.
exit /b 1
