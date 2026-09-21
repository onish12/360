$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$root=Join-Path $PSScriptRoot '..'
$sourceH=Get-Content -LiteralPath (Join-Path $root 'm062\driver\firmware_source.h') -Raw
$sourceC=Get-Content -LiteralPath (Join-Path $root 'm062\driver\firmware_source.cpp') -Raw
$ownerH=Get-Content -LiteralPath (Join-Path $root 'm062\driver\device_owner.h') -Raw
$ownerC=Get-Content -LiteralPath (Join-Path $root 'm062\driver\device_owner.cpp') -Raw
$entry=Get-Content -LiteralPath (Join-Path $root 'm062\driver\driver_entry.cpp') -Raw
$generator=Get-Content -LiteralPath (Join-Path $root 'scripts\generate-embedded-firmware.py') -Raw
$project=Get-Content -LiteralPath (Join-Path $root 'm062\driver\phaser360_boot_dma.vcxproj') -Raw
$workflow=Get-Content -LiteralPath (Join-Path $root '.github\workflows\build-m062-dma.yml') -Raw

foreach($required in @(
    'struct FirmwareSourceView',
    'FirmwareSourceProvider',
    'StagePinnedFirmwareFromSource',
    'GetEmbeddedFirmwareSource'
)) {
    if($sourceH.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "H7_SOURCE_CONTRACT_MISSING: $required"
    }
}

foreach($required in @(
    'view.bytes!=kPinnedImageBytes',
    'owner.Load(device,view.data,view.bytes)',
    'view=FirmwareSourceView{}'
)) {
    if($sourceC.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "H7_STAGE_GUARD_MISSING: $required"
    }
}

if($ownerH.IndexOf('StageEmbeddedFirmware() noexcept',[StringComparison]::Ordinal) -lt 0) {
    throw 'H7_OWNER_EMBEDDED_STAGE_MISSING'
}
if($ownerH -match 'StageFirmware\s*\(\s*const\s+UCHAR\*') {
    throw 'H7_ARBITRARY_OWNER_BUFFER_API_PRESENT'
}
if($ownerC.IndexOf('GetEmbeddedFirmwareSource',[StringComparison]::Ordinal) -lt 0) {
    throw 'H7_OWNER_NOT_BOUND_TO_EMBEDDED_PROVIDER_CONTRACT'
}
if($entry -match '(?i)StageEmbeddedFirmware\s*\(') {
    throw 'H7_DEVICEADD_AUTOSTAGE_FORBIDDEN'
}

foreach($forbidden in @(
    'ZwCreateFile','ZwReadFile','NtCreateFile','IoCreateFile',
    'WdfIoTargetOpen','WdfIoTargetSendReadSynchronously'
)) {
    if($sourceC.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0 -or
       $ownerC.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "H7_RUNTIME_FILE_IO_FORBIDDEN: $forbidden"
    }
}

foreach($net in @(
    'urllib','urlopen(','requests.','http.client',
    'Invoke-WebRequest','curl ','wget '
)) {
    if($generator.IndexOf($net,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "H7_GENERATOR_NETWORK_FORBIDDEN: $net"
    }
}

foreach($required in @(
    'sof_glk_reference.json',
    'firmware size mismatch',
    'firmware SHA-256 mismatch',
    'GetEmbeddedFirmwareSource',
    'GENERATED FILE - DO NOT EDIT OR COMMIT'
)) {
    if($generator.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "H7_GENERATOR_GUARD_MISSING: $required"
    }
}

if($project.IndexOf('<ClCompile Include="firmware_source.cpp" />',[StringComparison]::Ordinal) -lt 0) {
    throw 'H7_WDK_SOURCE_CONTRACT_NOT_COMPILED'
}
if($project.IndexOf('<ConfigurationType>StaticLibrary</ConfigurationType>',
                    [StringComparison]::Ordinal) -lt 0) {
    throw 'H7_PROJECT_MUST_REMAIN_STATIC_LIBRARY'
}

# Generated firmware/provider bytes must not be committed. Restrict the scan to
# git-tracked files so compiler/CMake scratch .bin files cannot create false
# positives.
$tracked=@(& git -C $root ls-files)
if($LASTEXITCODE -ne 0) {
    throw 'H7_GIT_LS_FILES_FAILED'
}

$forbiddenTracked=@(
    $tracked | Where-Object {
        $_ -match '(?i)\.(ri|bin)$' -or
        $_ -match '(?i)embedded[_-]firmware[_-]generated\.(cpp|c|h)$'
    }
)
if($forbiddenTracked.Count -ne 0) {
    throw ('H7_FIRMWARE_BYTES_COMMITTED: ' + ($forbiddenTracked -join ', '))
}

if($workflow.IndexOf('INSTALLABLE=FALSE',[StringComparison]::Ordinal) -lt 0 -or
   $workflow.IndexOf('AUDIO_PLAYBACK=NOT_IMPLEMENTED',[StringComparison]::Ordinal) -lt 0) {
    throw 'H7_WORKFLOW_SAFETY_MARKERS_MISSING'
}

Write-Host 'H7_FIRMWARE_SOURCE_STATIC_TESTS=PASS; build_time_embedded=YES; runtime_file_io=NO; arbitrary_buffer_api=NO; deviceadd_autostage=NO; firmware_bytes_committed=NO; installable=NO; playback=NO'
