#requires -Version 5.1
#requires -RunAsAdministrator
# Run only this copy from a recorded recovery directory, after Windows boots.
param()
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
. (Join-Path $PSScriptRoot 'Transaction.ps1')
. (Join-Path $PSScriptRoot 'Windows.ps1')
$dir = Split-Path -Parent $PSScriptRoot
$record = Get-Content -LiteralPath (Join-Path $dir 'journal.json') -Raw | ConvertFrom-Json
if ($record.Transaction.Version -ne '0.5.1') { throw 'WRONG_RECOVERY_VERSION' }
$script:M051 = $record.Context
$handoff=($null -ne $script:M051.PSObject.Properties['Mode'] -and $script:M051.Mode -eq 'INTEL_HANDOFF_1')
if ($handoff) {
    . (Join-Path $PSScriptRoot 'AudioDecision.ps1')
    . (Join-Path $PSScriptRoot 'AudioWindows.ps1')
    . (Join-Path $PSScriptRoot 'Transition.ps1')
    Add-Type -Path (Join-Path $PSScriptRoot 'DeviceBinding.dll')
}
if ([IO.Path]::GetFullPath($script:M051.RunDir) -ine [IO.Path]::GetFullPath($dir)) {
    throw 'RECOVERY_DIRECTORY_MISMATCH'
}
$mutex = [Threading.Mutex]::new($false, 'Global\PHASER360_M051_TRANSACTION')
if (-not $mutex.WaitOne(0)) { $mutex.Dispose(); throw 'M051_TRANSACTION_IS_RUNNING' }
try {
    $errors = @()
    if ($record.Transaction.StageAttempted) {
        try {
            $p = Find-M051CurrentOwned
            if ($null -ne $p) {
                $record.Transaction.OwnedPackage = $p
                if ($handoff) { Remove-HandoffOwnedPackage $record.Transaction }
                else { Remove-M051OwnedPackage $record.Transaction }
            }
            if ($handoff) { Test-HandoffClean $record.Transaction }
            else { Test-M051Clean }
        } catch { $errors += $_.Exception.Message }
    }
    if ($record.Transaction.TrustAttempted) {
        try { Remove-M051Trust } catch { $errors += $_.Exception.Message }
    }
    if ($errors.Count) { throw ($errors -join '; ') }
    Write-Host 'RECOVERY_CLEANUP=PASS; no Intel package installed'
} finally { $mutex.ReleaseMutex(); $mutex.Dispose() }
