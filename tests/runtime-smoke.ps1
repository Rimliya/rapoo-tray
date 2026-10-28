$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
Add-Type @"
using System;
using System.Runtime.InteropServices;
public static class RapooRuntime {
    public delegate bool EnumWindowsProc(IntPtr hwnd, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int Left, Top, Right, Bottom; }
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr FindWindow(string cls, string title);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hwnd);
    [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hwnd, uint msg, IntPtr wparam, IntPtr lparam);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hwnd, out RECT rect);
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr hwnd, IntPtr hdc, uint flags);
    [DllImport("user32.dll")] public static extern IntPtr SetThreadDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc proc, IntPtr lParam);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hwnd, out uint processId);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr hwnd, System.Text.StringBuilder name, int max);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetWindowText(IntPtr hwnd, System.Text.StringBuilder name, int max);
}
"@

function Get-RapooWindows([int]$ProcessId) {
    $items = [System.Collections.Generic.List[string]]::new()
    $callback = [RapooRuntime+EnumWindowsProc]{
        param([IntPtr]$window, [IntPtr]$unused)
        $owner = 0
        [RapooRuntime]::GetWindowThreadProcessId($window, [ref]$owner) | Out-Null
        if ($owner -eq $ProcessId) {
            $class = [System.Text.StringBuilder]::new(256)
            $title = [System.Text.StringBuilder]::new(256)
            [RapooRuntime]::GetClassName($window, $class, 256) | Out-Null
            [RapooRuntime]::GetWindowText($window, $title, 256) | Out-Null
            $items.Add("$window|$class|$title|visible=$([RapooRuntime]::IsWindowVisible($window))")
        }
        return $true
    }
    [RapooRuntime]::EnumWindows($callback, [IntPtr]::Zero) | Out-Null
    return $items
}

function Send-RapooCommand([string]$Command) {
    $pipe = [System.IO.Pipes.NamedPipeClientStream]::new('.', 'rapoo-tray-ui-v1', [System.IO.Pipes.PipeDirection]::InOut)
    try {
        $pipe.Connect(3000)
        $request = [System.Text.Encoding]::UTF8.GetBytes($Command)
        $pipe.Write($request, 0, $request.Length)
        $pipe.Flush()
        $memory = [System.IO.MemoryStream]::new()
        $buffer = [byte[]]::new(65536)
        while (($read = $pipe.Read($buffer, 0, $buffer.Length)) -gt 0) { $memory.Write($buffer, 0, $read) }
        return [System.Text.Encoding]::UTF8.GetString($memory.ToArray())
    }
    finally { $pipe.Dispose() }
}

function Capture-Window([IntPtr]$Handle, [string]$Path) {
    $rect = New-Object RapooRuntime+RECT
    [RapooRuntime]::GetWindowRect($Handle, [ref]$rect) | Out-Null
    $bitmap = [System.Drawing.Bitmap]::new(($rect.Right - $rect.Left), ($rect.Bottom - $rect.Top))
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $hdc = $graphics.GetHdc()
    [RapooRuntime]::PrintWindow($Handle, $hdc, 2) | Out-Null
    $graphics.ReleaseHdc($hdc)
    $graphics.Dispose()
    $bitmap.Save($Path, [System.Drawing.Imaging.ImageFormat]::Png)
    $bitmap.Dispose()
}

function Send-ConfiguredHotkey([int]$Modifiers, [int]$VirtualKey) {
    $keys = [System.Collections.Generic.List[byte]]::new()
    if (($Modifiers -band 2) -ne 0) { $keys.Add(0x11) }
    if (($Modifiers -band 1) -ne 0) { $keys.Add(0x12) }
    if (($Modifiers -band 4) -ne 0) { $keys.Add(0x10) }
    if (($Modifiers -band 8) -ne 0) { $keys.Add(0x5B) }
    foreach ($key in $keys) {
        [RapooRuntime]::keybd_event($key, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 45
    }
    [RapooRuntime]::keybd_event([byte]$VirtualKey, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 90
    [RapooRuntime]::keybd_event([byte]$VirtualKey, 0, 2, [UIntPtr]::Zero)
    for ($i = $keys.Count - 1; $i -ge 0; $i--) {
        [RapooRuntime]::keybd_event($keys[$i], 0, 2, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds 30
    }
}

$root = Split-Path -Parent $PSScriptRoot
$testExe = Join-Path $root 'bin\rapoo-tray.exe'
$quickShot = Join-Path $root '.tools\quick-panel.png'
$osdShot = Join-Path $root '.tools\osd-notification.png'
if (!(Test-Path $testExe)) { throw "Native test binary not found: $testExe" }
[RapooRuntime]::SetThreadDpiAwarenessContext([IntPtr](-4)) | Out-Null

$installedProcess = Get-Process rapoo-tray -ErrorAction SilentlyContinue | Select-Object -First 1
$installedWasRunning = $null -ne $installedProcess
$installedExe = if ($installedProcess) { $installedProcess.Path } else { $null }
$testProcess = $null
try {
    Get-Process rapoo-tray -ErrorAction SilentlyContinue | Stop-Process -Force
    Start-Sleep -Milliseconds 500
    $testProcess = Start-Process -FilePath $testExe -PassThru
    $mainWindow = [IntPtr]::Zero
    for ($i = 0; $i -lt 60 -and $mainWindow -eq [IntPtr]::Zero; $i++) {
        Start-Sleep -Milliseconds 200
        $mainWindow = [RapooRuntime]::FindWindow('RapooTrayMessageWnd', 'RapooTray')
    }
    if ($mainWindow -eq [IntPtr]::Zero) { throw 'Native tray window did not appear.' }
    Start-Sleep -Milliseconds 900

    $state = Send-RapooCommand 'GET_STATE'
    if (!$state.StartsWith('OK')) { throw 'GET_STATE did not return OK.' }
    if ($state -notmatch '(?m)^OSD\|') { throw 'GET_STATE omitted OSD settings.' }
    if ($state -notmatch '(?m)^BATTERY_SETTINGS\|') { throw 'GET_STATE omitted battery settings.' }
    $osdLine = [regex]::Match($state, '(?m)^OSD\|([^\r\n]+)').Groups[1].Value.Split('|')
    if ($osdLine.Length -lt 7) { throw 'OSD state has an invalid shape.' }
    $modifiers = $osdLine[2]
    $virtualKey = $osdLine[3]
    $displayMask = $osdLine[5]
    $panelMask = $osdLine[6]
    if ($osdLine[4] -ne '1') { throw 'Configured global hotkey was not registered.' }

    $osdResponse = Send-RapooCommand "SET_OSD_CONFIG|1|1|$modifiers|$virtualKey|$displayMask|$panelMask"
    if (!$osdResponse.StartsWith('OK')) { throw 'SET_OSD_CONFIG failed.' }
    $batteryLine = [regex]::Match($state, '(?m)^BATTERY_SETTINGS\|([^\r\n]+)').Groups[1].Value.Split('|')
    $batteryResponse = Send-RapooCommand "SET_BATTERY_CONFIG|$($batteryLine[0])|$($batteryLine[1])|$($batteryLine[2])|$($batteryLine[3])|$($batteryLine[4])"
    if (!$batteryResponse.StartsWith('OK')) { throw 'SET_BATTERY_CONFIG failed.' }
    $history = Send-RapooCommand 'GET_BATTERY_HISTORY|1'
    if (!$history.StartsWith('OK') -or $history -notmatch '(?m)^HISTORY\|') { throw 'GET_BATTERY_HISTORY failed.' }

    Send-ConfiguredHotkey ([int]$modifiers) ([int]$virtualKey)
    $quickWindow = [IntPtr]::Zero
    $quickVisible = $false
    for ($i = 0; $i -lt 60 -and !$quickVisible; $i++) {
        Start-Sleep -Milliseconds 150
        if ($quickWindow -eq [IntPtr]::Zero) { $quickWindow = [RapooRuntime]::FindWindow('RapooQuickPanelWnd', 'RapooQuickPanel') }
        if ($quickWindow -ne [IntPtr]::Zero) { $quickVisible = [RapooRuntime]::IsWindowVisible($quickWindow) }
    }
    if (!$quickVisible) {
        Write-Output 'keyboard injection was not observed; validating the registered WM_HOTKEY path'
        [RapooRuntime]::PostMessage($mainWindow, 0x0312, [IntPtr]0x7101, [IntPtr]0) | Out-Null
        for ($i = 0; $i -lt 60 -and !$quickVisible; $i++) {
            Start-Sleep -Milliseconds 150
            $quickVisible = [RapooRuntime]::IsWindowVisible($quickWindow)
        }
    }
    if ($quickWindow -eq [IntPtr]::Zero -or !$quickVisible) {
        Get-RapooWindows $testProcess.Id | ForEach-Object { Write-Output $_ }
        throw 'Quick panel did not open from WM_HOTKEY.'
    }
    Start-Sleep -Milliseconds 350
    Capture-Window $quickWindow $quickShot
    [RapooRuntime]::PostMessage($quickWindow, 0x0100, [IntPtr]0x1B, [IntPtr]0) | Out-Null
    Start-Sleep -Milliseconds 250
    if ([RapooRuntime]::IsWindowVisible($quickWindow)) { throw 'Quick panel did not close with Escape.' }

    [RapooRuntime]::PostMessage($mainWindow, 0x8001, [IntPtr]0, [IntPtr]0x0202) | Out-Null
    $osdWindow = [IntPtr]::Zero
    $osdVisible = $false
    for ($i = 0; $i -lt 30 -and !$osdVisible; $i++) {
        Start-Sleep -Milliseconds 120
        if ($osdWindow -eq [IntPtr]::Zero) { $osdWindow = [RapooRuntime]::FindWindow('RapooOsdPopupWnd', 'RapooOSD') }
        if ($osdWindow -ne [IntPtr]::Zero) { $osdVisible = [RapooRuntime]::IsWindowVisible($osdWindow) }
    }
    if (!$osdVisible) { throw 'Configured status OSD did not appear after tray refresh.' }
    Capture-Window $osdWindow $osdShot
    Write-Output 'runtime smoke test passed'
    Write-Output $quickShot
    Write-Output $osdShot
}
finally {
    if ($testProcess -and !$testProcess.HasExited) { Stop-Process -Id $testProcess.Id -Force }
    Start-Sleep -Milliseconds 300
    if ($installedWasRunning -and (Test-Path $installedExe)) {
        Start-Process -FilePath $installedExe -WindowStyle Hidden | Out-Null
    }
}
