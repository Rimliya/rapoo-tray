$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName UIAutomationClient
Add-Type -AssemblyName UIAutomationTypes

function Find-ById($Root, [string]$Id) {
    $condition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::AutomationIdProperty, $Id)
    return $Root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
}

function Find-ByName($Root, [string]$Name) {
    $condition = New-Object System.Windows.Automation.PropertyCondition([System.Windows.Automation.AutomationElement]::NameProperty, $Name)
    return $Root.FindFirst([System.Windows.Automation.TreeScope]::Descendants, $condition)
}

function Wait-For([scriptblock]$Probe, [string]$Failure, [int]$Attempts = 40) {
    for ($i = 0; $i -lt $Attempts; $i++) {
        if (& $Probe) { return }
        Start-Sleep -Milliseconds 150
    }
    throw $Failure
}

$root = Split-Path -Parent $PSScriptRoot
$exe = Join-Path $root 'settings-ui\bin\Release\net48\rapoo-tray-settings.exe'
if (!(Test-Path $exe)) { throw "Settings executable not found: $exe" }
$process = $null
try {
    $process = Start-Process -FilePath $exe -ArgumentList @('--demo', '--page', '2') -PassThru
    for ($i = 0; $i -lt 80 -and $process.MainWindowHandle -eq 0; $i++) {
        Start-Sleep -Milliseconds 200
        $process.Refresh()
    }
    if ($process.MainWindowHandle -eq 0) { throw 'Settings window did not appear.' }
    Start-Sleep -Milliseconds 1000
    $window = [System.Windows.Automation.AutomationElement]::FromHandle($process.MainWindowHandle)

    $quickToggle = Find-ById $window 'QuickPanelToggle'
    if ($quickToggle -eq $null) { throw 'QuickPanelToggle was not found.' }
    $togglePattern = $quickToggle.GetCurrentPattern([System.Windows.Automation.TogglePattern]::Pattern)
    $initialToggle = $togglePattern.Current.ToggleState
    if ($initialToggle -ne [System.Windows.Automation.ToggleState]::On) { throw 'Quick panel demo state should start enabled.' }
    $togglePattern.Toggle()
    $saveOsd = Find-ById $window 'OsdSaveButton'
    if ($saveOsd -eq $null) { throw 'OsdSaveButton was not found.' }
    $saveOsd.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 1200
    if ($togglePattern.Current.ToggleState -ne [System.Windows.Automation.ToggleState]::Off) {
        throw 'Quick panel toggle did not persist after save.'
    }
    $togglePattern.Toggle()
    $saveOsd.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 1200
    if ($togglePattern.Current.ToggleState -ne [System.Windows.Automation.ToggleState]::On) {
        throw 'Quick panel toggle did not restore to enabled.'
    }

    $navBattery = Find-ById $window 'NavBattery'
    if ($navBattery -eq $null) { throw 'NavBattery was not found.' }
    $navBattery.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 900
    $samples = Find-ById $window 'BatterySamplesText'
    if ($samples -eq $null -or $samples.Current.Name -ne '48') { throw 'Battery history page did not load 24H samples.' }
    $sevenDay = Find-ByName $window '7D'
    if ($sevenDay -eq $null) { throw '7D range selector was not found.' }
    $sevenDay.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 900
    if ($samples -eq $null -or $samples.Current.Name -ne '168') {
        throw "Battery history UI did not update to 168 samples; got '$($samples.Current.Name)'."
    }
    $navDiagnostics = Find-ById $window 'NavDiagnostics'
    if ($navDiagnostics -eq $null) { throw 'NavDiagnostics was not found.' }
    $navDiagnostics.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 900
    $pollingStart = Find-ById $window 'PollingStartButton'
    if ($pollingStart -eq $null) { throw 'PollingStartButton was not found.' }
    $pollingStart.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 600
    if ($pollingStart.Current.Name -ne '停止采样') { $pollingStatus = Find-ById $window 'PollingStatusText'; Write-Output ('polling_button=' + $pollingStart.Current.Name); if ($pollingStatus) { Write-Output ('polling_status=' + $pollingStatus.Current.Name) }; $footerStatus = Find-ById $window 'StatusText'; if ($footerStatus) { Write-Output ('footer_status=' + $footerStatus.Current.Name) }; throw 'Polling probe did not enter running state.' }
    $pollingStart.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 350

    $snapshotDir = Join-Path $env:APPDATA 'rapoo-tray\snapshots'
    $beforeSnapshots = @()
    if (Test-Path $snapshotDir) { $beforeSnapshots = @(Get-ChildItem $snapshotDir -File | Where-Object { $_.Extension -in '.rapoo','.rapoo-snapshot' } | Select-Object -ExpandProperty FullName) }
    $navAbout = Find-ById $window 'NavAbout'
    if ($navAbout -eq $null) { throw 'NavAbout was not found.' }
    $navAbout.GetCurrentPattern([System.Windows.Automation.SelectionItemPattern]::Pattern).Select()
    Start-Sleep -Milliseconds 700
    $snapshotButton = Find-ByName $window '立即备份'
    if ($snapshotButton -eq $null) { throw 'Snapshot backup button was not found.' }
    $snapshotButton.GetCurrentPattern([System.Windows.Automation.InvokePattern]::Pattern).Invoke()
    Start-Sleep -Milliseconds 700
    $afterSnapshots = @()
    if (Test-Path $snapshotDir) { $afterSnapshots = @(Get-ChildItem $snapshotDir -File | Where-Object { $_.Extension -in '.rapoo','.rapoo-snapshot' } | Select-Object -ExpandProperty FullName) }
    $createdSnapshots = @($afterSnapshots | Where-Object { $beforeSnapshots -notcontains $_ })
    if ($createdSnapshots.Count -lt 1) { throw 'Snapshot backup did not create a snapshot file.' }
    $snapshotText = Get-Content -LiteralPath $createdSnapshots[0] -Raw
    if ($snapshotText -notmatch 'rapoo-tray-settings-snapshot' -or $snapshotText -notmatch 'osd' -or $snapshotText -notmatch 'battery') { throw 'Snapshot did not include full settings payload.' }
    foreach ($created in $createdSnapshots) { Remove-Item -LiteralPath $created -Force }

    Write-Output 'settings UI smoke test passed'
}
finally {
    if ($process -and !$process.HasExited) { Stop-Process -Id $process.Id -Force }
}
