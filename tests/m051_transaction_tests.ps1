$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
. (Join-Path $PSScriptRoot '..\m051\runtime\Transaction.ps1')
$script:checks = 0
function Check([bool]$Value, [string]$Message) {
    $script:checks++
    if (-not $Value) { throw "Check failed: $Message" }
}
function Reject([scriptblock]$Action, [string]$Message) {
    $threw = $false
    try { & $Action | Out-Null } catch { $threw = $true }
    Check $threw $Message
}
$clean = [pscustomobject]@{ HardwareIds = @('PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06')
    Service = ''; Inf = ''; Problem = 28 }
Assert-M051CleanTarget $clean
foreach ($change in @(@{Service='IntcAudioBus'},@{Inf='oem27.inf'},@{Problem=0},@{Problem=10},
    @{HardwareIds=@('PCI\VEN_8086&DEV_3198')})) {
    $copy = $clean.PSObject.Copy()
    foreach ($key in $change.Keys) { $copy.$key = $change[$key] }
    Reject { Assert-M051CleanTarget $copy } 'active or wrong target rejected'
}
$p = [pscustomobject]@{ OriginalName='phaser360_m051_mmio_ro.inf'; PublishedName='oem147.inf'; Hash='ABCD' }
Check ((Find-M051OwnedPackage @('oem0.inf') @($p) 'ABCD').PublishedName -eq 'oem147.inf') 'dynamic OEM'
Check ($null -eq (Find-M051OwnedPackage @() @() 'ABCD')) 'no package is not ownership'
Reject { Find-M051OwnedPackage @('oem147.inf') @($p) 'ABCD' } 'preexisting package'
Reject { Find-M051OwnedPackage @() @($p) 'FFFF' } 'different bytes'
Reject { Find-M051OwnedPackage @() @($p,$p) 'ABCD' } 'ambiguous ownership'
$bad = $p.PSObject.Copy(); $bad.PublishedName = '..\oem147.inf'
Reject { Find-M051OwnedPackage @() @($bad) 'ABCD' } 'invalid published name'

function Scenario([string]$Fail) {
    $script:case = @{ Fail=$Fail; Events=[Collections.Generic.List[string]]::new(); Package=$false
        Cert=$false; Bound=$false; Removed=$false; DiscoverCount=0 }
    $ops = @{
        Save = { param($s) $script:case.Events.Add('save:' + $s.Phase) }
        Preflight = { param($s)
            $script:case.Events.Add('preflight')
            if ($script:case.Fail -eq 'preflight') { throw 'baseline' }
        }
        Trust = { param($s)
            $script:case.Events.Add('trust'); $script:case.Cert=$true
            if ($script:case.Fail -eq 'trust') { throw 'partial certificate import' }
        }
        Stage = { param($s)
            $script:case.Events.Add('stage')
            if ($script:case.Fail -eq 'stage-before') { throw 'stage before mutation' }
            $script:case.Package=$true
            if ($script:case.Fail -eq 'stage-after') { throw 'stage after mutation' }
        }
        Discover = { param($s)
            $script:case.Events.Add('discover'); $script:case.DiscoverCount++
            if ($script:case.Fail -eq 'ownership' -and $script:case.DiscoverCount -gt 1) { throw 'ownership changed' }
            if ($script:case.Package) { [pscustomobject]@{ PublishedName='oem147.inf' } }
        }
        BeforeBind = { param($s)
            $script:case.Events.Add('beforebind')
            if ($script:case.Fail -eq 'race') { throw 'another driver bound' }
        }
        Bind = { param($s)
            $script:case.Events.Add('bind'); $script:case.Bound=$true
            if ($script:case.Fail -in @('bind','reboot')) { throw 'bind failed or reboot required' }
        }
        Snapshot = { param($s)
            $script:case.Events.Add('snapshot')
            if ($script:case.Fail -eq 'snapshot') { throw 'read failed' }
            @{ Version='0.5.1'; DspAdspcs='0x00000000' }
        }
        Remove = { param($s)
            $script:case.Events.Add('remove')
            if ($script:case.Fail -eq 'remove') { throw 'uninstall failed' }
            $script:case.Package=$false; $script:case.Bound=$false; $script:case.Removed=$true
        }
        CheckClean = { param($s)
            $script:case.Events.Add('checkclean')
            if ($script:case.Fail -eq 'checkclean' -or $script:case.Package -or $script:case.Bound) { throw 'not clean' }
        }
        Untrust = { param($s)
            $script:case.Events.Add('untrust')
            if ($script:case.Fail -eq 'untrust') { throw 'cert remove failed' }
            $script:case.Cert=$false
        }
    }
    $result = Invoke-M051Transaction $ops
    if ($Fail -eq 'preflight') {
        Check ($result.Phase -eq 'STOPPED_CLEAN') 'preflight has no false recovery alarm'
        Check (-not $result.TrustAttempted -and -not $result.StageAttempted) 'no mutation after baseline failure'
        Check (-not $script:case.Events.Contains('remove') -and -not $script:case.Events.Contains('untrust')) 'no unrelated rollback'
    } elseif ($Fail -in @('ownership','remove','checkclean','untrust')) {
        Check (-not $result.Clean -and $result.Phase -eq 'RECOVERY_REQUIRED') 'cleanup failure is explicit'
        if ($Fail -eq 'ownership') { Check (-not $script:case.Removed) 'never deletes changed ownership' }
    } else {
        Check $result.Clean 'ordinary transaction restored'
        Check (-not $script:case.Package -and -not $script:case.Cert -and -not $script:case.Bound) 'all owned state removed'
        if ($Fail -eq '') {
            Check ($result.Phase -eq 'SNAPSHOT_COMPLETE_CLEAN' -and $null -ne $result.Snapshot) 'success requires snapshot and cleanup'
            Check ($script:case.Events.IndexOf('remove') -gt $script:case.Events.IndexOf('snapshot')) 'snapshot precedes cleanup'
        } else { Check ($result.Phase -eq 'STOPPED_CLEAN' -and $result.Error.Length -gt 0) 'failure preserved' }
    }
    Check ($script:case.Events -notcontains 'restore-intel') 'no legacy restoration'
}
foreach ($name in @('','preflight','trust','stage-before','stage-after','race','bind','reboot',
    'snapshot','ownership','remove','checkclean','untrust')) { Scenario $name }
Write-Host "M051_TRANSACTION_TESTS=PASS; checks=$script:checks; scenarios=13"

# Syntax-check every shipped PowerShell script using the host parser, without running it.
foreach ($file in Get-ChildItem (Join-Path $PSScriptRoot '..\m051\runtime') -Filter '*.ps1') {
    $tokens=$null; $parseErrors=$null
    [Management.Automation.Language.Parser]::ParseFile($file.FullName,[ref]$tokens,[ref]$parseErrors) | Out-Null
    if ($parseErrors.Count) { throw ($parseErrors | Out-String) }
}
Write-Host 'M051_RUNTIME_PARSE=PASS'
