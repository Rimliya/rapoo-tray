$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class InstallerCapture {
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wParam, IntPtr lParam);
}
"@
$root = Split-Path -Parent $PSScriptRoot
$setup = Join-Path $root 'dist\rapoo-tray-setup.exe'
$output = Join-Path $root '.tools\installer-1.14.0.png'
if (!(Test-Path $setup)) { throw "Installer not found: $setup" }
[InstallerCapture]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null
$process = $null
try {
    $process = Start-Process -FilePath $setup -PassThru
    for ($i = 0; $i -lt 80 -and $process.MainWindowHandle -eq 0; $i++) {
        Start-Sleep -Milliseconds 200
        $process.Refresh()
    }
    if ($process.MainWindowHandle -eq 0) { throw 'Installer window did not appear.' }
    Start-Sleep -Milliseconds 900
    $rect = New-Object InstallerCapture+RECT
    [InstallerCapture]::GetWindowRect($process.MainWindowHandle, [ref]$rect) | Out-Null
    $bitmap = New-Object System.Drawing.Bitmap ($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    [InstallerCapture]::PrintWindow($process.MainWindowHandle, $hdc, 2) | Out-Null
    $graphics.ReleaseHdc($hdc)
    $graphics.Dispose()
    $bitmap.Save($output, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
    Write-Output $output
    Write-Output ("title=" + $process.MainWindowTitle)
}
finally {
    if ($process -and !$process.HasExited) {
        [InstallerCapture]::PostMessage($process.MainWindowHandle, 0x0010, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
        Start-Sleep -Milliseconds 300
        if (!$process.HasExited) { Stop-Process -Id $process.Id -Force }
    }
}
