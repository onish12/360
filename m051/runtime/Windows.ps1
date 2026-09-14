Set-StrictMode -Version 2

function Get-M051Target {
    $devices = @(Get-PnpDevice -PresentOnly -ErrorAction Stop |
        Where-Object { $_.InstanceId -like 'PCI\VEN_8086&DEV_3198*' })
    if ($devices.Count -ne 1) { throw 'EXPECTED_ONE_PHASER360_CONTROLLER' }
    $id = $devices[0].InstanceId
    $properties = @(Get-PnpDeviceProperty -InstanceId $id -ErrorAction Stop)
    $data = @{}
    foreach ($p in $properties) { $data[$p.KeyName] = $p.Data }
    foreach ($key in @('DEVPKEY_Device_HardwareIds','DEVPKEY_Device_ProblemCode')) {
        if (-not $data.ContainsKey($key)) { throw "DEVICE_PROPERTY_MISSING: $key" }
    }
    [pscustomobject]@{
        InstanceId = $id; HardwareIds = @($data['DEVPKEY_Device_HardwareIds'])
        Problem = [int]$data['DEVPKEY_Device_ProblemCode']
        Service = [string]$data['DEVPKEY_Device_Service']
        Inf = [string]$data['DEVPKEY_Device_DriverInfPath']
        DriverVersion = [string]$data['DEVPKEY_Device_DriverVersion']
        DriverProvider = [string]$data['DEVPKEY_Device_DriverProvider']
    }
}

function Get-M051Store {
    foreach ($d in @(Get-WindowsDriver -Online -ErrorAction Stop)) {
        $original = [string]$d.OriginalFileName
        $name = [IO.Path]::GetFileName($original)
        $hash = ''
        if ($name -ieq 'phaser360_m051_mmio_ro.inf') {
            $prefix = Join-Path $env:SystemRoot 'System32\DriverStore\FileRepository\'
            if (-not $original.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
                throw 'UNEXPECTED_DRIVERSTORE_PATH'
            }
            $hash = (Get-FileHash -LiteralPath $original -Algorithm SHA256).Hash
        }
        [pscustomobject]@{ PublishedName = [string]$d.Driver; OriginalName = $name
            OriginalPath = $original; Hash = $hash }
    }
}

function Write-M051Journal($State) {
    $record = [ordered]@{ Transaction = $State; Context = $script:M051 }
    $bytes = [Text.Encoding]::UTF8.GetBytes(($record | ConvertTo-Json -Depth 12))
    $temp = Join-Path $script:M051.RunDir 'journal.new'
    $stream = [IO.FileStream]::new($temp, [IO.FileMode]::Create, [IO.FileAccess]::Write,
        [IO.FileShare]::None, 4096, [IO.FileOptions]::WriteThrough)
    try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) }
    finally { $stream.Dispose() }
    Move-Item -LiteralPath $temp -Destination (Join-Path $script:M051.RunDir 'journal.json') -Force
}

function Invoke-M051Pnp([string[]]$Arguments) {
    # Synchronous: do not start rollback concurrently with an unfinished installer.
    $text = (& "$env:SystemRoot\System32\pnputil.exe" @Arguments 2>&1 | Out-String)
    $code = $LASTEXITCODE
    Add-Content -LiteralPath (Join-Path $script:M051.RunDir 'pnputil.log') -Value (
        "ARGS=" + ($Arguments -join ' ') + "`r`nEXIT=$code`r`n$text")
    if ($code -eq 3010 -or $code -eq 1641) { throw "REBOOT_REQUIRED_$code; no automatic reboot requested" }
    if ($code -ne 0) { throw "PNPUTIL_FAILED_$code; see pnputil.log" }
}

function Find-M051CurrentOwned {
    Find-M051OwnedPackage $script:M051.BeforePackages @(Get-M051Store) $script:M051.InfHash
}

function Disable-M051OwnedService($Package) {
    $key = 'HKLM:\SYSTEM\CurrentControlSet\Services\phaser360_m051_mmio_ro'
    if (-not (Test-Path -LiteralPath $key)) { return }
    $expected = Join-Path ([IO.Path]::GetDirectoryName($Package.OriginalPath)) 'phaser360_m051_mmio_ro.sys'
    $raw = [string](Get-ItemProperty -LiteralPath $key -Name ImagePath -ErrorAction Stop).ImagePath
    $actual = [Environment]::ExpandEnvironmentVariables($raw).Trim('"')
    if ($actual.StartsWith('\??\')) { $actual = $actual.Substring(4) }
    if ($actual.StartsWith('\SystemRoot\', [StringComparison]::OrdinalIgnoreCase)) {
        $actual = Join-Path $env:SystemRoot $actual.Substring(12)
    }
    if ($actual -ine $expected) { throw 'SERVICE_OWNERSHIP_UNPROVEN' }
    # Prevent subsequent boot loading if cleanup is interrupted; only our service.
    Set-ItemProperty -LiteralPath $key -Name Start -Type DWord -Value 4
    if ((Get-ItemProperty -LiteralPath $key -Name Start).Start -ne 4) {
        throw 'COULD_NOT_DISABLE_OWNED_SERVICE'
    }
}

function Remove-M051OwnedPackage($State) {
    $p = Find-M051CurrentOwned
    if ($null -eq $p -or $p.PublishedName -ine $State.OwnedPackage.PublishedName) {
        throw 'PACKAGE_OWNERSHIP_CHANGED'
    }
    Disable-M051OwnedService $p
    Invoke-M051Pnp @('/delete-driver', $p.PublishedName, '/uninstall')
}

function Test-M051Clean {
    if (@(Get-M051Store | Where-Object { $_.OriginalName -ieq 'phaser360_m051_mmio_ro.inf' }).Count -ne 0) {
        throw 'OWNED_PACKAGE_STILL_PRESENT'
    }
    # No rescan/update of unrelated devices and no fallback-driver installation.
    $target = Get-M051Target
    if ($target.InstanceId -ine $script:M051.Target.InstanceId) { throw 'TARGET_INSTANCE_CHANGED' }
    Assert-M051CleanTarget $target
}

function Add-M051Trust($State) {
    $cert = [Security.Cryptography.X509Certificates.X509Certificate2]::new(
        (Join-Path $script:M051.PackageRoot 'PHASER360_M051_TEST_SIGNING.cer'))
    foreach ($store in @('Root','TrustedPublisher')) {
        $path = "Cert:\LocalMachine\$store\$($cert.Thumbprint)"
        if (-not (Test-Path -LiteralPath $path)) {
            # Record intent before importing, including a partially successful import.
            $script:M051.OwnedCertStores += $store
            Write-M051Journal $State
            Import-Certificate -FilePath (Join-Path $script:M051.PackageRoot 'PHASER360_M051_TEST_SIGNING.cer') `
                -CertStoreLocation "Cert:\LocalMachine\$store" -ErrorAction Stop | Out-Null
            if (-not (Test-Path -LiteralPath $path)) { throw 'CERT_IMPORT_NOT_CONFIRMED' }
        }
    }
}

function Remove-M051Trust {
    $errors = @()
    foreach ($store in @($script:M051.OwnedCertStores)) {
        try {
            if ($store -notin @('Root','TrustedPublisher') -or $script:M051.CertThumbprint -notmatch '^[0-9A-F]{40}$') {
                throw 'INVALID_CERT_OWNERSHIP_RECORD'
            }
            $path = "Cert:\LocalMachine\$store\$($script:M051.CertThumbprint)"
            if (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -ErrorAction Stop }
            if (Test-Path -LiteralPath $path) { throw 'CERT_STILL_PRESENT' }
        } catch { $errors += $_.Exception.Message }
    }
    if ($errors.Count) { throw ($errors -join '; ') }
}

function Read-M051Snapshot {
    $target = Get-M051Target
    if ($target.InstanceId -ine $script:M051.Target.InstanceId -or $target.Problem -ne 0 -or
        $target.Service -ine 'phaser360_m051_mmio_ro' -or
        $target.Inf -ine (Find-M051CurrentOwned).PublishedName) { throw 'PROBE_NOT_BOUND_TO_TARGET' }
    $bytes = [PhaserM051.Native]::ReadSnapshot($target.InstanceId)
    if ([BitConverter]::ToUInt32($bytes,0) -ne 0x354D4850 -or
        [BitConverter]::ToUInt32($bytes,4) -ne 0x00050100 -or
        [BitConverter]::ToUInt32($bytes,24) -ne 0x4000 -or
        [BitConverter]::ToUInt32($bytes,28) -ne 0x100000 -or
        [BitConverter]::ToUInt32($bytes,48) -ne 1 -or
        [BitConverter]::ToUInt32($bytes,52) -ne 0) { throw 'SNAPSHOT_CONTRACT_MISMATCH' }
    [IO.File]::WriteAllBytes((Join-Path $script:M051.RunDir 'snapshot.bin'), $bytes)
    $snapshot = [ordered]@{
        Version = '0.5.1'; InstanceId = $target.InstanceId
        HdaPhysical = ('0x{0:X16}' -f [BitConverter]::ToUInt64($bytes,8))
        DspPhysical = ('0x{0:X16}' -f [BitConverter]::ToUInt64($bytes,16))
        HdaLength = '0x4000'; DspLength = '0x100000'
        HdaGcap = ('0x{0:X8}' -f [BitConverter]::ToUInt32($bytes,32))
        HdaVersion = ('0x{0:X8}' -f [BitConverter]::ToUInt32($bytes,36))
        DspAdspcs = ('0x{0:X8}' -f [BitConverter]::ToUInt32($bytes,40))
        DspAdspis = ('0x{0:X8}' -f [BitConverter]::ToUInt32($bytes,44))
    }
    $snapshot | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $script:M051.RunDir 'snapshot.json') -Encoding UTF8
    return $snapshot
}
