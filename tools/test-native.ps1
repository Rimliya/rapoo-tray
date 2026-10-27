$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$zig = Join-Path $root '.tools\ziglang\ziglang\zig.exe'
if (!(Test-Path $zig)) { throw 'Bundled Zig toolchain was not found under .tools.' }

$env:ZIG_GLOBAL_CACHE_DIR = Join-Path $root '.tools\zig-cache-test-global'
$env:ZIG_LOCAL_CACHE_DIR = Join-Path $root '.tools\zig-cache-test-local'
New-Item -ItemType Directory -Force -Path $env:ZIG_GLOBAL_CACHE_DIR,$env:ZIG_LOCAL_CACHE_DIR | Out-Null
$output = Join-Path $root '.tools\rapoo-tray-native-tests.exe'
Push-Location $root
try {
    $arguments = @(
        'c++', '-target', 'x86_64-windows-gnu', '-std=c++20', '-O2', '-w',
        '-DUNICODE', '-D_UNICODE', '-DWIN32_LEAN_AND_MEAN', '-DNOMINMAX', '-D_CRT_SECURE_NO_WARNINGS',
        '-Isrc', 'tests\native_tests.cpp', 'src\osd_config.cpp',
        'src\battery_alert_policy.cpp', 'src\battery_history.cpp',
        '-o', $output, '-luser32', '-lshell32', '-ladvapi32'
    )
    & $zig @arguments
    if ($LASTEXITCODE -ne 0) { throw 'Native test compilation failed.' }
    & $output
    if ($LASTEXITCODE -ne 0) { throw 'Native logic tests failed.' }
}
finally {
    Pop-Location
}
