$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
foreach ($name in @('Transaction.ps1','Windows.ps1','AudioDecision.ps1','AudioWindows.ps1','Transition.ps1','Repair.ps1')) {
    . (Join-Path $PSScriptRoot "..\m051\runtime\$name")
}
$script:repairChecks=0
function Check-Repair([bool]$Value,[string]$Message) {
    $script:repairChecks++
    if (-not $Value) { throw "Repair assertion: $Message" }
}
function Reject-Repair([scriptblock]$Action,[string]$Message) {
    $failed=$false
    try { & $Action | Out-Null } catch { $failed=$true }
    Check-Repair $failed $Message
}
function New-RepairFixture {
    $id='PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06\fixture'
    $hash='B9314489AB096311E5A01DC652A6FDFCDF5E7A8A5B8E7938C96110CF27A7D51D'
    [pscustomobject]@{
        Transaction=[pscustomobject]@{
            Version='0.5.1'; Kind='INTEL_HANDOFF_1'; StageAttempted=$true
            OwnedPackage=[pscustomobject]@{ PublishedName='oem37.inf'; OriginalName='phaser360_m051_mmio_ro.inf'; Hash=$hash }
            Snapshot=[pscustomobject]@{ Version='0.5.1'; InstanceId=$id; HdaGcap='0x00006701'; DspAdspcs='0x001D003C' }
        }
        Context=[pscustomobject]@{
            Mode='INTEL_HANDOFF_1'; InfHash=$hash; CertThumbprint='5AC2064FA901334A31316ACF7742AC102F4EAE6C'
            RunDir='C:\PHASER360_M051_RECOVERY\20260916_211227_13c9e569'; BeforePackages=@('oem32.inf')
            Target=[pscustomobject]@{ InstanceId=$id; HardwareIds=@('PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06')
                Problem=0; Service='IntcAudioBus'; Inf='oem32.inf'; DriverVersion='9.22.0.4883'; DriverProvider='Intel(R) Corporation' }
            Handoff=[pscustomobject]@{ NullAttempted=$true; NullRebootRequired=$false; BindRebootRequired=$false
                FinalState='NOT_CHECKED'; FinalTarget=$null; IntelPackagePreserved=$null }
        }
    }
}
$f=New-RepairFixture
Assert-ReviewedRepairRecord $f $f.Context.RunDir
foreach ($change in @(
    { param($f) $f.Context.InfHash=('0'*64) },
    { param($f) $f.Context.CertThumbprint=('0'*40) },
    { param($f) $f.Context.BeforePackages+= 'oem37.inf' },
    { param($f) $f.Context.Handoff.NullAttempted=$false },
    { param($f) $f.Context.Handoff.NullRebootRequired=$true },
    { param($f) $f.Context.Handoff.BindRebootRequired=$true },
    { param($f) $f.Transaction.StageAttempted=$false },
    { param($f) $f.Transaction.Kind='OTHER' },
    { param($f) $f.Transaction.Snapshot=$null },
    { param($f) $f.Transaction.Snapshot.InstanceId='OTHER' },
    { param($f) $f.Transaction.OwnedPackage.Hash=('0'*64) },
    { param($f) $f.Transaction.OwnedPackage.OriginalName='intcaudiobus.inf' },
    { param($f) $f.Transaction.OwnedPackage.PublishedName='..\oem37.inf' },
    { param($f) $f.Context.Target.HardwareIds=@('PCI\OTHER') }
)) {
    $f=New-RepairFixture; & $change $f
    Reject-Repair { Assert-ReviewedRepairRecord $f $f.Context.RunDir } 'unreviewed journal rejected'
}
$f=New-RepairFixture
Reject-Repair { Assert-ReviewedRepairRecord $f 'C:\PHASER360_M051_RECOVERY\another_run' } 'directory mismatch'
$f.Context.RunDir='C:\PHASER360_M051_RECOVERY\another_run'
Reject-Repair { Assert-ReviewedRepairRecord $f $f.Context.RunDir } 'unknown run cannot be adopted'

# Exercise the production ownership, deletion and final-state functions with
# Windows boundaries replaced. No driver, service or certificate is installed.
function Get-M051Store {
    if ($script:rc.PackagePresent) {
        [pscustomobject]@{ OriginalName='phaser360_m051_mmio_ro.inf'
            PublishedName=$script:rc.PackageName; Hash=$script:rc.Hash
            OriginalPath='C:\Windows\System32\DriverStore\FileRepository\fixture\phaser360_m051_mmio_ro.inf' }
    }
}
function Get-M051Target {
    $script:rc.TargetReads++
    if (($script:rc.Name -eq 'race' -and $script:rc.TargetReads -eq 2) -or
        ($script:rc.Name -eq 'final-race' -and $script:rc.TargetReads -eq 4)) {
        $script:rc.Target.Service='IntcAudioBus'; $script:rc.Target.Inf='oem32.inf'
    }
    if ($script:rc.Name -eq 'package-race' -and $script:rc.TargetReads -eq 2) { $script:rc.PackageName='oem38.inf' }
    return $script:rc.Target
}
function Write-M051Journal($State) {
    $script:rc.Events.Add('journal')
    if ($script:rc.Name -eq 'journal-fails') { throw 'fixture journal failure' }
}
function Disable-M051OwnedService($Package) { $script:rc.Events.Add('disable-owned') }
function Invoke-M051Pnp([string[]]$Arguments) {
    Check-Repair (($Arguments -join '|') -eq '/delete-driver|oem37.inf') 'delete only reviewed unused package, without uninstall/force/reboot'
    $script:rc.Events.Add('delete')
    if ($script:rc.Name -eq 'delete-fails') { throw 'fixture package in use' }
    $script:rc.PackagePresent=$false
}
function Test-HandoffIntelPreserved {
    $script:rc.Events.Add('verify-intel')
    if ($script:rc.Name -eq 'intel-changed') { throw 'fixture Intel changed' }
}
function Remove-M051Trust {
    $script:rc.Events.Add('untrust')
    if ($script:rc.Name -eq 'cert-fails') { throw 'fixture certificate failure' }
}
function Set-HandoffNullBinding { throw 'repair must never change binding' }
function Set-HandoffProbeBinding { throw 'repair must never install probe' }
function Add-M051Trust { throw 'repair must never import certificates' }
function Read-M051Snapshot { throw 'repair must never read hardware again' }
function Test-RepairScenario([string]$Name) {
    $record=New-RepairFixture
    $script:M051=$record.Context
    $script:rc=@{ Name=$Name; PackagePresent=($Name -ne 'already-clean'); PackageName='oem37.inf'
        Hash=$record.Context.InfHash; TargetReads=0; Target=$record.Context.Target.PSObject.Copy()
        Events=[Collections.Generic.List[string]]::new() }
    $script:rc.Target.Service=''; $script:rc.Target.Inf=''
    switch ($Name) {
        'code28' { $script:rc.Target.Problem=28 }
        'intel-bound' { $script:rc.Target.Service='IntcAudioBus'; $script:rc.Target.Inf='oem32.inf' }
        'foreign-bound' { $script:rc.Target.Service='Foreign'; $script:rc.Target.Inf='oem70.inf' }
        'service-only' { $script:rc.Target.Service='IntcAudioBus' }
        'inf-only' { $script:rc.Target.Inf='oem37.inf' }
        'problem32' { $script:rc.Target.Problem=32 }
        'wrong-instance' { $script:rc.Target.InstanceId='OTHER' }
        'wrong-hardware' { $script:rc.Target.HardwareIds=@('PCI\OTHER') }
        'wrong-hash' { $script:rc.Hash=('0'*64) }
    }
    $ops=@{
        Save={ param($r)
            $script:rc.Events.Add('save')
            if ($script:rc.Name -eq 'save-fails') { throw 'fixture report write failure' }
        }
        Validate={ Assert-ReviewedRepairRecord $record $record.Context.RunDir }
        Target={ Get-M051Target }
        Discover={ Find-M051CurrentOwned }
        Remove={ param($s) Remove-ReviewedDetachedPackage $s }
        Check={ param($s) Test-HandoffClean $s }
        Untrust={ Remove-M051Trust }
    }
    $snapshotBefore=$record.Transaction.Snapshot | ConvertTo-Json -Compress
    $r=Invoke-DetachedRepair $record $ops
    Check-Repair (-not $r.ProbeRepeated -and $r.AudioPlayback -eq 'NOT_IMPLEMENTED') 'no repeated probe or audio claim'
    Check-Repair (($record.Transaction.Snapshot | ConvertTo-Json -Compress) -ceq $snapshotBefore) 'original snapshot retained'
    if ($Name -in @('code0','code28','already-clean')) {
        Check-Repair ($r.Status -eq 'RECOVERY_CLEANUP_COMPLETE' -and $r.IntelPackagePreserved) 'verified repair success'
        Check-Repair (-not $script:rc.PackagePresent -and $script:rc.Events.Contains('verify-intel') -and $script:rc.Events.Contains('untrust')) 'package and certificate cleanup with Intel verification'
        if ($Name -eq 'already-clean') {
            Check-Repair (-not $r.PackageDeleted -and -not $script:rc.Events.Contains('delete')) 'repeat repair idempotent'
        } else {
            Check-Repair ($r.PackageDeleted -and $script:rc.Events.IndexOf('journal') -lt $script:rc.Events.IndexOf('delete')) 'journal precedes deletion'
        }
    } else {
        $expected=if ($Name -eq 'save-fails') { 'REPORT_WRITE_FAILED' } else { 'RECOVERY_REQUIRED' }
        Check-Repair ($r.Status -eq $expected -and -not [string]::IsNullOrWhiteSpace($r.Error)) 'failure remains explicit'
        Check-Repair ($null -eq $r.IntelPackagePreserved) 'incomplete verification never reported as verified'
        if ($Name -notin @('delete-fails','intel-changed','cert-fails','final-race')) {
            Check-Repair (-not $script:rc.Events.Contains('delete') -and -not $script:rc.Events.Contains('disable-owned') -and
                -not $script:rc.Events.Contains('untrust')) 'refusal before machine changes'
        }
    }
}
$scenarios=@('code0','code28','already-clean','intel-bound','foreign-bound','service-only','inf-only','problem32',
    'wrong-instance','wrong-hardware','wrong-hash','race','package-race','journal-fails','delete-fails',
    'intel-changed','cert-fails','final-race','save-fails')
foreach ($name in $scenarios) { Test-RepairScenario $name }
Write-Host "REPAIR_TESTS=PASS; checks=$script:repairChecks; scenarios=$($scenarios.Count); hardware_access=NONE"
