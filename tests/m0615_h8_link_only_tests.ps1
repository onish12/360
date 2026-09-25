$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$root=Join-Path $PSScriptRoot '..'
$project=Get-Content -LiteralPath (Join-Path $root 'm062\driver\phaser360_m1_boot.vcxproj') -Raw
$owner=Get-Content -LiteralPath (Join-Path $root 'm062\driver\device_owner.cpp') -Raw
$entry=Get-Content -LiteralPath (Join-Path $root 'm062\driver\driver_entry.cpp') -Raw
$source=Get-Content -LiteralPath (Join-Path $root 'm062\driver\firmware_source.cpp') -Raw
$workflow=Get-Content -LiteralPath (Join-Path $root '.github\workflows\build-m062-dma.yml') -Raw

foreach($required in @(
    '<ConfigurationType>Driver</ConfigurationType>',
    '<DriverType>KMDF</DriverType>',
    '<PlatformToolset>WindowsKernelModeDriver10.0</PlatformToolset>',
    '<SignMode>Off</SignMode>',
    '<ClCompile Include="$(GeneratedFirmwareSource)"'
)) {
    if($project.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "H8_DRIVER_PROJECT_REQUIRED_MISSING: $required"
    }
}
if($project -match '(?i)<Inf\b') {
    throw 'H8_INF_ITEM_FORBIDDEN'
}

$install=$owner.IndexOf('pnp_.InstallLifecycle(lifecycle_.Ops())',[StringComparison]::Ordinal)
$stage=$owner.IndexOf('status=StageEmbeddedFirmware()',[StringComparison]::Ordinal)
$irq=$owner.IndexOf('lifecycle_.CreateInterruptShell(device_)',[StringComparison]::Ordinal)
if(-not($install -ge 0 -and $install -lt $stage -and $stage -lt $irq)) {
    throw "H8_OWNER_ORDER_INVALID: lifecycle=$install firmware=$stage irq=$irq"
}

foreach($forbidden in @(
    'ZwCreateFile','ZwReadFile','NtCreateFile','IoCreateFile',
    'WdfIoTargetOpen','WdfIoTargetSendReadSynchronously'
)) {
    if($owner.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0 -or
       $entry.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0 -or
       $source.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "H8_RUNTIME_FILE_IO_FORBIDDEN: $forbidden"
    }
}

foreach($audio in @(
    'MAX98357','DA7219','MX98357','DLGS7219',
    'WaveRT','AudioEngine','ACX','PortCls'
)) {
    if($entry.IndexOf($audio,[StringComparison]::OrdinalIgnoreCase) -ge 0 -or
       $owner.IndexOf($audio,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "H8_AUDIO_PATH_FORBIDDEN: $audio"
    }
}

foreach($required in @(
    'phaser360_m1_boot.vcxproj',
    'GeneratedFirmwareSource=',
    'H8_DRIVER_LINK=PASS',
    'Remove-Item -LiteralPath $sys -Force',
    'SYS_UPLOADED=FALSE',
    'INF=ABSENT',
    'AUDIO_PLAYBACK=NOT_IMPLEMENTED'
)) {
    if($workflow.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "H8_WORKFLOW_GUARD_MISSING: $required"
    }
}

# No workflow command may copy the linked SYS into the development artifact.
# H13 may copy the SYS into an ephemeral package directory for Inf2Cat.
if($workflow -match '(?im)Copy-Item[^\r\n]*phaser360_m1_boot\.sys[^\r\n]*_artifact_m062' -or
   $workflow -match '(?im)Copy-Item[^\r\n]*_artifact_m062[^\r\n]*phaser360_m1_boot\.sys') {
    throw 'H8_SYS_COPY_TO_ARTIFACT_FORBIDDEN'
}

Write-Host 'H8_LINK_ONLY_STATIC_TESTS=PASS; real_kmdf_project=YES; embedded_stage_before_irq=YES; runtime_file_io=NO; inf=ABSENT; sys_upload=NO; audio_path=NO; playback=NO'
