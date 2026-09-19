$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
. (Join-Path $PSScriptRoot '..\m051\runtime\AudioDecision.ps1')
. (Join-Path $PSScriptRoot '..\m051\runtime\AudioWindows.ps1')
$script:audioChecks=0
function Check-Audio([bool]$Condition,[string]$Message) {
    $script:audioChecks++
    if (-not $Condition) { throw "Audio assertion failed: $Message" }
}
function Reject-Audio([scriptblock]$Action,[string]$Message) {
    $failed=$false
    try { & $Action | Out-Null } catch { $failed=$true }
    Check-Audio $failed $Message
}
function New-AudioFixture([string]$Kind='intel') {
    $t=[pscustomobject]@{ HardwareIds=@('PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06')
        InstanceId='PCI\fixture'; Problem=0; Service='IntcAudioBus'; Inf='oem147.inf'
        DriverVersion='9.22.0.4883'; DriverProvider='Intel(R) Corporation' }
    $p=[pscustomobject]@{ PublishedName='oem147.inf'; OriginalName='intcaudiobus.inf'
        OriginalPath='C:\Windows\System32\DriverStore\FileRepository\fixture\intcaudiobus.inf'
        Version='9.22.0.4883'; Provider='Intel(R) Corporation'; InfHash=('A'*64); BootCritical=$true }
    if ($Kind -eq 'unbound') { $t.Problem=28; $t.Service=''; $t.Inf=''; $p=$null }
    [pscustomobject]@{ Target=$t; Package=$p; ExperimentalPresent=$false; CodeIntegrityOptions=[uint32]2 }
}
$intel=New-AudioFixture
Check-Audio ((Get-AudioDecision $intel).Code -eq 'INTEL_BOOT_CRITICAL') 'observed Intel baseline'
Check-Audio ((Get-AudioDecision $intel).Export -and -not (Get-AudioDecision $intel).Probe) 'preserve bound package without probing'
foreach ($flag in @($null,'False','True',0,1)) {
    $state=New-AudioFixture; $state.Package.BootCritical=$flag
    Check-Audio ((Get-AudioDecision $state).Code -eq 'INTEL_BOOT_STATUS_UNKNOWN') 'untyped boot flag'
}
$state=New-AudioFixture; $state.Package.PSObject.Properties.Remove('BootCritical')
Check-Audio ((Get-AudioDecision $state).Code -eq 'INTEL_BOOT_STATUS_UNKNOWN') 'missing boot flag'
$state=New-AudioFixture; $state.Package.BootCritical=$false
Check-Audio ((Get-AudioDecision $state).Code -eq 'INTEL_TRANSITION_REQUIRED') 'noncritical still needs supported transition'
$state=New-AudioFixture; $state.Target.Inf='oem32.inf'
Check-Audio ((Get-AudioDecision $state).Code -eq 'BOUND_DRIVER_UNRESOLVED') 'no stale OEM mapping'
$state=New-AudioFixture; $state.Target.HardwareIds=@('PCI\VEN_8086&DEV_3198')
Check-Audio ((Get-AudioDecision $state).Code -eq 'TARGET_MISMATCH') 'exact identity'
$state=New-AudioFixture; $state.Package.Provider='Another provider'
Check-Audio ((Get-AudioDecision $state).Code -eq 'OTHER_DRIVER_BOUND') 'no Intel assumption'
$state=New-AudioFixture; $state.ExperimentalPresent=$true
Check-Audio ((Get-AudioDecision $state).Code -eq 'EXPERIMENTAL_RECOVERY_REQUIRED') 'previous experiment not adopted'
foreach ($ci in @([uint32]0,[uint32]2,[uint32]3)) {
    $state=New-AudioFixture 'unbound'; $state.CodeIntegrityOptions=$ci
    Check-Audio ((Get-AudioDecision $state).Probe) 'active session permits probe'
}
foreach ($ci in @($null,'2',2)) {
    $state=New-AudioFixture 'unbound'; $state.CodeIntegrityOptions=$ci
    Check-Audio ((Get-AudioDecision $state).Code -eq 'SIGNING_STATE_UNKNOWN') 'unknown signing is not permission'
}
$state=New-AudioFixture 'unbound'; $state.CodeIntegrityOptions=[uint32]1
Check-Audio ((Get-AudioDecision $state).Code -eq 'SIGNING_SESSION_REQUIRED') 'production enforcement'
function Test-AudioScenario([string]$Name) {
    $script:audioCase=@{ Name=$Name; Reads=0; Events=[Collections.Generic.List[string]]::new()
        State=(New-AudioFixture); Exported=$false; Probed=$false }
    if ($Name -in @('probe','probe-fails','probe-dirty','probe-throws','false-success','changed','save-fails')) {
        $script:audioCase.State=New-AudioFixture 'unbound'
    }
    if ($Name -eq 'target-mismatch') { $script:audioCase.State.Target.HardwareIds=@('OTHER') }
    $ops=@{
        Save={ param($s)
            $script:audioCase.Events.Add('save:'+$s.Phase)
            if ($script:audioCase.Name -eq 'save-fails') { throw 'disk full' }
        }
        ReadState={
            $script:audioCase.Reads++
            if ($script:audioCase.Name -eq 'read-fails') { throw 'query failed' }
            if ($script:audioCase.Name -eq 'changed' -and $script:audioCase.Reads -gt 1) { return New-AudioFixture }
            return $script:audioCase.State
        }
        Export={ param($s)
            $script:audioCase.Exported=$true; $script:audioCase.Events.Add('export')
            if ($script:audioCase.Name -eq 'export-fails') { throw 'export failed' }
            [pscustomobject]@{ Verified=($script:audioCase.Name -ne 'export-incomplete') }
        }
        Capture={ param($s)
            $script:audioCase.Events.Add('context')
            if ($script:audioCase.Name -eq 'context-fails') { throw 'context failed' }
            [pscustomobject]@{ RecoveryBootTested=$false; Warnings=@() }
        }
        Probe={
            $script:audioCase.Probed=$true; $script:audioCase.Events.Add('probe')
            if ($script:audioCase.Name -eq 'probe-throws') { throw 'missing child result' }
            $r=[pscustomobject]@{ Phase='SNAPSHOT_COMPLETE_CLEAN'; Clean=$true; Snapshot=@{Valid=$true}; Error=$null }
            if ($script:audioCase.Name -eq 'probe-fails') { $r.Phase='STOPPED_CLEAN'; $r.Error='test failure' }
            if ($script:audioCase.Name -eq 'probe-dirty') { $r.Clean=$false; $r.Phase='RECOVERY_REQUIRED' }
            if ($script:audioCase.Name -eq 'false-success') { $r.Snapshot=$null }
            [pscustomobject]@{ Transaction=$r; ExitCode=0 }
        }
    }
    $r=Invoke-AudioAuto $ops
    switch ($Name) {
        'intel' {
            Check-Audio ($r.Status -eq 'PREPARED_INTEL_BOOT_CRITICAL') 'Intel preservation complete'
            Check-Audio ($script:audioCase.Exported -and -not $script:audioCase.Probed) 'Intel branch cannot probe'
        }
        'probe' { Check-Audio ($r.Status -eq 'SNAPSHOT_COMPLETE_CLEAN' -and $script:audioCase.Probed) 'automatic probe' }
        'changed' { Check-Audio ($r.Status -eq 'STATE_CHANGED' -and -not $script:audioCase.Probed) 'race blocks probe' }
        'export-fails' { Check-Audio ($r.Status -eq 'FAILED_EXPORT' -and -not $script:audioCase.Probed) 'export failure' }
        'export-incomplete' { Check-Audio ($r.Status -eq 'FAILED_EXPORT') 'partial copy rejected' }
        'context-fails' { Check-Audio ($r.Status -eq 'FAILED_CONTEXT') 'context failure explicit' }
        'read-fails' { Check-Audio ($r.Status -eq 'FAILED_ANALYZE') 'query failure explicit' }
        'probe-fails' { Check-Audio ($r.Status -eq 'PROBE_STOPPED') 'probe errors preserved' }
        'probe-dirty' { Check-Audio ($r.Status -eq 'RECOVERY_REQUIRED') 'cleanup failure explicit' }
        'probe-throws' { Check-Audio ($r.Status -eq 'PROBE_RESULT_UNCONFIRMED') 'missing child leaves cleanup unconfirmed' }
        'false-success' { Check-Audio ($r.Status -eq 'PROBE_STOPPED') 'snapshot required' }
        'save-fails' { Check-Audio ($r.Status -eq 'REPORT_WRITE_FAILED' -and -not $script:audioCase.Probed) 'journal failure blocks operations' }
        'target-mismatch' { Check-Audio (-not $script:audioCase.Exported -and -not $script:audioCase.Probed) 'wrong hardware no actions' }
    }
    Check-Audio ($r.AudioPlayback -eq 'NOT_IMPLEMENTED') 'no false sound claim'
}
foreach ($name in @('intel','probe','changed','export-fails','export-incomplete','context-fails','read-fails',
    'probe-fails','probe-dirty','probe-throws','false-success','save-fails','target-mismatch')) { Test-AudioScenario $name }
$testDir=Join-Path ([IO.Path]::GetTempPath()) ('phaser_audio_test_'+[Guid]::NewGuid().ToString('N'))
try {
    $sourceRoot=Join-Path $testDir 'source with spaces'
    New-Item -ItemType Directory -Path $sourceRoot -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $sourceRoot 'intcaudiobus.inf') -Value 'fixture INF'
    Set-Content -LiteralPath (Join-Path $sourceRoot 'driver.sys') -Value 'fixture SYS'
    $files=@(Get-AudioInventory $sourceRoot)
    Check-Audio ($files.Count -eq 2) 'real inventory'
    Check-Audio ((Compare-AudioInventory $files $files).Verified) 'complete matching copy'
    Check-Audio (-not (Compare-AudioInventory $files @($files[0])).Verified) 'missing source files'
    $corrupt=$files[0].PSObject.Copy(); $corrupt.Hash=('0'*64)
    Reject-Audio { Compare-AudioInventory $files @($corrupt,$files[1]) } 'changed bytes'
    Reject-Audio { Compare-AudioInventory $files @() } 'empty export'
    Reject-Audio { Compare-AudioInventory $files @($files[0],$files[0]) } 'duplicate path'
    $extra=$files[0].PSObject.Copy(); $extra.Relative='other.sys'
    Reject-Audio { Compare-AudioInventory $files @($extra) } 'unexpected exported file'
    $junction=Join-Path $testDir 'junction'
    New-Item -ItemType Junction -Path $junction -Target $sourceRoot | Out-Null
    Reject-Audio { Get-AudioInventory $junction } 'reparse root'
    [IO.Directory]::Delete($junction)
} finally {
    if (Test-Path -LiteralPath $testDir) { Remove-Item -LiteralPath $testDir -Recurse -Force }
}
Write-Host "AUDIO_AUTO_TESTS=PASS; checks=$script:audioChecks; scenarios=13"
