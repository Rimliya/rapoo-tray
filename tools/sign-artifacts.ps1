param(
    [Parameter(Mandatory = $true)]
    [string[]]$Paths,
    [string]$CertificateBase64 = $env:WINDOWS_CERTIFICATE_BASE64,
    [string]$CertificatePassword = $env:WINDOWS_CERTIFICATE_PASSWORD,
    [string]$CertificateThumbprint = $env:WINDOWS_CERTIFICATE_THUMBPRINT,
    [switch]$MachineStore,
    [string]$TimestampUrl = "http://timestamp.digicert.com"
)

$ErrorActionPreference = 'Stop'
if ($Paths.Count -eq 0) { throw 'No artifacts were supplied for signing.' }

$certificate = $null
$importedThumbprint = $null
$tempPfx = $null
try {
    if ($CertificateThumbprint) {
        $store = if ($MachineStore) { 'Cert:\LocalMachine\My' } else { 'Cert:\CurrentUser\My' }
        $certificate = Get-ChildItem $store | Where-Object { $_.Thumbprint -eq ($CertificateThumbprint -replace '\s', '') -and $_.HasPrivateKey } | Select-Object -First 1
        if (!$certificate) { throw "Code-signing certificate $CertificateThumbprint was not found with a private key." }
    }
    elseif ($CertificateBase64) {
        if ([string]::IsNullOrWhiteSpace($CertificatePassword)) {
            throw 'WINDOWS_CERTIFICATE_PASSWORD is required when a PFX is supplied.'
        }
        $tempPfx = Join-Path ([IO.Path]::GetTempPath()) ('rapoo-sign-' + [Guid]::NewGuid().ToString('N') + '.pfx')
        [IO.File]::WriteAllBytes($tempPfx, [Convert]::FromBase64String(($CertificateBase64 -replace '\s', '')))
        $securePassword = ConvertTo-SecureString $CertificatePassword -AsPlainText -Force
        $certificate = Import-PfxCertificate -FilePath $tempPfx -CertStoreLocation 'Cert:\CurrentUser\My' -Password $securePassword
        if (!$certificate -or !$certificate.HasPrivateKey) { throw 'Imported PFX does not contain a private key.' }
        $importedThumbprint = $certificate.Thumbprint
    }
    else {
        throw 'No code-signing certificate was configured. Set WINDOWS_CERTIFICATE_BASE64 and WINDOWS_CERTIFICATE_PASSWORD, or provide -CertificateThumbprint for a local test.'
    }

    foreach ($path in $Paths) {
        $resolved = (Resolve-Path -LiteralPath $path -ErrorAction Stop).Path
        Write-Host "[sign] $resolved"
        $signParameters = @{
            LiteralPath = $resolved
            Certificate = $certificate
            HashAlgorithm = 'SHA256'
        }
        if (![string]::IsNullOrWhiteSpace($TimestampUrl)) { $signParameters.TimestampServer = $TimestampUrl }
        $signature = Set-AuthenticodeSignature @signParameters
        if ($signature.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
            throw "Signing failed for $resolved`: $($signature.StatusMessage)"
        }
        $verified = Get-AuthenticodeSignature -LiteralPath $resolved
        if ($verified.Status -ne [System.Management.Automation.SignatureStatus]::Valid) {
            throw "Signature verification failed for $resolved`: $($verified.StatusMessage)"
        }
        Write-Host "[verified] $($verified.SignerCertificate.Subject)"
    }
}
finally {
    if ($importedThumbprint) { Remove-Item ("Cert:\CurrentUser\My\" + $importedThumbprint) -Force -ErrorAction SilentlyContinue }
    if ($certificate) { $certificate.Dispose() }
    if ($tempPfx -and (Test-Path -LiteralPath $tempPfx)) { Remove-Item -LiteralPath $tempPfx -Force }
}