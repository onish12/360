#requires -Version 5.1
#requires -RunAsAdministrator
param()
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Split-Path -Parent $PSScriptRoot
$mutex=$null; $locked=$false; $exitCode=1
try {
    & (Join-Path $PSScriptRoot 'VERIFY_PACKAGE.ps1') -Root $root
    if (-not [Environment]::Is64BitProcess -or $env:PROCESSOR_ARCHITECTURE -ine 'AMD64' -or
        [Environment]::OSVersion.Version.Build -lt 22000) { throw 'WINDOWS_11_X64_REQUIRED' }
    . (Join-Path $PSScriptRoot 'Transaction.ps1')
    . (Join-Path $PSScriptRoot 'Windows.ps1')
    . (Join-Path $PSScriptRoot 'AudioDecision.ps1')
    . (Join-Path $PSScriptRoot 'AudioWindows.ps1')
    . (Join-Path $PSScriptRoot 'Transition.ps1')
    Add-Type -Path (Join-Path $PSScriptRoot 'Native.cs')
    $mutex=[Threading.Mutex]::new($false,'Global\PHASER360_AUDIO_AUTO')
    try { $locked=$mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked=$true }
    if (-not $locked) { throw 'ANOTHER_AUDIO_AUTO_RUN_IS_ACTIVE' }
    $runId=(Get-Date -Format 'yyyyMMdd_HHmmss')+'_'+[Guid]::NewGuid().ToString('N').Substring(0,8)
    $runDir=Join-Path $env:SystemDrive "PHASER360_AUDIO\$runId"
    $report=Join-Path $runDir 'Report'
    New-Item -ItemType Directory -Path $report -Force -ErrorAction Stop | Out-Null
    $script:AudioPaths=@{ RunDir=$runDir; Report=$report; PackageRoot=$root }
    Copy-Item -LiteralPath (Join-Path $root 'BUILD_AUDIT.txt') -Destination $report -ErrorAction Stop
    $ops=@{
        Save={ param($s) Write-AudioJson 'analysis.json' $s }
        ReadState={ Read-AudioState }
        Export={ param($s) Export-AudioPackage $s }
        Capture={ param($s) Read-AudioContext $s }
        Probe={ Invoke-AudioProbe }
        Transition={ param($s) Invoke-AudioHandoff $s }
    }
    $r=Invoke-AudioAuto $ops
    $lines=@('PHASER360 AUDIO AUTO',"STATUS=$($r.Status)",'AUDIO_PLAYBACK=NOT_IMPLEMENTED')
    if ($null -ne $r.Decision) {
        $lines+=@("ACTIUNE=$($r.Decision.Message)","KERNEL=$($r.Decision.KernelVersion)",
            "PROBA_SELECTATA=$($r.Decision.Probe)","TRANZITIE_SELECTATA=$($r.Decision.Transition)",
            'STERGERE_PACHET_INTEL=False')
        Write-AudioJson 'build-selection.json' $r.Decision
    }
    if ($null -ne $r.State) {
        $t=$r.State.Target
        $lines+="DRIVER=$($t.Service); INF=$($t.Inf); VERSION=$($t.DriverVersion); PROBLEM=$($t.Problem)"
    }
    if ($null -ne $r.Backup) { $lines+=@("BACKUP=$($r.Backup.Path)","BACKUP_VERIFIED=$($r.Backup.Verified)") }
    if ($r.Error) { $lines+="ERROR=$($r.Error)" }
    if ($r.Warnings.Count) { $lines+="AVERTIZARI=$($r.Warnings -join '; ')" }
    if ($null -ne $r.Context -and $r.Context.Warnings.Count) { $lines+="CITIRI_INCOMPLETE=$($r.Context.Warnings -join '; ')" }
    if ($null -ne $r.Probe) { $lines+="RECOVERY=$($r.Probe.RecoveryDirectory)\RECOVER_WINRE.cmd" }
    if ($null -ne $r.Probe -and $null -ne (Get-AudioValue $r.Probe 'Handoff')) {
        $lines+=@("STARE_FINALA=$($r.Probe.Handoff.FinalState)",
            "PACHET_INTEL_PASTRAT=$($r.Probe.Handoff.IntelPackagePreserved)",
            'Tranzitia nu reinstaleaza Intel. UNBOUND inseamna controler fara driver, nu sunet reparat.')
    }
    $lines+=@('Redarea audio nu este implementata. Pachetul contine proba compilata de citire.',
        'Exportul nu este un backup complet Windows. WinRE nu a fost testat prin pornire.',"DOSAR_REZULTAT=$report")
    $lines | Set-Content -LiteralPath (Join-Path $report 'REZULTAT.txt') -Encoding UTF8
    $lines | ForEach-Object { Write-Host $_ }
    $zip=Join-Path $root "RESULT_AUDIO_$runId.zip"
    try { Compress-Archive -Path (Join-Path $report '*') -DestinationPath $zip -ErrorAction Stop }
    catch {
        $zip=Join-Path $runDir "RESULT_AUDIO_$runId.zip"
        Compress-Archive -Path (Join-Path $report '*') -DestinationPath $zip -ErrorAction Stop
    }
    Write-Host "Trimite arhiva: $zip"
    Write-Host 'Copia integrala a driverului ramane in dosarul DriverBackup. Nimic nu este incarcat automat pe internet.'
    if ($r.Status -eq 'SNAPSHOT_COMPLETE_CLEAN') { $exitCode=0 }
    elseif ($r.Status -like 'PREPARED_*') { $exitCode=3 }
    elseif ($r.Status -in @('RECOVERY_REQUIRED','PROBE_RESULT_UNCONFIRMED')) { $exitCode=2 }
} catch { Write-Host ("EROARE: "+$_.Exception.Message) }
finally {
    if ($locked) { $mutex.ReleaseMutex() }
    if ($null -ne $mutex) { $mutex.Dispose() }
}
exit $exitCode
