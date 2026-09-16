Set-StrictMode -Version 2
# The hashes are from the user's 2026-09-16 export, not a generic Intel allowlist.
function Get-HandoffIntelFiles {
    @(
        [pscustomobject]@{ Relative='IntcAudioBus.cat'; Length=15451; Hash='D36C560617CA66FED4CC6A53BDCD95EFC2C9223853F31A89386EC8EAFB594AAF' }
        [pscustomobject]@{ Relative='IntcAudioBus.inf'; Length=48802; Hash='4B0AC72AF0FE9425BFF3BEEC53F0999A0690C94D50DD7B87057A4B58BD4EEA15' }
        [pscustomobject]@{ Relative='IntcAudioBus.sys'; Length=248520; Hash='C01881C17F120573CBE1423BE462C2871F5D446C507003D752C91271D96371F9' }
    )
}
function Assert-HandoffSigning($Options) {
    if ($Options -isnot [uint32]) { throw 'SIGNING_STATE_UNKNOWN' }
    if (($Options -band 1) -ne 0 -and ($Options -band 2) -eq 0) { throw 'SIGNING_SESSION_REQUIRED' }
}
function Assert-HandoffBaseline($State,$Service,$Files) {
    $t=$State.Target; $p=$State.Package
    if (@($t.HardwareIds) -notcontains 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06' -or
        $t.Problem -ne 0 -or $t.Service -ine 'IntcAudioBus' -or $t.Inf -notmatch '^oem[0-9]+\.inf$' -or
        $t.DriverVersion -ne '9.22.0.4883' -or $t.DriverProvider -ine 'Intel(R) Corporation' -or
        $null -eq $p -or $p.PublishedName -ine $t.Inf -or $p.OriginalName -ine 'intcaudiobus.inf' -or
        $p.Version -ne $t.DriverVersion -or $p.Provider -ine $t.DriverProvider -or
        $p.InfHash -ine '4B0AC72AF0FE9425BFF3BEEC53F0999A0690C94D50DD7B87057A4B58BD4EEA15') {
        throw 'HANDOFF_BASELINE_NOT_REVIEWED'
    }
    if ($Service.Start -ne 3 -or $Service.Type -ne 1 -or $Service.State -ne 'Running' -or
        $Service.Owners.Count -ne 1 -or $Service.Owners[0] -ine $t.Inf -or
        $Service.LoadedFileHash -ine 'C01881C17F120573CBE1423BE462C2871F5D446C507003D752C91271D96371F9') {
        throw 'HANDOFF_INTEL_SERVICE_CHANGED'
    }
    if (-not (Compare-AudioInventory @(Get-HandoffIntelFiles) $Files).Verified) { throw 'HANDOFF_INTEL_FILES_CHANGED' }
    Assert-HandoffSigning $State.CodeIntegrityOptions
}
function Get-HandoffTargetStamp($Target) {
    [ordered]@{ InstanceId=$Target.InstanceId; HardwareIds=@($Target.HardwareIds | Sort-Object)
        Problem=$Target.Problem; Service=$Target.Service; Inf=$Target.Inf
        Version=$Target.DriverVersion; Provider=$Target.DriverProvider } | ConvertTo-Json -Compress
}
function Get-HandoffCleanupAction($Target,$InitialTarget,$Package) {
    if ($Target.InstanceId -ine $InitialTarget.InstanceId) { throw 'HANDOFF_TARGET_INSTANCE_CHANGED' }
    if ($null -ne $Package -and $Target.Service -ieq 'phaser360_m051_mmio_ro' -and
        $Target.Inf -ieq $Package.PublishedName) { return 'DETACH_OWNED' }
    if ((Get-HandoffTargetStamp $Target) -ceq (Get-HandoffTargetStamp $InitialTarget)) { return 'BASELINE_PRESERVED' }
    try { Assert-M051CleanTarget $Target; return 'UNBOUND' } catch { }
    throw 'HANDOFF_UNEXPECTED_BINDING: cleanup will not replace this driver'
}
function Read-HandoffService {
    $key='HKLM:\SYSTEM\CurrentControlSet\Services\IntcAudioBus'
    $r=Get-ItemProperty -LiteralPath $key -ErrorAction Stop
    $drivers=@(Get-CimInstance Win32_SystemDriver -Filter "Name='IntcAudioBus'" -ErrorAction Stop)
    if ($drivers.Count -ne 1) { throw 'HANDOFF_INTEL_SERVICE_NOT_UNIQUE' }
    $expected=Join-Path $env:SystemRoot 'System32\drivers\IntcAudioBus.sys'
    $actual=[Environment]::ExpandEnvironmentVariables([string]$r.ImagePath).Trim('"')
    if ($actual.StartsWith('\SystemRoot\',[StringComparison]::OrdinalIgnoreCase)) {
        $actual=Join-Path $env:SystemRoot $actual.Substring(12)
    }
    if ($actual -ine $expected -or $drivers[0].PathName -ine $expected) { throw 'HANDOFF_INTEL_SERVICE_PATH_CHANGED' }
    [pscustomobject]@{ Start=[int]$r.Start; Type=[int]$r.Type; State=[string]$drivers[0].State
        Owners=@($r.Owners); ImagePath=[string]$r.ImagePath
        LoadedFileHash=(Get-FileHash -LiteralPath $expected -Algorithm SHA256).Hash }
}
function Set-HandoffNullBinding([string]$InstanceId) {
    [PhaserM051.DeviceBinding]::InstallNull($InstanceId)
}
function Set-HandoffProbeBinding([string]$InstanceId,[string]$Inf) {
    [PhaserM051.DeviceBinding]::BindProbe($InstanceId,$Inf)
}
function Test-HandoffIntelPreserved {
    $p=$script:M051.Handoff.InitialState.Package
    $matches=@(Get-WindowsDriver -Online -ErrorAction Stop | Where-Object { $_.Driver -ieq $p.PublishedName })
    if ($matches.Count -ne 1 -or $matches[0].OriginalFileName -ine $p.OriginalPath) {
        throw 'HANDOFF_PRESERVED_INTEL_PACKAGE_CHANGED'
    }
    $files=@(Get-AudioInventory (Split-Path -Parent $p.OriginalPath))
    if (-not (Compare-AudioInventory @(Get-HandoffIntelFiles) $files).Verified) { throw 'HANDOFF_PRESERVED_INTEL_FILES_CHANGED' }
    # PnP may stop the old service; its start policy, owners and binary must not change.
    $service=Read-HandoffService
    $before=$script:M051.Handoff.InitialService
    foreach ($key in @('Start','Type','ImagePath','LoadedFileHash')) {
        if ($service.$key -ine $before.$key) { throw "HANDOFF_INTEL_SERVICE_POLICY_CHANGED: $key" }
    }
    if (($service.Owners -join '|') -ine ($before.Owners -join '|')) { throw 'HANDOFF_INTEL_OWNERS_CHANGED' }
}
function Remove-HandoffOwnedPackage($State) {
    $p=Find-M051CurrentOwned
    if ($null -eq $p -or $p.PublishedName -ine $State.OwnedPackage.PublishedName) { throw 'PACKAGE_OWNERSHIP_CHANGED' }
    $target=Get-M051Target
    $action=Get-HandoffCleanupAction $target $script:M051.Target $p
    Disable-M051OwnedService $p
    if ($action -eq 'DETACH_OWNED') {
        $script:M051.Handoff.NullAttempted=$true
        Write-M051Journal $State
        $reboot=Set-HandoffNullBinding $target.InstanceId
        $script:M051.Handoff.NullRebootRequired=$reboot
        Write-M051Journal $State
        if ($reboot) { throw 'HANDOFF_NULL_REBOOT_REQUIRED: owned service disabled; no automatic reboot' }
        $now=Get-M051Target
        if ($now.InstanceId -ine $target.InstanceId) { throw 'TARGET_INSTANCE_CHANGED' }
        Assert-M051CleanTarget $now
    }
    # No /uninstall: the probe has already been detached. No /force, no rescan.
    # This also removes a staged-but-never-bound package after a pre-bind failure.
    Invoke-M051Pnp @('/delete-driver',$p.PublishedName)
}
function Test-HandoffClean($State) {
    if (@(Get-M051Store | Where-Object { $_.OriginalName -ieq 'phaser360_m051_mmio_ro.inf' }).Count) {
        throw 'OWNED_PACKAGE_STILL_PRESENT'
    }
    $target=Get-M051Target
    $action=Get-HandoffCleanupAction $target $script:M051.Target $null
    if ($action -eq 'BASELINE_PRESERVED' -and $null -ne $State.Snapshot) { throw 'HANDOFF_UNEXPECTED_INTEL_REBIND' }
    Test-HandoffIntelPreserved
    $script:M051.Handoff.FinalState=$action
    $script:M051.Handoff.FinalTarget=$target
    $script:M051.Handoff.IntelPackagePreserved=$true
}
function Invoke-AudioHandoff($AutoResult) {
    $mutex=[Threading.Mutex]::new($false,'Global\PHASER360_M051_TRANSACTION')
    $locked=$false
    try {
        try { $locked=$mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked=$true }
        if (-not $locked) { throw 'ANOTHER_M051_TRANSACTION_IS_RUNNING' }
        Add-Type -Path (Join-Path $script:AudioPaths.PackageRoot 'runtime\DeviceBinding.dll')
        [PhaserM051.DeviceBinding]::CheckAbi()
        $runId=Split-Path -Leaf $script:AudioPaths.RunDir
        $runDir=Join-Path $env:SystemDrive "PHASER360_M051_RECOVERY\$runId"
        New-Item -ItemType Directory -Path $runDir -ErrorAction Stop | Out-Null
        $root=$script:AudioPaths.PackageRoot
        foreach ($name in @('RECOVER_WINRE.cmd','CLEANUP_AUDIO.cmd','PHASER360_M051_TEST_SIGNING.cer')) {
            Copy-Item -LiteralPath (Join-Path $root $name) -Destination $runDir -ErrorAction Stop
        }
        Copy-Item -LiteralPath (Join-Path $root 'runtime') -Destination $runDir -Recurse -ErrorAction Stop
        $script:M051=[ordered]@{ Mode='INTEL_HANDOFF_1'; RunDir=$runDir; PackageRoot=$root
            Target=$AutoResult.State.Target; BeforePackages=@()
            InfHash=(Get-FileHash (Join-Path $root 'driver\phaser360_m051_mmio_ro.inf')).Hash
            CertThumbprint=''; OwnedCertStores=@(); CodeIntegrityOptions=$null
            Handoff=[ordered]@{ InitialState=$AutoResult.State; InitialService=$null; Backup=$AutoResult.Backup
                NullAttempted=$false; NullRebootRequired=$false; BindRebootRequired=$false
                FinalState='NOT_CHECKED'; FinalTarget=$null; IntelPackagePreserved=$false } }
        $ops=@{
            Save={ param($s) Write-M051Journal $s }
            Preflight={ param($s)
                $s['Kind']='INTEL_HANDOFF_1'
                $now=Read-AudioState
                if ((Get-AudioStateStamp $now) -cne (Get-AudioStateStamp $script:M051.Handoff.InitialState)) { throw 'HANDOFF_STATE_CHANGED' }
                if ($now.ExperimentalPresent) { throw 'EXPERIMENTAL_PACKAGE_ALREADY_PRESENT' }
                $service=Read-HandoffService
                Assert-HandoffBaseline $now $service @(Get-AudioInventory (Split-Path -Parent $now.Package.OriginalPath))
                $script:M051.Handoff.InitialService=$service
                $backup=$script:M051.Handoff.Backup
                if ($null -eq $backup -or $backup.Verified -ne $true) { throw 'HANDOFF_BACKUP_REQUIRED' }
                $tree=@(Get-AudioInventory $backup.Path)
                $infs=@($tree | Where-Object { [IO.Path]::GetFileName($_.Relative) -ieq 'intcaudiobus.inf' })
                if ($infs.Count -ne 1) { throw 'HANDOFF_BACKUP_INF_AMBIGUOUS' }
                $backupRoot=Split-Path -Parent (Join-Path $backup.Path $infs[0].Relative)
                if (-not (Compare-AudioInventory @(Get-HandoffIntelFiles) @(Get-AudioInventory $backupRoot)).Verified) {
                    throw 'HANDOFF_BACKUP_FILES_CHANGED'
                }
                $script:M051.BeforePackages=@(Get-M051Store | ForEach-Object { $_.PublishedName })
                $cert=[Security.Cryptography.X509Certificates.X509Certificate2]::new(
                    (Join-Path $script:M051.PackageRoot 'PHASER360_M051_TEST_SIGNING.cer'))
                if ($cert.HasPrivateKey -or $cert.NotAfter -lt (Get-Date) -or $cert.NotBefore -gt (Get-Date)) { throw 'INVALID_PUBLIC_TEST_CERTIFICATE' }
                $script:M051.CertThumbprint=$cert.Thumbprint
                $script:M051.CodeIntegrityOptions=('0x{0:X8}' -f $now.CodeIntegrityOptions)
                Write-Host "Recuperare pregatita: $($script:M051.RunDir)\RECOVER_WINRE.cmd"
            }
            Trust={ param($s) Add-M051Trust $s }
            Stage={ param($s) Invoke-M051Pnp @('/add-driver',(Join-Path $script:M051.PackageRoot 'driver\phaser360_m051_mmio_ro.inf')) }
            Discover={ param($s) Find-M051CurrentOwned }
            BeforeBind={ param($s)
                $now=Read-AudioState
                if ((Get-HandoffTargetStamp $now.Target) -cne (Get-HandoffTargetStamp $script:M051.Target)) { throw 'HANDOFF_BINDING_CHANGED' }
                Assert-HandoffBaseline $now (Read-HandoffService) @(Get-AudioInventory (Split-Path -Parent $now.Package.OriginalPath))
                if ($null -eq (Find-M051CurrentOwned)) { throw 'STAGED_PACKAGE_NOT_FOUND' }
            }
            Bind={ param($s)
                $p=Find-M051CurrentOwned
                if ($null -eq $p -or $p.PublishedName -ine $s.OwnedPackage.PublishedName) { throw 'PACKAGE_OWNERSHIP_CHANGED' }
                try {
                    $reboot=Set-HandoffProbeBinding $script:M051.Target.InstanceId $p.OriginalPath
                    $script:M051.Handoff.BindRebootRequired=$reboot
                    Write-M051Journal $s
                    if ($reboot) { throw 'HANDOFF_BIND_REBOOT_REQUIRED: no automatic reboot' }
                } finally { Disable-M051OwnedService $p }
            }
            Snapshot={ param($s) Read-M051Snapshot }
            Remove={ param($s) Remove-HandoffOwnedPackage $s }
            CheckClean={ param($s) Test-HandoffClean $s }
            Untrust={ param($s) Remove-M051Trust }
        }
        $result=Invoke-M051Transaction $ops
        $r=[pscustomobject]@{ Transaction=$result; RecoveryDirectory=$runDir
            Handoff=$script:M051.Handoff; ExitCode=$(if (-not $result.Clean) { 2 } elseif ($result.Error) { 1 } else { 0 }) }
        Write-AudioJson 'probe-result.json' $r
        Copy-AudioProbeReport $runDir
        return $r
    } finally { if ($locked) { $mutex.ReleaseMutex() }; $mutex.Dispose() }
}
