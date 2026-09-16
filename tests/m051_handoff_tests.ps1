$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
. (Join-Path $PSScriptRoot '..\m051\runtime\Transaction.ps1')
. (Join-Path $PSScriptRoot '..\m051\runtime\AudioDecision.ps1')
. (Join-Path $PSScriptRoot '..\m051\runtime\AudioWindows.ps1')
. (Join-Path $PSScriptRoot '..\m051\runtime\Transition.ps1')
$script:handoffChecks=0
function Check-Handoff([bool]$Value,[string]$Message) {
    $script:handoffChecks++
    if (-not $Value) { throw "Handoff assertion: $Message" }
}
function Reject-Handoff([scriptblock]$Action,[string]$Message) {
    $failed=$false
    try { & $Action | Out-Null } catch { $failed=$true }
    Check-Handoff $failed $Message
}
function New-HandoffFixture {
    [pscustomobject]@{
        Target=[pscustomobject]@{ InstanceId='PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06\fixture'
            HardwareIds=@('PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06'); Problem=0
            Service='IntcAudioBus'; Inf='oem147.inf'; DriverVersion='9.22.0.4883'; DriverProvider='Intel(R) Corporation' }
        Package=[pscustomobject]@{ PublishedName='oem147.inf'; OriginalName='intcaudiobus.inf'
            OriginalPath='C:\Windows\System32\DriverStore\FileRepository\intel\intcaudiobus.inf'
            Version='9.22.0.4883'; Provider='Intel(R) Corporation'; BootCritical=$true
            InfHash='4B0AC72AF0FE9425BFF3BEEC53F0999A0690C94D50DD7B87057A4B58BD4EEA15' }
        ExperimentalPresent=$false; CodeIntegrityOptions=[uint32]0; IntelPackageRetained=$true
    }
}
$service=[pscustomobject]@{ Start=3; Type=1; State='Running'; Owners=@('oem147.inf')
    LoadedFileHash='C01881C17F120573CBE1423BE462C2871F5D446C507003D752C91271D96371F9' }
$files=@(Get-HandoffIntelFiles)
$fixture=New-HandoffFixture
Assert-HandoffBaseline $fixture $service $files
Check-Handoff ((Get-AudioDecision $fixture).Transition) 'reviewed package accepts transition despite DISM BootCritical'
$fixture.CodeIntegrityOptions=[uint32]5
Check-Handoff ((Get-AudioDecision $fixture).Code -eq 'SIGNING_SESSION_REQUIRED') 'actual uploaded CI 5 blocks before binding'
Reject-Handoff { Assert-HandoffBaseline $fixture $service $files } 'runtime repeats signing check'
foreach ($change in @(@{Start=0},@{Start=4},@{Type=2},@{State='Stopped'},@{Owners=@('oem147.inf','oem149.inf')},
    @{LoadedFileHash=('0'*64)})) {
    $s=$service.PSObject.Copy()
    foreach ($key in $change.Keys) { $s.$key=$change[$key] }
    Reject-Handoff { Assert-HandoffBaseline (New-HandoffFixture) $s $files } 'changed live Intel service rejected'
}
$s=New-HandoffFixture; $s.Package.InfHash=('A'*64)
Reject-Handoff { Assert-HandoffBaseline $s $service $files } 'different package bytes rejected'
Check-Handoff (-not (Get-AudioDecision $s).Transition) 'unreviewed Intel is export only'
Reject-Handoff { Assert-HandoffBaseline (New-HandoffFixture) $service @($files[0],$files[1]) } 'backup missing SYS'
$s=New-HandoffFixture; $s.Target.Inf='oem32.inf'
Reject-Handoff { Assert-HandoffBaseline $s $service $files } 'dynamic published name consistency'
$s=New-HandoffFixture; $s.Target.Problem=28; $s.Target.Service=''; $s.Target.Inf=''; $s.Package=$null
Check-Handoff ((Get-AudioDecision $s).Code -eq 'UNBOUND_INTEL_RETAINED') 'legacy cleanup cannot silently rebind retained Intel'

# Use the real cleanup function with injected native/Windows boundaries. This
# exercises ordering and refusal of foreign bindings without touching hardware.
function Find-M051CurrentOwned {
    if ($script:hc.Package) {
        [pscustomobject]@{ OriginalName='phaser360_m051_mmio_ro.inf'; PublishedName='oem208.inf'
            OriginalPath='C:\Windows\System32\DriverStore\FileRepository\probe\phaser360_m051_mmio_ro.inf' }
    }
}
function Get-M051Target { return $script:hc.Target }
function Disable-M051OwnedService($Package) { $script:hc.Events.Add('disable-owned') }
function Write-M051Journal($State) {
    $script:hc.Events.Add('journal')
    if ($script:hc.Fail -eq 'journal') { throw 'disk failure' }
}
function Set-HandoffNullBinding([string]$InstanceId) {
    Check-Handoff ($InstanceId -eq $script:M051.Target.InstanceId) 'null binding scopes exact instance'
    $script:hc.Events.Add('null')
    if ($script:hc.Fail -eq 'null-fails') { throw 'veto' }
    if ($script:hc.Fail -eq 'null-reboot') { return $true }
    $script:hc.Target=$script:M051.Target.PSObject.Copy()
    $script:hc.Target.Problem=28; $script:hc.Target.Service=''; $script:hc.Target.Inf=''
    return $false
}
function Invoke-M051Pnp([string[]]$Arguments) {
    $script:hc.Events.Add('delete')
    Check-Handoff (($Arguments -join '|') -eq '/delete-driver|oem208.inf') 'only owned unused package, no uninstall or force'
    if ($script:hc.Fail -eq 'delete-fails') { throw 'package in use' }
    $script:hc.Package=$false
}
function Get-M051Store { Find-M051CurrentOwned }
function Test-HandoffIntelPreserved {
    $script:hc.Events.Add('check-intel')
    if ($script:hc.Fail -eq 'intel-changed') { throw 'Intel changed externally' }
}
function Test-HandoffScenario([string]$Name) {
    $initial=(New-HandoffFixture).Target
    $script:M051=@{ Target=$initial; Handoff=@{ NullAttempted=$false; NullRebootRequired=$false
        FinalState='NOT_CHECKED'; FinalTarget=$null; IntelPackagePreserved=$false } }
    $script:hc=@{ Fail=$Name; Events=[Collections.Generic.List[string]]::new(); Target=$initial.PSObject.Copy(); Package=$false }
    $ops=@{
        Save={ param($s) $script:hc.Events.Add('save:'+$s.Phase) }
        Preflight={ param($s) if ($script:hc.Fail -eq 'preflight') { throw 'blocked' } }
        Trust={ param($s) $script:hc.Events.Add('trust') }
        Stage={ param($s)
            $script:hc.Package=$true
            if ($script:hc.Fail -eq 'stage-partial') { throw 'partial stage' }
        }
        Discover={ param($s) Find-M051CurrentOwned }
        BeforeBind={ param($s)
            if ($script:hc.Fail -eq 'race') { $script:hc.Target.Inf='oem777.inf'; throw 'external driver update' }
        }
        Bind={ param($s)
            if ($script:hc.Fail -eq 'bind-refused') { throw 'API refused, Intel unchanged' }
            $script:hc.Target.Service='phaser360_m051_mmio_ro'; $script:hc.Target.Inf='oem208.inf'
            if ($script:hc.Fail -in @('bind-partial','bind-reboot')) { throw 'partial binding or pending reboot' }
        }
        Snapshot={ param($s)
            if ($script:hc.Fail -eq 'snapshot') { throw 'no registers' }
            @{ Valid=$true }
        }
        Remove={ param($s) Remove-HandoffOwnedPackage $s }
        CheckClean={ param($s) Test-HandoffClean $s }
        Untrust={ param($s) $script:hc.Events.Add('untrust') }
    }
    $r=Invoke-M051Transaction $ops
    if ($Name -eq 'preflight') {
        Check-Handoff (-not $r.TrustAttempted -and -not $r.BindAttempted) 'preflight no mutation'
    } elseif ($Name -in @('race','null-fails','null-reboot','delete-fails','journal','intel-changed')) {
        Check-Handoff (-not $r.Clean -and $r.Phase -eq 'RECOVERY_REQUIRED') 'uncertainty is not a clean success'
        if ($Name -in @('race','null-fails','null-reboot','journal')) {
            Check-Handoff (-not $script:hc.Events.Contains('delete')) 'no deletion until verified detach'
        }
        if ($Name -eq 'race') { Check-Handoff (-not $script:hc.Events.Contains('null')) 'foreign binding left alone' }
        if ($Name -eq 'journal') { Check-Handoff (-not $script:hc.Events.Contains('null')) 'journal failure blocks next mutation' }
    } elseif ($Name -in @('stage-partial','bind-refused')) {
        Check-Handoff ($r.Clean -and $script:M051.Handoff.FinalState -eq 'BASELINE_PRESERVED') 'failed before switch leaves Intel alone'
        Check-Handoff (-not $script:hc.Events.Contains('null')) 'unchanged Intel never receives null binding'
    } else {
        Check-Handoff ($r.Clean -and $script:M051.Handoff.FinalState -eq 'UNBOUND') 'final target deliberately unbound'
        Check-Handoff ($script:hc.Events.IndexOf('null') -lt $script:hc.Events.IndexOf('delete')) 'detach before own package deletion'
        Check-Handoff ($script:hc.Events.IndexOf('disable-owned') -lt $script:hc.Events.IndexOf('null')) 'disable own service before detach'
        if ($Name -eq '') { Check-Handoff ($r.Phase -eq 'SNAPSHOT_COMPLETE_CLEAN' -and $null -ne $r.Snapshot) 'success includes snapshot' }
        else { Check-Handoff ($r.Phase -eq 'STOPPED_CLEAN') 'read or bind error retained despite successful cleanup' }
    }
}
foreach ($name in @('','preflight','stage-partial','bind-refused','bind-partial','bind-reboot','race','snapshot',
    'null-fails','null-reboot','delete-fails','journal','intel-changed')) { Test-HandoffScenario $name }

# Managed x64 structures must agree with the SDK static_assert build as well.
Add-Type -Path (Join-Path $PSScriptRoot '..\m051\runtime\DeviceBinding.cs')
[PhaserM051.DeviceBinding]::CheckAbi()
Reject-Handoff { [PhaserM051.DeviceBinding]::InstallNull('PCI\OTHER') } 'native boundary refuses other hardware before opening SetupAPI'
Reject-Handoff { [PhaserM051.DeviceBinding]::BindProbe('PCI\OTHER','C:\foreign.inf') } 'native boundary refuses non-owned INF'
Write-Host "HANDOFF_TESTS=PASS; checks=$script:handoffChecks; scenarios=13; native_abi=x64"
