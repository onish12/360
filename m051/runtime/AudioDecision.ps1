# All Windows operations are injected for offline failure tests.
Set-StrictMode -Version 2
function Get-AudioValue($Object, [string]$Name) {
    if ($null -eq $Object) { return $null }
    if ($Object -is [Collections.IDictionary]) { return $Object[$Name] }
    $p=$Object.PSObject.Properties[$Name]
    if ($null -ne $p) { return $p.Value }
    return $null
}
function New-AudioDecision([string]$Code,[string]$Message,[bool]$Export=$false,[bool]$Probe=$false) {
    [pscustomobject]@{ Code=$Code; Message=$Message; Export=$Export; Probe=$Probe
        Kernel='phaser360_m051_mmio_ro'; KernelVersion='0.5.1.0'
        AudioPlayback='NOT_IMPLEMENTED'; DeleteExistingDriver=$false }
}
function Get-AudioDecision($State) {
    $t=Get-AudioValue $State 'Target'
    if ($null -eq $t -or @($t.HardwareIds) -notcontains 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06') {
        return New-AudioDecision 'TARGET_MISMATCH' 'Controlerul nu corespunde profilului Phaser360.'
    }
    if ((Get-AudioValue $State 'ExperimentalPresent') -ne $false) {
        return New-AudioDecision 'EXPERIMENTAL_RECOVERY_REQUIRED' 'Exista un pachet experimental anterior. Curatarea necesita jurnalul care ii dovedeste apartenenta.'
    }
    if ($t.Problem -eq 28 -and [string]::IsNullOrWhiteSpace($t.Service) -and [string]::IsNullOrWhiteSpace($t.Inf)) {
        $ci=Get-AudioValue $State 'CodeIntegrityOptions'
        if ($ci -isnot [uint32]) {
            return New-AudioDecision 'SIGNING_STATE_UNKNOWN' 'Starea activa a verificarilor de semnatura nu a putut fi citita.'
        }
        if (($ci -band 1) -ne 0 -and ($ci -band 2) -eq 0) {
            return New-AudioDecision 'SIGNING_SESSION_REQUIRED' 'Controler liber. Sesiunea Windows nu permite driverul semnat pentru test; vezi START_AICI.txt.'
        }
        return New-AudioDecision 'RUN_PROBE' 'Controler liber; rulez proba compilata si curatarea ei automata.' $false $true
    }
    $p=Get-AudioValue $State 'Package'
    if ($null -eq $p -or $t.Inf -notmatch '^oem[0-9]+\.inf$' -or
        $p.PublishedName -ine $t.Inf -or $p.InfHash -notmatch '^[0-9A-Fa-f]{64}$') {
        return New-AudioDecision 'BOUND_DRIVER_UNRESOLVED' 'Exista o legatura de driver, dar identitatea pachetului nu este suficient confirmata.'
    }
    if ($t.Service -ieq 'IntcAudioBus' -and $p.OriginalName -ieq 'intcaudiobus.inf' -and
        $p.Version -eq '9.22.0.4883' -and $t.DriverVersion -eq $p.Version -and $p.Provider -ieq 'Intel(R) Corporation') {
        $boot=Get-AudioValue $p 'BootCritical'
        if ($boot -isnot [bool]) {
            return New-AudioDecision 'INTEL_BOOT_STATUS_UNKNOWN' 'Intel SST 4883 identificat. Salvez pachetul; marcajul Boot Critical ramane necunoscut.' $true
        }
        if ($boot) {
            return New-AudioDecision 'INTEL_BOOT_CRITICAL' 'Intel SST 4883 este marcat Boot Critical. Salvez si verific pachetul, serviciul si configuratia recuperarii; dezinstalarea nu este automatizata in acest pachet.' $true
        }
        return New-AudioDecision 'INTEL_TRANSITION_REQUIRED' 'Intel SST 4883 identificat. Salvez pachetul si pregatesc datele pentru tranzitie.' $true
    }
    return New-AudioDecision 'OTHER_DRIVER_BOUND' 'Alt driver este legat de controler. Salvez pachetul identificat pentru analiza compatibilitatii.' $true
}
function Get-AudioStateStamp($State) {
    $t=$State.Target; $p=$State.Package
    [ordered]@{
        InstanceId=$t.InstanceId; HardwareIds=@($t.HardwareIds | Sort-Object)
        Problem=$t.Problem; Service=$t.Service; Inf=$t.Inf; Version=$t.DriverVersion; Provider=$t.DriverProvider
        PackageName=(Get-AudioValue $p 'PublishedName'); PackageHash=(Get-AudioValue $p 'InfHash')
        PackagePath=(Get-AudioValue $p 'OriginalPath'); PackageVersion=(Get-AudioValue $p 'Version')
        PackageProvider=(Get-AudioValue $p 'Provider'); BootCritical=(Get-AudioValue $p 'BootCritical')
        ExperimentalPresent=$State.ExperimentalPresent; CodeIntegrityOptions=$State.CodeIntegrityOptions
    } | ConvertTo-Json -Depth 5 -Compress
}
function Invoke-AudioAuto([hashtable]$Ops) {
    $s=[ordered]@{ CoordinatorVersion='1.0'; Phase='ANALYZE'; Status='IN_PROGRESS'
        State=$null; Decision=$null; Backup=$null; Context=$null; Probe=$null
        Error=$null; Warnings=@(); AudioPlayback='NOT_IMPLEMENTED' }
    try {
        & $Ops.Save $s
        $s.State=& $Ops.ReadState
        $s.Decision=Get-AudioDecision $s.State
        & $Ops.Save $s
        if ($s.Decision.Code -eq 'TARGET_MISMATCH') { $s.Status='TARGET_MISMATCH' }
        else {
            if ($s.Decision.Export) {
                $s.Phase='EXPORT'; & $Ops.Save $s
                $s.Backup=& $Ops.Export $s.State
                if ((Get-AudioValue $s.Backup 'Verified') -ne $true) { throw 'EXPORT_NOT_VERIFIED' }
                & $Ops.Save $s
            }
            $s.Phase='CONTEXT'; & $Ops.Save $s
            $s.Context=& $Ops.Capture $s.State
            $s.Phase='RECHECK'; & $Ops.Save $s
            $now=& $Ops.ReadState
            if ((Get-AudioStateStamp $s.State) -cne (Get-AudioStateStamp $now)) {
                $s.Status='STATE_CHANGED'; $s.Error='Starea controlerului s-a schimbat. Proba nu a fost pornita.'
            } elseif ($s.Decision.Probe) {
                $s.Phase='PROBE'; & $Ops.Save $s
                $s.Probe=& $Ops.Probe
                $r=$s.Probe.Transaction
                if ($r.Phase -eq 'SNAPSHOT_COMPLETE_CLEAN' -and $r.Clean -eq $true -and
                    $null -ne $r.Snapshot -and $s.Probe.ExitCode -eq 0) { $s.Status='SNAPSHOT_COMPLETE_CLEAN' }
                elseif ($r.Clean -ne $true) { $s.Status='RECOVERY_REQUIRED'; $s.Error=$r.Error }
                else { $s.Status='PROBE_STOPPED'; $s.Error=$r.Error }
            } else {
                $s.Status=$s.Decision.Code
                if ($s.Decision.Export) { $s.Status='PREPARED_'+$s.Decision.Code }
            }
        }
    } catch {
        $s.Status=if ($s.Phase -eq 'PROBE') { 'PROBE_RESULT_UNCONFIRMED' } else { 'FAILED_'+$s.Phase }
        $s.Error=$_.Exception.Message
    } finally {
        $s.Phase='FINISHED'
        try { & $Ops.Save $s } catch { $s.Status='REPORT_WRITE_FAILED'; $s.Warnings+=$_.Exception.Message }
    }
    return [pscustomobject]$s
}
