Set-StrictMode -Version 2
function Write-AudioJson([string]$Name,$Value) {
    $Value | ConvertTo-Json -Depth 14 | Set-Content -LiteralPath (Join-Path $script:AudioPaths.Report $Name) -Encoding UTF8 -ErrorAction Stop
}
function Invoke-AudioCommand([string]$Exe,[string[]]$Arguments,[string]$Log) {
    # Capture native stderr on PS 5.1 and use the actual exit code.
    $ErrorActionPreference='Continue'
    $output=(& (Join-Path "$env:SystemRoot\System32" $Exe) @Arguments 2>&1 | Out-String)
    $code=$LASTEXITCODE
    $output | Set-Content -LiteralPath (Join-Path $script:AudioPaths.Report $Log) -Encoding UTF8 -ErrorAction Stop
    [pscustomobject]@{ ExitCode=$code; Log=$Log; Arguments=$Arguments }
}
function ConvertTo-AudioPackage($Driver) {
    $path=[IO.Path]::GetFullPath([string](Get-AudioValue $Driver 'OriginalFileName'))
    $prefix=Join-Path $env:SystemRoot 'System32\DriverStore\FileRepository\'
    if (-not $path.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)) { throw 'PACKAGE_OUTSIDE_DRIVERSTORE' }
    $boot=Get-AudioValue $Driver 'BootCritical'
    if ($boot -isnot [bool]) { $boot=$null }
    [pscustomobject]@{ PublishedName=[string](Get-AudioValue $Driver 'Driver')
        OriginalPath=$path; OriginalName=[IO.Path]::GetFileName($path)
        Version=[string](Get-AudioValue $Driver 'Version'); Provider=[string](Get-AudioValue $Driver 'ProviderName')
        BootCritical=$boot; InfHash=(Get-FileHash -LiteralPath $path -Algorithm SHA256 -ErrorAction Stop).Hash }
}
function Read-AudioState {
    Write-Host 'Identific legatura actuala a controlerului...'
    $target=Get-M051Target
    $store=@(Get-WindowsDriver -Online -All -ErrorAction Stop)
    $experimental=@($store | Where-Object { [IO.Path]::GetFileName([string]$_.OriginalFileName) -like 'phaser360_m05*.inf' })
    $package=$null
    if ($target.Inf -match '^oem[0-9]+\.inf$') {
        $match=@($store | Where-Object { [string]$_.Driver -ieq $target.Inf })
        if ($match.Count -ne 1) { throw 'BOUND_PACKAGE_NOT_UNIQUE_IN_DRIVERSTORE' }
        $package=ConvertTo-AudioPackage $match[0]
    }
    $ci=$null
    try { $ci=[PhaserM051.Native]::CodeIntegrityOptions() } catch { }
    [pscustomobject]@{ Target=$target; Package=$package; CodeIntegrityOptions=$ci
        ExperimentalPresent=($experimental.Count -ne 0 -or
            (Test-Path -LiteralPath 'HKLM:\SYSTEM\CurrentControlSet\Services\phaser360_m051_mmio_ro'))
        ExperimentalPackages=@($experimental | Select-Object Driver,OriginalFileName,Version) }
}
function Get-AudioInventory([string]$Root) {
    $rootPath=[IO.Path]::GetFullPath($Root).TrimEnd('\')
    $pending=[Collections.Generic.Stack[string]]::new()
    $pending.Push($rootPath)
    while ($pending.Count) {
        $dir=$pending.Pop()
        if (((Get-Item -LiteralPath $dir -Force -ErrorAction Stop).Attributes -band
            [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'PACKAGE_REPARSE_DIRECTORY' }
        foreach ($item in Get-ChildItem -LiteralPath $dir -Force -ErrorAction Stop) {
            if (($item.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0) { throw 'PACKAGE_REPARSE_ENTRY' }
            if ($item.PSIsContainer) { $pending.Push($item.FullName) }
            else {
                [pscustomobject]@{ Relative=$item.FullName.Substring($rootPath.Length+1).Replace('\','/')
                    Length=[long]$item.Length; Hash=(Get-FileHash -LiteralPath $item.FullName -Algorithm SHA256 -ErrorAction Stop).Hash }
            }
        }
    }
}
function Compare-AudioInventory($Before,$Exported) {
    $source=@{}; $seen=@{}
    foreach ($entry in @($Before)) {
        if ($source.ContainsKey($entry.Relative)) { throw 'DUPLICATE_SOURCE_PATH' }
        $source[$entry.Relative]=$entry
    }
    foreach ($entry in @($Exported)) {
        if ($seen.ContainsKey($entry.Relative)) { throw 'DUPLICATE_EXPORT_PATH' }
        if (-not $source.ContainsKey($entry.Relative)) { throw 'UNEXPECTED_EXPORTED_FILE' }
        $expected=$source[$entry.Relative]
        if ($expected.Length -ne $entry.Length -or $expected.Hash -ine $entry.Hash) { throw "EXPORTED_FILE_MISMATCH: $($entry.Relative)" }
        $seen[$entry.Relative]=$true
    }
    if ($seen.Count -eq 0) { throw 'EMPTY_EXPORT' }
    $missing=@($source.Keys | Where-Object { -not $seen.ContainsKey($_) } | Sort-Object)
    [pscustomobject]@{ Verified=($missing.Count -eq 0); MissingSourceFiles=$missing; Files=$seen.Count }
}
function Export-AudioPackage($State) {
    Write-Host 'Salvez pachetul instalat si compar fisierele prin SHA-256...'
    $p=$State.Package
    if ($p.PublishedName -notmatch '^oem[0-9]+\.inf$') { throw 'INVALID_EXPORT_INF' }
    $now=Read-AudioState
    if ((Get-AudioStateStamp $State) -cne (Get-AudioStateStamp $now)) { throw 'BINDING_CHANGED_BEFORE_EXPORT' }
    $sourceRoot=Split-Path -Parent $p.OriginalPath
    $before=@(Get-AudioInventory $sourceRoot | Sort-Object Relative)
    Write-AudioJson 'source-files.json' $before
    $backup=Join-Path $script:AudioPaths.RunDir 'DriverBackup'
    New-Item -ItemType Directory -Path $backup -ErrorAction Stop | Out-Null
    $cmd=Invoke-AudioCommand 'pnputil.exe' @('/export-driver',$p.PublishedName,$backup) 'export-driver.txt'
    Write-AudioJson 'export-command.json' $cmd
    if ($cmd.ExitCode -ne 0) { throw "DRIVER_EXPORT_FAILED: $($cmd.ExitCode)" }
    $exportTree=@(Get-AudioInventory $backup)
    $infs=@($exportTree | Where-Object { [IO.Path]::GetFileName($_.Relative) -ieq $p.OriginalName -and $_.Hash -ieq $p.InfHash })
    if ($infs.Count -ne 1) { throw 'EXPORTED_INF_NOT_UNIQUE_OR_HASH_MISMATCH' }
    $exportInf=Join-Path $backup $infs[0].Relative
    $exportRoot=Split-Path -Parent $exportInf
    $exported=@(Get-AudioInventory $exportRoot | Sort-Object Relative)
    Write-AudioJson 'exported-files.json' $exported
    $verified=Compare-AudioInventory $before $exported
    $after=@(Get-AudioInventory $sourceRoot | Sort-Object Relative)
    if (($before | ConvertTo-Json -Compress) -cne ($after | ConvertTo-Json -Compress)) { throw 'SOURCE_PACKAGE_CHANGED_DURING_EXPORT' }
    Copy-Item -LiteralPath $exportInf -Destination (Join-Path $script:AudioPaths.Report 'bound-driver.inf.txt') -ErrorAction Stop
    $record=[pscustomobject]@{ Path=$backup; Verified=$verified.Verified; Files=$verified.Files
        InfHash=$p.InfHash; MissingSourceFiles=$verified.MissingSourceFiles
        Scope='Driver package files only; not a full Windows backup or tested rollback.' }
    Write-AudioJson 'backup.json' $record
    return $record
}
function Read-AudioContext($State) {
    Write-Host 'Citesc serviciul, dispozitivele asociate si configuratia recuperarii...'
    $context=[ordered]@{ Warnings=@(); WinRE=$null; RecoveryBootTested=$false
        RecoveryVolumeAccessConfirmed=$false; DriverRemovalValidated=$false }
    $id=$State.Target.InstanceId
    $captures=@{
        WinRE={
            $c=Invoke-AudioCommand 'reagentc.exe' @('/info') 'WinRE.txt'
            if ($c.ExitCode -ne 0) { throw "REAGENTC_INFO_FAILED: $($c.ExitCode)" }
            $c
        }
        PnP={
            $a=@('/enum-devices','/instanceid',$id,'/relations','/services','/stack','/drivers','/properties')
            if ([Environment]::OSVersion.Version.Build -ge 22621) { $a+='/resources' }
            $c=Invoke-AudioCommand 'pnputil.exe' $a 'controller.txt'
            if ($c.ExitCode -ne 0) { throw "PNP_CONTEXT_FAILED: $($c.ExitCode)" }
            $c
        }
        Service={
            $service=$State.Target.Service
            if ([string]::IsNullOrWhiteSpace($service)) { return 'UNBOUND' }
            if ($service -notmatch '^[A-Za-z0-9_.-]{1,256}$') { throw 'UNEXPECTED_SERVICE_NAME' }
            $c=Invoke-AudioCommand 'reg.exe' @('query',"HKLM\SYSTEM\CurrentControlSet\Services\$service",'/s') 'service-registry.txt'
            if ($c.ExitCode -ne 0) { throw "SERVICE_READ_FAILED: $($c.ExitCode)" }
            $drivers=@(Get-CimInstance Win32_SystemDriver -Filter "Name='$service'" -ErrorAction Stop |
                Select-Object Name,State,Started,StartMode,PathName,ServiceType,ErrorControl)
            Write-AudioJson 'service-state.json' $drivers
            $c
        }
        Bindings={
            $inf=$State.Target.Inf
            $drivers=@(Get-CimInstance Win32_PnPSignedDriver -ErrorAction Stop |
                Where-Object { -not [string]::IsNullOrWhiteSpace($inf) -and $_.InfName -ieq $inf } |
                Select-Object DeviceID,DeviceName,DeviceClass,InfName,DriverVersion,DriverProviderName,IsSigned,Signer)
            Write-AudioJson 'package-bindings.json' $drivers
            [pscustomobject]@{ Count=$drivers.Count; Scope='WMI-reported bindings; not proof of exclusive package use.' }
        }
        AudioDevices={
            $devices=@(Get-PnpDevice -PresentOnly -ErrorAction Stop | Where-Object {
                $_.Class -in @('MEDIA','AudioEndpoint') -or $_.InstanceId -like 'ACPI\DLGS7219*' -or $_.InstanceId -like 'ACPI\MX98357A*'
            } | Select-Object Status,Class,FriendlyName,InstanceId)
            Write-AudioJson 'audio-devices.json' $devices
            [pscustomobject]@{ Count=$devices.Count }
        }
        Encryption={
            if (-not (Get-Command Get-BitLockerVolume -ErrorAction SilentlyContinue)) { throw 'BITLOCKER_QUERY_UNAVAILABLE' }
            $vol=Get-BitLockerVolume -MountPoint $env:SystemDrive -ErrorAction Stop |
                Select-Object MountPoint,VolumeStatus,ProtectionStatus,LockStatus,EncryptionMethod
            Write-AudioJson 'volume-status.json' $vol
            'RECOVERY_KEYS_NOT_COLLECTED'
        }
    }
    foreach ($name in @('WinRE','PnP','Service','Bindings','AudioDevices','Encryption')) {
        try { $context[$name]=& $captures[$name] }
        catch { $context.Warnings+="$name : $($_.Exception.Message)" }
    }
    $context['WindowsBuild']=[Environment]::OSVersion.Version.ToString()
    return [pscustomobject]$context
}
function Invoke-AudioProbe {
    Write-Host 'Pornesc proba compilata M0.5.1...'
    $resultFile=Join-Path $script:AudioPaths.Report 'probe-result.json'
    $entry=Join-Path $script:AudioPaths.PackageRoot 'runtime\RUN_M051.ps1'
    $engine=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    & $engine -NoLogo -NoProfile -ExecutionPolicy Bypass -File $entry -ResultFile $resultFile
    $code=$LASTEXITCODE
    if (-not (Test-Path -LiteralPath $resultFile -PathType Leaf)) { throw "PROBE_RESULT_MISSING: exit=$code" }
    $r=Get-Content -LiteralPath $resultFile -Raw -ErrorAction Stop | ConvertFrom-Json
    $r | Add-Member -NotePropertyName ExitCode -NotePropertyValue $code
    $expected=Join-Path $env:SystemDrive 'PHASER360_M051_RECOVERY\'
    $dir=[IO.Path]::GetFullPath($r.RecoveryDirectory)
    if (-not $dir.StartsWith($expected,[StringComparison]::OrdinalIgnoreCase) -or
        [IO.Path]::GetFileName($dir) -notmatch '^[0-9]{8}_[0-9]{6}_[0-9a-f]{8}$') { throw 'PROBE_RECOVERY_PATH_INVALID' }
    $copy=Join-Path $script:AudioPaths.Report 'probe'
    New-Item -ItemType Directory -Path $copy -ErrorAction Stop | Out-Null
    foreach ($name in @('RESULT.txt','journal.json','snapshot.json','snapshot.bin','pnputil.log')) {
        $file=Join-Path $dir $name
        if (Test-Path -LiteralPath $file -PathType Leaf) { Copy-Item -LiteralPath $file -Destination $copy -ErrorAction Stop }
    }
    return $r
}
