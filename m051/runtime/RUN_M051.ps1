#requires -Version 5.1
#requires -RunAsAdministrator
param([string]$ResultFile = '')
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$root = Split-Path -Parent $PSScriptRoot
& (Join-Path $PSScriptRoot 'VERIFY_PACKAGE.ps1') -Root $root
if (-not [Environment]::Is64BitProcess -or [Environment]::OSVersion.Version.Build -lt 22000) {
    throw 'WINDOWS_11_X64_REQUIRED'
}
. (Join-Path $PSScriptRoot 'Transaction.ps1')
. (Join-Path $PSScriptRoot 'Windows.ps1')
Add-Type -Path (Join-Path $PSScriptRoot 'Native.cs')

$mutex = [Threading.Mutex]::new($false, 'Global\PHASER360_M051_TRANSACTION')
if (-not $mutex.WaitOne(0)) { $mutex.Dispose(); throw 'ANOTHER_M051_TRANSACTION_IS_RUNNING' }
try {
    $runId = (Get-Date -Format 'yyyyMMdd_HHmmss') + '_' + [Guid]::NewGuid().ToString('N').Substring(0,8)
    $runDir = Join-Path $env:SystemDrive "PHASER360_M051_RECOVERY\$runId"
    New-Item -ItemType Directory -Path $runDir -Force | Out-Null
    Copy-Item -LiteralPath (Join-Path $root 'RECOVER_WINRE.cmd') -Destination $runDir
    Copy-Item -LiteralPath (Join-Path $root 'runtime') -Destination $runDir -Recurse
    Copy-Item -LiteralPath (Join-Path $root 'PHASER360_M051_TEST_SIGNING.cer') -Destination $runDir
    $script:M051 = [ordered]@{
        RunDir = $runDir; PackageRoot = $root; Target = $null; BeforePackages = @()
        InfHash = (Get-FileHash (Join-Path $root 'driver\phaser360_m051_mmio_ro.inf') -Algorithm SHA256).Hash
        CertThumbprint = ''; OwnedCertStores = @(); CodeIntegrityOptions = $null
    }
    $ops = @{
        Save = { param($s) Write-M051Journal $s }
        Preflight = {
            param($s)
            $script:M051.Target = Get-M051Target
            Assert-M051CleanTarget $script:M051.Target
            $store = @(Get-M051Store)
            $script:M051.BeforePackages = @($store | ForEach-Object { $_.PublishedName })
            if (@($store | Where-Object { $_.OriginalName -like 'phaser360_m05*.inf' }).Count -or
                (Test-Path 'HKLM:\SYSTEM\CurrentControlSet\Services\phaser360_m051_mmio_ro')) {
                throw 'EXPERIMENTAL_PACKAGE_ALREADY_PRESENT: use its recorded cleanup first'
            }
            $cert = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
                (Join-Path $root 'PHASER360_M051_TEST_SIGNING.cer'))
            if ($cert.HasPrivateKey -or $cert.NotAfter -lt (Get-Date) -or $cert.NotBefore -gt (Get-Date)) {
                throw 'INVALID_PUBLIC_TEST_CERTIFICATE'
            }
            $script:M051.CertThumbprint = $cert.Thumbprint
            $ci = [PhaserM051.Native]::CodeIntegrityOptions()
            $script:M051.CodeIntegrityOptions = ('0x{0:X8}' -f $ci)
            if (($ci -band 1) -ne 0 -and ($ci -band 2) -eq 0) {
                throw 'SIGNING_SESSION_REQUIRED: sesiunea Windows nu permite driverul test-semnat. Nu am instalat nimic. Vezi START_AICI.txt.'
            }
            Write-Host "Target: $($script:M051.Target.InstanceId)"
            Write-Host "Recuperare: $runDir\RECOVER_WINRE.cmd"
        }
        Trust = { param($s) Add-M051Trust $s }
        Stage = { param($s) Invoke-M051Pnp @('/add-driver', (Join-Path $root 'driver\phaser360_m051_mmio_ro.inf')) }
        Discover = { param($s) Find-M051CurrentOwned }
        BeforeBind = {
            param($s)
            $target = Get-M051Target
            if ($target.InstanceId -ine $script:M051.Target.InstanceId) { throw 'TARGET_INSTANCE_CHANGED' }
            Assert-M051CleanTarget $target
        }
        Bind = {
            param($s)
            Invoke-M051Pnp @('/add-driver', (Join-Path $root 'driver\phaser360_m051_mmio_ro.inf'), '/install')
            Disable-M051OwnedService $s.OwnedPackage
        }
        Snapshot = { param($s) Read-M051Snapshot }
        Remove = { param($s) Remove-M051OwnedPackage $s }
        CheckClean = { param($s) Test-M051Clean }
        Untrust = { param($s) Remove-M051Trust }
    }
    $result = Invoke-M051Transaction $ops
    $report = @("STATUS=$($result.Phase)", "CLEAN=$($result.Clean)", "ERROR=$($result.Error)",
        "CLEANUP_ERRORS=$($result.CleanupErrors -join '; ')", "RECOVERY=$runDir\RECOVER_WINRE.cmd",
        'AUDIO_PLAYBACK=NOT_IMPLEMENTED')
    if ($null -ne $script:M051.Target) {
        $target = $script:M051.Target
        $report += @("TARGET_PROBLEM=$($target.Problem)", "TARGET_SERVICE=$($target.Service)",
            "TARGET_INF=$($target.Inf)", "TARGET_DRIVER_VERSION=$($target.DriverVersion)",
            "TARGET_DRIVER_PROVIDER=$($target.DriverProvider)")
    }
    $report | Set-Content -LiteralPath (Join-Path $runDir 'RESULT.txt') -Encoding UTF8
    $report | ForEach-Object { Write-Host $_ }
    $zip = Join-Path $root "RESULT_M051_$runId.zip"
    try {
        Compress-Archive -Path (Join-Path $runDir '*') -DestinationPath $zip
        Write-Host "Trimite fisierul: $zip"
    } catch { Write-Host "Raportul este in $runDir (arhivarea a esuat: $($_.Exception.Message))." }
    if (-not [string]::IsNullOrWhiteSpace($ResultFile)) {
        [ordered]@{ Transaction=$result; RecoveryDirectory=$runDir; ResultZip=$zip } |
            ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $ResultFile -Encoding UTF8 -ErrorAction Stop
    }
    if (-not $result.Clean) { exit 2 }
    if ($result.Error) { exit 1 }
} finally { $mutex.ReleaseMutex(); $mutex.Dispose() }
