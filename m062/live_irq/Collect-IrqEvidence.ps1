#requires -Version 5.1
param(
    [string]$OutputRoot = '',
    [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

function Assert-PnpReadOnlyArguments([string[]]$Arguments) {
    if ($null -eq $Arguments -or $Arguments.Count -lt 3 -or
        $Arguments[0] -ine '/enum-devices') {
        throw 'PNPUTIL_READONLY_ENUM_ONLY'
    }
    $allowed = @('/enum-devices','/instanceid','/deviceids','/services','/stack',
        '/drivers','/properties','/resources','/connected')
    foreach ($arg in $Arguments) {
        if ($arg.StartsWith('/')) {
            if ($arg.ToLowerInvariant() -notin $allowed) {
                throw "PNPUTIL_ARGUMENT_NOT_ALLOWED: $arg"
            }
        }
    }
    $instanceIndex = [Array]::IndexOf($Arguments, '/instanceid')
    if ($instanceIndex -lt 0 -or $instanceIndex + 1 -ge $Arguments.Count) {
        throw 'PNPUTIL_INSTANCE_REQUIRED'
    }
    if ($Arguments[$instanceIndex + 1] -notlike 'PCI\VEN_8086&DEV_3198*') {
        throw 'PNPUTIL_TARGET_NOT_DEV3198'
    }
}

function Invoke-PnpReadOnly([string]$PnPUtil,[string[]]$Arguments,[string]$Destination) {
    Assert-PnpReadOnlyArguments $Arguments
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $text = (& $PnPUtil @Arguments 2>&1 | Out-String -Width 8192)
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldPreference
    }
    @(
        'COMMAND=pnputil.exe ' + ($Arguments -join ' ')
        "EXIT=$code"
        '--- OUTPUT ---'
        $text
    ) | Set-Content -LiteralPath $Destination -Encoding UTF8
    return [pscustomobject]@{ ExitCode=$code; Output=$text }
}

function Get-PhaserTargetState {
    $devices = @(Get-PnpDevice -PresentOnly -ErrorAction Stop |
        Where-Object { $_.InstanceId -like 'PCI\VEN_8086&DEV_3198*' })
    if ($devices.Count -ne 1) { throw "EXPECTED_ONE_PRESENT_DEV3198: count=$($devices.Count)" }

    $id = [string]$devices[0].InstanceId
    $properties = @(Get-PnpDeviceProperty -InstanceId $id -ErrorAction Stop)
    $map = @{}
    foreach ($p in $properties) { $map[[string]$p.KeyName] = $p.Data }

    if (-not $map.ContainsKey('DEVPKEY_Device_HardwareIds') -or
        -not $map.ContainsKey('DEVPKEY_Device_ProblemCode')) {
        throw 'REQUIRED_DEVICE_PROPERTY_MISSING'
    }

    [pscustomobject]@{
        InstanceId = $id
        Status = [string]$devices[0].Status
        Class = [string]$devices[0].Class
        ProblemCode = [int]$map['DEVPKEY_Device_ProblemCode']
        Service = [string]$map['DEVPKEY_Device_Service']
        DriverInfPath = [string]$map['DEVPKEY_Device_DriverInfPath']
        DriverVersion = [string]$map['DEVPKEY_Device_DriverVersion']
        DriverProvider = [string]$map['DEVPKEY_Device_DriverProvider']
        HardwareIds = @($map['DEVPKEY_Device_HardwareIds'])
    }
}

function Test-StableState($Before,$After) {
    foreach ($name in @('InstanceId','ProblemCode','Service','DriverInfPath',
                        'DriverVersion','DriverProvider')) {
        if ([string]$Before.$name -cne [string]$After.$name) { return $false }
    }
    return $true
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Write-Hashes([string]$Directory) {
    $sum = Join-Path $Directory 'SHA256SUMS.txt'
    Get-ChildItem -LiteralPath $Directory -File |
        Where-Object { $_.Name -ne 'SHA256SUMS.txt' } |
        Sort-Object Name |
        ForEach-Object {
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$hash  $($_.Name)"
        } | Set-Content -LiteralPath $sum -Encoding ASCII
}

if ($SelfTest) {
    $fixture = 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06\FIXTURE'
    Assert-PnpReadOnlyArguments @('/enum-devices','/instanceid',$fixture,'/resources')
    Assert-PnpReadOnlyArguments @('/enum-devices','/instanceid',$fixture,
        '/deviceids','/services','/stack','/drivers','/properties','/resources')
    $rejected = 0
    foreach ($bad in @(
        @('/restart-device',$fixture),
        @('/enum-devices','/deviceid','PCI\VEN_8086&DEV_3198','/resources'),
        @('/enum-devices','/instanceid','PCI\VEN_1234&DEV_5678\X','/resources'),
        @('/enum-devices','/instanceid',$fixture,'/enable-device')
    )) {
        try { Assert-PnpReadOnlyArguments $bad }
        catch { $rejected++ }
    }
    if ($rejected -ne 4) { throw "SELFTEST_REJECTION_COUNT=$rejected" }
    Write-Host 'IRQ_CAPTURE_SELFTEST=PASS; readonly_enum_only=YES; mutation_commands=REJECTED'
    return
}

if (-not (Test-IsAdministrator)) { throw 'ADMINISTRATOR_REQUIRED_FOR_PNPUTIL_ENUMERATION' }
if (-not [Environment]::Is64BitProcess) { throw 'WINDOWS_X64_PROCESS_REQUIRED' }
if ([Environment]::OSVersion.Version.Build -lt 22621) {
    throw "WINDOWS_11_22H2_OR_NEWER_REQUIRED: build=$([Environment]::OSVersion.Version.Build)"
}

$pnp = Join-Path $env:SystemRoot 'System32\pnputil.exe'
if (-not (Test-Path -LiteralPath $pnp -PathType Leaf)) { throw 'PNPUTIL_NOT_FOUND' }

$before = Get-PhaserTargetState
$id = $before.InstanceId
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$suffix = [Guid]::NewGuid().ToString('N').Substring(0,8)
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = $PSScriptRoot }
$runDir = Join-Path $OutputRoot ('IRQ_EVIDENCE_' + $stamp + '_' + $suffix)
New-Item -ItemType Directory -Path $runDir -Force | Out-Null

$before | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $runDir 'target_before.json') -Encoding UTF8

$allProperties = @(Get-PnpDeviceProperty -InstanceId $id -ErrorAction Stop |
    ForEach-Object {
        [pscustomobject]@{
            KeyName = [string]$_.KeyName
            Type = [string]$_.Type
            Data = $_.Data
        }
    })
$allProperties | ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath (Join-Path $runDir 'device_properties.json') -Encoding UTF8

$resourceArgs = @('/enum-devices','/instanceid',$id,'/resources')
$fullArgs = @('/enum-devices','/instanceid',$id,
    '/deviceids','/services','/stack','/drivers','/properties','/resources')

$resource = Invoke-PnpReadOnly $pnp $resourceArgs (Join-Path $runDir 'pnputil_resources.txt')
$full = Invoke-PnpReadOnly $pnp $fullArgs (Join-Path $runDir 'pnputil_full.txt')

$after = Get-PhaserTargetState
$after | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $runDir 'target_after.json') -Encoding UTF8

$stable = Test-StableState $before $after
$complete = ($resource.ExitCode -eq 0 -and $full.ExitCode -eq 0 -and
             -not [string]::IsNullOrWhiteSpace($resource.Output) -and
             -not [string]::IsNullOrWhiteSpace($full.Output))
$status = if ($complete -and $stable) { 'CAPTURE_COMPLETE_STABLE' }
          elseif (-not $stable) { 'STATE_CHANGED_DURING_CAPTURE' }
          else { 'CAPTURE_PARTIAL' }

@(
    "STATUS=$status"
    "WINDOWS_BUILD=$([Environment]::OSVersion.Version.Build)"
    "TARGET_INSTANCE=$id"
    "TARGET_PROBLEM_BEFORE=$($before.ProblemCode)"
    "TARGET_PROBLEM_AFTER=$($after.ProblemCode)"
    "TARGET_SERVICE_BEFORE=$($before.Service)"
    "TARGET_SERVICE_AFTER=$($after.Service)"
    "TARGET_INF_BEFORE=$($before.DriverInfPath)"
    "TARGET_INF_AFTER=$($after.DriverInfPath)"
    "PNPUTIL_RESOURCES_EXIT=$($resource.ExitCode)"
    "PNPUTIL_FULL_EXIT=$($full.ExitCode)"
    "STATE_STABLE=$($stable.ToString().ToUpperInvariant())"
    'MODE=READ_ONLY_ENUMERATION'
    'DRIVER_INSTALL=NO'
    'DRIVER_BIND_UNBIND=NO'
    'DEVICE_RESTART=NO'
    'DEVICE_ENABLE_DISABLE=NO'
    'MMIO=NO'
    'DSP_BOOT=NO'
    'WDF_INTERRUPT_CREATE=NO'
    'IRQ_SELECTION=DEFERRED'
    'AUDIO_PLAYBACK=NO'
) | Set-Content -LiteralPath (Join-Path $runDir 'RESULT.txt') -Encoding UTF8

Write-Hashes $runDir

$zip = Join-Path $OutputRoot ('RESULT_IRQ_READONLY_' + $stamp + '_' + $suffix + '.zip')
Compress-Archive -Path (Join-Path $runDir '*') -DestinationPath $zip -Force
Write-Host "STATUS=$status"
Write-Host "TARGET=$id"
Write-Host "Trimite fisierul: $zip"
if ($status -ne 'CAPTURE_COMPLETE_STABLE') { exit 2 }
