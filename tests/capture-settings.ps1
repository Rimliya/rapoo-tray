param(
    [int]$Page = 2,
    [string]$Output = "",
    [int]$Theme = 2
)
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class RapooCapture {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
}
"@
$root = Split-Path -Parent $PSScriptRoot
[RapooCapture]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
$exe = Join-Path $root 'settings-ui\bin\Release\net48\rapoo-tray-settings.exe'
if (!(Test-Path $exe)) { throw "Settings executable not found: $exe" }
if ([string]::IsNullOrWhiteSpace($Output)) { $Output = Join-Path $root ".tools\settings-page-$Page.png" }

$registryPath = 'HKCU:\Software\rapoo-tray\Settings'
$hadKey = Test-Path $registryPath
$hadTheme = $false
$oldTheme = $null
if ($hadKey) {
    try { $oldTheme = Get-ItemPropertyValue -Path $registryPath -Name UiTheme -ErrorAction Stop; $hadTheme = $true } catch { }
}
try {
    if (-not $hadKey) { New-Item -Path $registryPath -Force | Out-Null }
    New-ItemProperty -Path $registryPath -Name UiTheme -PropertyType DWord -Value $Theme -Force | Out-Null
    $process = Start-Process -FilePath $exe -ArgumentList @('--demo', '--page', $Page.ToString()) -PassThru
    for ($i = 0; $i -lt 80 -and $process.MainWindowHandle -eq 0; $i++) {
        Start-Sleep -Milliseconds 200
        $process.Refresh()
    }
    if ($process.MainWindowHandle -eq 0) { throw 'Settings window did not appear.' }
    Start-Sleep -Milliseconds 1200
    $rect = New-Object RapooCapture+RECT
    [RapooCapture]::GetWindowRect($process.MainWindowHandle, [ref]$rect) | Out-Null
    $bitmap = New-Object System.Drawing.Bitmap ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    [RapooCapture]::PrintWindow($process.MainWindowHandle, $hdc, 2) | Out-Null
    $graphics.ReleaseHdc($hdc)
    $graphics.Dispose()
    $bitmap.Save($Output, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    Write-Output $Output
}
finally {
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
    if ($hadKey) {
        if ($hadTheme) { New-ItemProperty -Path $registryPath -Name UiTheme -PropertyType DWord -Value $oldTheme -Force | Out-Null }
        else { Remove-ItemProperty -Path $registryPath -Name UiTheme -ErrorAction SilentlyContinue }
    }
}
