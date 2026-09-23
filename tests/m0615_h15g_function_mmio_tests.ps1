$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content -LiteralPath (Join-Path $root 'm062\h15g\h15g_function_mmio.h') -Raw
$src=Get-Content -LiteralPath (Join-Path $root 'm062\h15g\h15g_function_mmio.cpp') -Raw
$proj=Get-Content -LiteralPath (Join-Path $root 'm062\h15g\phaser360_h15g_function_mmio.vcxproj') -Raw
$inf=Get-Content -LiteralPath (Join-Path $root 'm062\h15g\phaser360_h15g_function_mmio.inf') -Raw
$doc=Get-Content -LiteralPath (Join-Path $root 'docs\M0615_H15G_FUNCTION_MMIO.md') -Raw

foreach($x in @(
 '0x833b6464u','kH15gRequiredLiveFlags=0x1fffu',
 'sizeof(H15gSnapshotV1)==120u','kH15gHdaBytes=0x4000u',
 'kH15gDspBytes=0x100000u'
)){if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15G_HDR_MISSING: $x"}}

foreach($x in @(
 'MmMapIoSpaceEx(addresses[0],lengths[0],PAGE_READONLY|PAGE_NOCACHE)',
 'MmMapIoSpaceEx(addresses[1],lengths[1],PAGE_READONLY|PAGE_NOCACHE)',
 'READ_REGISTER_USHORT','READ_REGISTER_UCHAR','READ_REGISTER_ULONG',
 'MmUnmapIoSpace(dsp,dspLength)','MmUnmapIoSpace(hda,hdaLength)',
 'EvtDevicePrepareHardware=H15gEvtPrepareHardware',
 'EvtDeviceD0Entry=H15gEvtD0Entry',
 'EvtDeviceReleaseHardware=H15gEvtReleaseHardware',
 'EvtDeviceSurpriseRemoval=H15gEvtSurpriseRemoval',
 'WdfIoQueueDispatchParallel','PowerManaged=WdfFalse'
)){if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15G_SRC_MISSING: $x"}}

foreach($x in @(
 'WRITE_REGISTER_','PAGE_READWRITE','GetBusData','SetBusData','BUS_INTERFACE_STANDARD',
 'WdfInterruptCreate','WdfCommonBuffer','WdfDma','firmware_source',
 'PinnedFirmware','GlkBoot','ColdPower','HdaTransport'
)){if(($src+$hdr+$proj).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15G_FORBIDDEN: $x"}}

foreach($x in @('Class=MEDIA','AddService=Phaser360H15g,0x00000002',
 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06','KmdfLibraryVersion=1.31')){
 if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15G_INF_MISSING: $x"}
}
foreach($x in @('PAGE_READONLY | PAGE_NOCACHE','retained through D0',
 'does not authorize any hardware mutation')){
 if($doc.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15G_DOC_MISSING: $x"}
}

Write-Host 'H15G_STATIC_TESTS=PASS; role=FUNCTION_DRIVER; mapping=READ_ONLY_THROUGH_D0; mmio_write=NO; pci_write=NO; dma=NO; irq_ownership=NO; firmware=NO; playback=NO'
