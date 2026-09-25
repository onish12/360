$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$root=Join-Path $PSScriptRoot '..'
$entry=Get-Content -LiteralPath (Join-Path $root 'm062\driver\driver_entry.cpp') -Raw
$owner=Get-Content -LiteralPath (Join-Path $root 'm062\driver\device_owner.cpp') -Raw
$ownerH=Get-Content -LiteralPath (Join-Path $root 'm062\driver\device_owner.h') -Raw
$pinned=Get-Content -LiteralPath (Join-Path $root 'm062\driver\pinned_firmware.h') -Raw
$project=Get-Content -LiteralPath (Join-Path $root 'm062\driver\phaser360_boot_dma.vcxproj') -Raw

foreach($required in @(
    'WDF_DRIVER_CONFIG_INIT','WdfDriverCreate','Phaser360EvtDeviceAdd',
    'PnpResources::Configure','WdfDeviceCreate','DeviceOwner::CreateInDeviceContext'
)) {
    if($entry.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "DRIVER_ENTRY_REQUIRED_MISSING: $required"
    }
}

$configure=$entry.IndexOf('PnpResources::Configure',[StringComparison]::Ordinal)
$create=$entry.IndexOf('WdfDeviceCreate',[StringComparison]::Ordinal)
$ownerCreate=$entry.IndexOf('DeviceOwner::CreateInDeviceContext',[StringComparison]::Ordinal)
if(-not($configure -ge 0 -and $configure -lt $create -and $create -lt $ownerCreate)) {
    throw "DEVICEADD_ORDER_INVALID: configure=$configure create=$create owner=$ownerCreate"
}

foreach($required in @(
    'WdfObjectAllocateContext',
    'EvtCleanupCallback=DeviceOwnerContextCleanup',
    'pnp_.Attach(device_)',
    'pnp_.InstallLifecycle(lifecycle_.Ops())',
    'lifecycle_.CreateInterruptShell(device_)',
    'StagePinnedFirmwareFromSource('
)) {
    if($owner.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "DEVICE_OWNER_REQUIRED_MISSING: $required"
    }
}

foreach($required in @(
    'alignas(DeviceOwner)',
    'WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DeviceOwnerContext,GetDeviceOwnerContext)',
    'FirmwareReady() const noexcept'
)) {
    if($ownerH.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "DEVICE_CONTEXT_REQUIRED_MISSING: $required"
    }
}
if($pinned.IndexOf('bool Loaded() const noexcept',[StringComparison]::Ordinal) -lt 0) {
    throw 'PINNED_FIRMWARE_LOADED_GUARD_MISSING'
}

# DeviceAdd must not stage firmware implicitly. H7 narrows the owner API to
# StageEmbeddedFirmware, but connection remains a separate later milestone.
if($entry -match '(?i)StageEmbeddedFirmware\s*\(') {
    throw 'DEVICEADD_MUST_NOT_STAGE_EMBEDDED_FIRMWARE'
}
if($owner -match 'StageFirmware\s*\(\s*const\s+UCHAR\*') {
    throw 'ARBITRARY_FIRMWARE_BUFFER_API_MUST_NOT_RETURN'
}

foreach($forbidden in @(
    'ZwCreateFile','ZwReadFile','NtCreateFile','IoCreateFile',
    'WdfIoTargetOpen','WdfIoTargetSendReadSynchronously',
    'MmMapIoSpace','READ_REGISTER_','WRITE_REGISTER_',
    'MAX98357','DA7219','DLGS7219','MX98357',
    'WaveRT','ACX','AudioEngine','speaker','playback'
)) {
    if($entry.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "DRIVER_ENTRY_FORBIDDEN_PRESENT: $forbidden"
    }
}

foreach($forbidden in @(
    'ZwCreateFile','ZwReadFile','NtCreateFile','IoCreateFile',
    'WdfIoTargetOpen','WdfIoTargetSendReadSynchronously'
)) {
    if($owner.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "DEVICE_OWNER_FILE_IO_FORBIDDEN: $forbidden"
    }
}

if($project.IndexOf('<ConfigurationType>StaticLibrary</ConfigurationType>',
                    [StringComparison]::Ordinal) -lt 0) {
    throw 'H6_PROJECT_MUST_REMAIN_STATIC_LIBRARY'
}
foreach($source in @('device_owner.cpp','driver_entry.cpp')) {
    if($project.IndexOf('<ClCompile Include="'+$source+'" />',
                        [StringComparison]::Ordinal) -lt 0) {
        throw "H6_WDK_SOURCE_NOT_COMPILED: $source"
    }
}

Write-Host 'H6_DEVICE_OWNER_STATIC_TESTS=PASS; driverentry=YES; deviceadd_order=YES; multi_context=YES; driverentry_firmware_io=NO; arbitrary_buffer_api=NO; file_io=NO; playback=NO; installable=NO'
