param(
    [switch]$SkipWpf
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$zig = Join-Path $root '.tools\ziglang\ziglang\zig.exe'
$windresWrapper = Join-Path $root '.tools\buildtools\bin\windres.py'
if (!(Test-Path $zig) -or !(Test-Path $windresWrapper)) {
    throw 'Bundled Zig toolchain was not found under .tools.'
}

$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root '.tools\zig-cache-global'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root '.tools\zig-cache-local'
New-Item -ItemType Directory -Force -Path $env:ZIG_GLOBAL_CACHE_DIR,$env:ZIG_LOCAL_CACHE_DIR,(Join-Path $root 'bin') | Out-Null

$resource = Join-Path $root '.tools\app-rc-ui.obj'
Push-Location $root
try {
    & python $windresWrapper 'res\app.rc' -o '.tools\app-rc-ui.obj' -O coff
    if ($LASTEXITCODE -ne 0) { throw 'Resource compilation failed.' }

    $sources = @(
        'src\main.cpp', 'src\rapoo_protocol.cpp', 'src\device_manager.cpp',
        'src\osd_window.cpp', 'src\tray_menu.cpp', 'src\alert_window.cpp',
        'src\profile_manager.cpp', 'src\battery_estimator.cpp', 'src\dpi_window.cpp',
        'src\settings_launcher.cpp', 'src\about_launcher.cpp', 'src\ui_bridge.cpp'
    )
    $arguments = @(
        'c++', '-target', 'x86_64-windows-gnu', '-std=c++20', '-O2',
        '-DUNICODE', '-D_UNICODE', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-D_CRT_SECURE_NO_WARNINGS',
        '-Isrc'
    ) + $sources + @(
        '.tools\app-rc-ui.obj', '-o', '.tools\rapoo-tray-test.exe', '-municode', '-Wl,--subsystem,windows',
        '-lsetupapi', '-lhid', '-luser32', '-lgdi32', '-lshell32', '-ladvapi32',
        '-luxtheme', '-ldwmapi', '-lcomdlg32', '-lcomctl32', '-lwinhttp', '-lversion'
    )
    & $zig @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Zig C++ link failed.' }
    if (!(Test-Path (Join-Path $root '.tools\rapoo-tray-test.exe'))) { throw '.tools\rapoo-tray-test.exe was not produced.' }
    Write-Host '[SUCCESS] native test binary built'
}
finally {
    Pop-Location
}
