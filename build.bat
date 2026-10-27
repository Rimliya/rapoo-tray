@echo off
setlocal EnableExtensions
cd /d "%~dp0"

echo ========================================================
echo   Building rapoo-tray (Win32 tray + WPF Fluent UI)
echo ========================================================

if exist "%~dp0.tools\buildtools\bin\cmake.exe" set "PATH=%~dp0.tools\buildtools\bin;%PATH%"

where dotnet >nul 2>nul
if errorlevel 1 (
    echo [ERROR] .NET SDK was not found. Install the .NET SDK to build the settings center.
    exit /b 1
)

if not exist "%~dp0NuGet.config" (
    echo [ERROR] NuGet.config is missing.
    exit /b 1
)

echo [1/3] Restoring and building WPF Fluent settings...
dotnet restore "settings-ui\RapooTray.Settings.csproj" --configfile "NuGet.config"
if errorlevel 1 goto failed
dotnet build "settings-ui\RapooTray.Settings.csproj" -c Release --no-restore
if errorlevel 1 goto failed
if not exist bin mkdir bin
set "UI_COPY_FAILED="
for %%F in ("settings-ui\bin\Release\net48\*") do (
    if /I not "%%~xF"==".pdb" (
        copy /Y "%%F" "bin\" >nul
        if errorlevel 1 set "UI_COPY_FAILED=1"
    )
)
if defined UI_COPY_FAILED (
    echo [ERROR] Settings files are in use. Close rapoo-tray-settings.exe and run build.bat again.
    goto failed
)

echo [2/3] Building native tray application...
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
if not errorlevel 1 (
    where cmake >nul 2>nul
    if errorlevel 1 (
        echo [ERROR] CMake 3.21 or newer was not found in PATH.
        goto failed
    )
    cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 goto failed
    cmake --build build --config Release
    if errorlevel 1 goto failed
) else (
    if not exist ".tools\ziglang\ziglang\zig.exe" (
        echo [ERROR] MSVC x64 toolchain not found, and the bundled Zig fallback is unavailable.
        echo Install Visual Studio Build Tools with Desktop development with C++.
        goto failed
    )
    echo [INFO] MSVC not found; using the bundled Zig fallback for local testing.
    powershell -NoProfile -ExecutionPolicy Bypass -File "tools\build-local-zig.ps1"
    if errorlevel 1 goto failed
    copy /Y ".tools\rapoo-tray-test.exe" "bin\rapoo-tray.exe" >nul
    if errorlevel 1 (
        echo [ERROR] bin\rapoo-tray.exe is in use. Exit the tray program and run build.bat again.
        goto failed
    )
)

if not exist bin\rapoo-tray.exe goto failed
if not exist bin\rapoo-tray-settings.exe goto failed
if not exist bin\Wpf.Ui.dll goto failed

echo [3/3] Verifying local test bundle...
echo.
echo [SUCCESS] bin\rapoo-tray.exe
echo [SUCCESS] bin\rapoo-tray-settings.exe
echo [NOTE] Local builds are for testing. Official releases are built by GitHub Actions.
exit /b 0

:failed
echo.
echo [ERROR] Build failed.
exit /b 1
