$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$root=Join-Path $PSScriptRoot '..'
$telemetry=Get-Content -LiteralPath (Join-Path $root 'm062\driver\telemetry.h') -Raw
$ioh=Get-Content -LiteralPath (Join-Path $root 'm062\driver\telemetry_ioctl.h') -Raw
$ioc=Get-Content -LiteralPath (Join-Path $root 'm062\driver\telemetry_ioctl.cpp') -Raw
$owner=Get-Content -LiteralPath (Join-Path $root 'm062\driver\device_owner.cpp') -Raw
$staticProject=Get-Content -LiteralPath (Join-Path $root 'm062\driver\phaser360_boot_dma.vcxproj') -Raw
$driverProject=Get-Content -LiteralPath (Join-Path $root 'm062\driver\phaser360_m1_boot.vcxproj') -Raw
$workflow=Get-Content -LiteralPath (Join-Path $root '.github\workflows\build-m062-dma.yml') -Raw

foreach($required in @(
    'TelemetrySnapshotV1',
    'static_assert(sizeof(TelemetrySnapshotV1)==32u',
    'TelemetryFirmwareLoaded',
    'TelemetryResourcesPrepared',
    'TelemetryD0Active',
    'TelemetryRemoved',
    'sessionGeneration',
    'completedD0',
    'failedD0',
    'lastD0Status'
)) {
    if($telemetry.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "H11_TELEMETRY_ABI_MISSING: $required"
    }
}

foreach($forbidden in @(
    'PHYSICAL_ADDRESS','ULONG_PTR','UCHAR*','void*','WDF',
    'bar','vector','affinity','register','mmio'
)) {
    if($telemetry.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "H11_TELEMETRY_HARDWARE_FIELD_FORBIDDEN: $forbidden"
    }
}

foreach($required in @(
    'IOCTL_PHASER360_QUERY_STATUS=0x00226000u',
    'FILE_DEVICE_UNKNOWN, function 0x800, METHOD_BUFFERED, FILE_READ_ACCESS',
    'CreateTelemetryEndpoint',
    'Phaser360EvtTelemetryIoctl'
)) {
    if($ioh.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "H11_IOCTL_ABI_MISSING: $required"
    }
}

foreach($required in @(
    'WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&config,WdfIoQueueDispatchSequential)',
    'config.PowerManaged=WdfFalse',
    'config.EvtIoDeviceControl=Phaser360EvtTelemetryIoctl',
    'WdfIoQueueCreate',
    'WdfDeviceCreateDeviceInterface',
    'ioControlCode!=IOCTL_PHASER360_QUERY_STATUS',
    'inputBufferLength!=0',
    'WdfRequestRetrieveOutputBuffer',
    'owner->QueryTelemetry(out)',
    'WdfRequestCompleteWithInformation'
)) {
    if($ioc.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "H11_IOCTL_GUARD_MISSING: $required"
    }
}

foreach($forbidden in @(
    'READ_REGISTER_','WRITE_REGISTER_','MmMapIoSpace','MmUnmapIoSpace',
    'WdfInterruptSynchronize','WdfDeviceStopIdle','WdfDeviceResumeIdle',
    'WdfDeviceStopIdleActual','WdfDeviceResumeIdleActual',
    'WdfRequestRetrieveInputBuffer','WdfMemoryGetBuffer',
    'GlkBoot','ColdPower','IpcInterrupt','Command('
)) {
    if($ioc.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0 -or
       $telemetry.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "H11_QUERY_HARDWARE_ACTION_FORBIDDEN: $forbidden"
    }
}

$stage=$owner.IndexOf('status=StageEmbeddedFirmware()',[StringComparison]::Ordinal)
$irq=$owner.IndexOf('status=lifecycle_.CreateInterruptShell(device_)',[StringComparison]::Ordinal)
$endpoint=$owner.IndexOf('status=CreateTelemetryEndpoint(device_)',[StringComparison]::Ordinal)
if(-not($stage -ge 0 -and $stage -lt $irq -and $irq -lt $endpoint)) {
    throw "H11_OWNER_ORDER_INVALID: firmware=$stage irq=$irq endpoint=$endpoint"
}
if($owner.IndexOf('telemetry_.SetFlag(TelemetryFirmwareLoaded,true)',
                  [StringComparison]::Ordinal) -lt 0) {
    throw 'H11_FIRMWARE_TELEMETRY_SET_MISSING'
}

foreach($projectText in @($staticProject,$driverProject)) {
    if($projectText.IndexOf('<ClCompile Include="telemetry_ioctl.cpp" />',
                            [StringComparison]::Ordinal) -lt 0) {
        throw 'H11_WDK_TELEMETRY_SOURCE_NOT_COMPILED'
    }
}

foreach($required in @(
    'phaser360_telemetry_tests',
    "sof_(pnp_resources|pinned_owner|telemetry|cng_pin",
    'H11_READONLY_TELEMETRY_STATIC_TESTS=PASS',
    'OBSERVABILITY=READ_ONLY_NON_POWER_MANAGED_IOCTL_SOFTWARE_MIRROR',
    'SYS_UPLOADED=FALSE',
    'INF=ABSENT',
    'AUDIO_PLAYBACK=NOT_IMPLEMENTED'
)) {
    if($workflow.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "H11_WORKFLOW_GUARD_MISSING: $required"
    }
}

$artifactSysCopies=@(
    $workflow -split "\r?\n" | Where-Object {
        $_ -match '(?i)Copy-Item' -and
        $_ -match '(?i)_artifact_m062' -and
        $_ -match '(?i)phaser360_m1_boot\.sys'
    }
)
if($artifactSysCopies.Count -ne 0) {
    throw 'H11_SYS_COPY_TO_ARTIFACT_FORBIDDEN'
}

Write-Host 'H11_READONLY_TELEMETRY_STATIC_TESTS=PASS; queue_power_managed=FALSE; method=BUFFERED; access=READ; input=NONE; software_mirror=ONLY; hardware_reads=NO; hardware_writes=NO; d0_trigger=NO; sys_upload=NO; inf=ABSENT; playback=NO'
