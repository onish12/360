$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content -LiteralPath (Join-Path $root 'm062\h15f\h15f_function_driver.h') -Raw
$src=Get-Content -LiteralPath (Join-Path $root 'm062\h15f\h15f_function_driver.cpp') -Raw
$proj=Get-Content -LiteralPath (Join-Path $root 'm062\h15f\phaser360_h15f_function_driver.vcxproj') -Raw
$inf=Get-Content -LiteralPath (Join-Path $root 'm062\h15f\phaser360_h15f_function_driver.inf') -Raw
$doc=Get-Content -LiteralPath (Join-Path $root 'docs\M0615_H15F_FUNCTION_DRIVER_HANDOFF.md') -Raw

foreach($x in @(
 '0x833a6460u','kH15fHdaBytes=0x4000u','kH15fDspBytes=0x100000u',
 'kH15fRequiredLiveFlags=0x3ffu','sizeof(H15fSnapshotV1)==96u'
)){if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15F_HDR_MISSING: $x"}}

foreach($x in @(
 'EvtDevicePrepareHardware=H15fEvtPrepareHardware',
 'EvtDeviceReleaseHardware=H15fEvtReleaseHardware',
 'EvtDeviceD0Entry=H15fEvtD0Entry','EvtDeviceD0Exit=H15fEvtD0Exit',
 'WdfCmResourceListGetCount','WdfCmResourceListGetDescriptor',
 'CmResourceTypeMemory','CmResourceTypeInterrupt',
 'memoryCount==2','interruptCount==1',
 'WdfIoQueueDispatchParallel','PowerManaged=WdfFalse',
 'WdfDeviceCreateDeviceInterface'
)){if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15F_SRC_MISSING: $x"}}

foreach($x in @(
 'WdfFdoInitSetFilter','MmMapIoSpace','MmUnmapIoSpace','READ_REGISTER_',
 'WRITE_REGISTER_','GetBusData','SetBusData','BUS_INTERFACE_STANDARD',
 'WdfInterruptCreate','WdfCommonBuffer','WdfDma',
 'firmware','GlkBoot','ColdPower','HdaTransport','KeStallExecutionProcessor'
)){if(($src+$hdr+$proj).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15F_FORBIDDEN: $x"}}

foreach($x in @('KMDF_VERSION_MINOR>31','h15f_function_driver.cpp')){
 if($proj.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15F_PROJECT_MISSING: $x"}
}
foreach($x in @(
 'Class=MEDIA','AddService=Phaser360H15f,0x00000002',
 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06',
 'NTamd64.10.0...19044','KmdfLibraryVersion=1.31'
)){if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15F_INF_MISSING: $x"}}
foreach($x in @(
 'UpdateDriverForPlugAndPlayDevicesW','INSTALLFLAG_FORCE',
 'exactly one translated interrupt resource','H15F does not authorize DSP boot'
)){if($doc.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15F_DOC_MISSING: $x"}}

Write-Host 'H15F_STATIC_TESTS=PASS; role=FUNCTION_DRIVER; pnp_prepare=YES; d0_entry=YES; resource_metadata=READ_ONLY; mmio=NONE; pci_write=NONE; dma=NONE; irq_ownership=NONE; firmware=NONE; playback=NO'
