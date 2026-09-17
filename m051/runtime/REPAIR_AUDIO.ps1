#requires -Version 5.1
#requires -RunAsAdministrator
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Split-Path -Parent $PSScriptRoot
$mutex=$null; $locked=$false; $exitCode=1
try {
    & (Join-Path $PSScriptRoot 'VERIFY_PACKAGE.ps1') -Root $root
    if (-not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ine 'AMD64') { throw 'WINDOWS_X64_REQUIRED' }
    foreach ($file in @('Transaction.ps1','Windows.ps1','AudioDecision.ps1','AudioWindows.ps1','Transition.ps1','Repair.ps1')) {
        . (Join-Path $PSScriptRoot $file)
    }
    $mutex=[Threading.Mutex]::new($false,'Global\PHASER360_M051_TRANSACTION')
    try { $locked=$mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked=$true }
    if (-not $locked) { throw 'M051_TRANSACTION_IS_RUNNING' }
    $dir=Join-Path $env:SystemDrive 'PHASER360_M051_RECOVERY\20260916_211227_13c9e569'
    $journal=Join-Path $dir 'journal.json'
    $record=Get-Content -LiteralPath $journal -Raw -ErrorAction Stop | ConvertFrom-Json
    Assert-ReviewedRepairRecord $record $dir
    $report=Join-Path $dir ('Repair_'+(Get-Date -Format 'yyyyMMdd_HHmmss')+'_'+[Guid]::NewGuid().ToString('N').Substring(0,8))
    New-Item -ItemType Directory -Path $report -ErrorAction Stop | Out-Null
    Copy-Item -LiteralPath $journal -Destination (Join-Path $report 'journal-before.json')
    $script:M051=$record.Context
    $script:M051.Handoff.FinalState='NOT_CHECKED'
    $script:M051.Handoff.FinalTarget=$null
    $script:M051.Handoff.IntelPackagePreserved=$null
    $script:AudioPaths=@{ RunDir=$dir; Report=$report; PackageRoot=$root }
    $ops=@{
        Save={ param($r) Write-AudioJson 'repair-result.json' $r }
        Validate={ Assert-ReviewedRepairRecord $record $dir }
        Target={ Get-M051Target }
        Discover={ Find-M051CurrentOwned }
        Remove={ param($s) Remove-ReviewedDetachedPackage $s }
        Check={ param($s) Test-HandoffClean $s }
        Untrust={ Remove-M051Trust }
    }
    $r=Invoke-DetachedRepair $record $ops
    $record.Transaction.Clean=($r.Status -eq 'RECOVERY_CLEANUP_COMPLETE')
    $record.Transaction.Phase=if ($record.Transaction.Clean) { 'SNAPSHOT_COMPLETE_CLEAN' } else { 'RECOVERY_REQUIRED' }
    $record.Transaction.CleanupErrors=if ($record.Transaction.Clean) { @() } else { @($r.Error) }
    Write-M051Journal $record.Transaction
    foreach ($name in @('journal.json','pnputil.log','snapshot.json','snapshot.bin')) {
        $path=Join-Path $dir $name
        if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination $report }
    }
    $preserved=if ($null -eq $r.IntelPackagePreserved) { 'NOT_CHECKED' } else { [string]$r.IntelPackagePreserved }
    $lines=@("STATUS=$($r.Status)","ERROR=$($r.Error)",'PROBA_REPETATA=False',
        "PACHET_INTEL_PASTRAT=$preserved",'AUDIO_PLAYBACK=NOT_IMPLEMENTED',"RAPORT=$report")
    $lines | Set-Content -LiteralPath (Join-Path $report 'REZULTAT.txt') -Encoding UTF8
    $lines | ForEach-Object { Write-Host $_ }
    $zip=Join-Path $root ('RESULT_REPAIR_'+(Split-Path -Leaf $report)+'.zip')
    try { Compress-Archive -Path (Join-Path $report '*') -DestinationPath $zip -ErrorAction Stop }
    catch { $zip=$report+'.zip'; Compress-Archive -Path (Join-Path $report '*') -DestinationPath $zip -ErrorAction Stop }
    Write-Host "Trimite arhiva: $zip"
    if ($record.Transaction.Clean) { $exitCode=0 } else { $exitCode=2 }
} catch { Write-Host ('EROARE: '+$_.Exception.Message) }
finally { if ($locked) { $mutex.ReleaseMutex() }; if ($null -ne $mutex) { $mutex.Dispose() } }
exit $exitCode
