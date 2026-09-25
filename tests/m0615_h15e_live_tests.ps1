$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content -LiteralPath (Join-Path $root 'm062\h15e_live\h15e_live_filter.h') -Raw
$src=Get-Content -LiteralPath (Join-Path $root 'm062\h15e_live\h15e_live_filter.cpp') -Raw
$proj=Get-Content -LiteralPath (Join-Path $root 'm062\h15e_live\phaser360_h15e_live_filter.vcxproj') -Raw
$inf=Get-Content -LiteralPath (Join-Path $root 'm062\h15e_live\phaser360_h15e_live_filter.inf') -Raw
$doc=Get-Content -LiteralPath (Join-Path $root 'docs\M0615_H15E_LIVE_R0.md') -Raw

foreach($x in @(
 '0x8339645cu','kH15eHdaBytes=0x4000u','kH15eDspBytes=0x100000u',
 'kH15eHdaGcap=0x0000u','kH15eHdaVmin=0x0002u','kH15eHdaVmaj=0x0003u',
 'kH15eHdaGctl=0x0008u','kH15eIntelEm2=0x1030u',
 'kH15eDspAdspcs=0x0004u','kH15eDspAdspis=0x000cu',
 'kH15eDspHipci=0x0048u','kH15eDspHipcie=0x004cu',
 'kH15eDspRomStatus=0x80000u','kH15eRequiredSuccessFlags=0x7ffu',
 'sizeof(H15eLiveSnapshotV1)==104u'
)){if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15E_HDR_MISSING: $x"}}

foreach($x in @(
 'PciConfigAttestation attestation','WdfCmResourceListGetCount',
 'WdfCmResourceListGetDescriptor','CmResourceTypeMemory',
 'MmMapIoSpaceEx(addresses[0],lengths[0],PAGE_READONLY|PAGE_NOCACHE)',
 'MmMapIoSpaceEx(addresses[1],lengths[1],PAGE_READONLY|PAGE_NOCACHE)',
 'READ_REGISTER_USHORT','READ_REGISTER_UCHAR','READ_REGISTER_ULONG',
 'MmUnmapIoSpace(dsp,lengths[1])','MmUnmapIoSpace(hda,lengths[0])',
 'H15eMappingsReleased','WdfIoQueueDispatchParallel','PowerManaged=WdfFalse'
)){if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15E_SRC_MISSING: $x"}}

foreach($x in @(
 'WRITE_REGISTER_','PAGE_READWRITE','SetBusData','PciConfigBootPolicy',
 'WdfCommonBuffer','WdfDma','WdfInterruptCreate','firmware_source',
 'glk_boot','hda_transport','KeStallExecutionProcessor'
)){if(($src+$hdr+$proj).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15E_FORBIDDEN: $x"}}

foreach($x in @('pci_config_attestation.cpp','KMDF_VERSION_MINOR>31')){
 if($proj.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15E_PROJECT_MISSING: $x"}
}
foreach($x in @(
 'Class=Extension','FilterPosition=Upper','Phaser360H15eLive',
 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06','NTamd64.10.0...19044',
 'KmdfLibraryVersion=1.31'
)){if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15E_INF_MISSING: $x"}}
foreach($x in @('PAGE_READONLY | PAGE_NOCACHE','no WRITE_REGISTER_* path',
 'unmaps both ranges before returning','does not authorize M1 DSP boot')){
 if($doc.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15E_DOC_MISSING: $x"}
}

Write-Host 'H15E_LIVE_R0_STATIC_TESTS=PASS; mmio=READ_ONLY_TRANSIENT; mapping=PAGE_READONLY_NOCACHE; hda=0x4000; dsp=0x100000; pci_write=NO; mmio_write=NO; dma=NO; irq=NO; dsp_boot=NO; playback=NO'

$run=Get-Content -LiteralPath (Join-Path $root 'm062\h15e_live\Run-H15eLiveR0.ps1') -Raw
$wf=Get-Content -LiteralPath (Join-Path $root '.github\workflows\h15e-live-r0-one-shot-package.yml') -Raw
foreach($x in @(
 "[Convert]::ToUInt32('8339645C',16)","[Convert]::ToUInt32('000007FF',16)",
 "[Convert]::ToUInt32('00000010',16)","[Convert]::ToUInt32('807B0DFF',16)",
 'CreateFile H15E GENERIC_READ','DeviceIoControl H15E snapshot',
 'SnapshotBytes=104','H15E_LIVE_SNAPSHOT_VALIDATION_FAILED',
 'MmioRead=''REVIEWED_REGISTERS_ONLY''','MmioWrite=''NO''','PciConfigWrite=''NO''',
 'BASELINE_RESTORED','TRUST_RESTORED','PHASER_DIAGNOSTIC_FILTER_ALREADY_ATTACHED'
)){if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15E_RUNNER_MISSING: $x"}}
foreach($x in @('GENERIC_WRITE','bcdedit','/reboot')){
 if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15E_RUNNER_FORBIDDEN: $x"}
}
foreach($x in @(
 '-KeyExportPolicy NonExportable','H15E_LIVE_R0_TRANSIENT_READONLY_MMIO_PACKAGE',
 "Mapping='PAGE_READONLY_NOCACHE_TRANSIENT'","MmioWrite='NO'","PciWrite='NO'",
 'PrivateKeyExported=$false','PHASER360_H15E_LIVE_R0_TRANSIENT_READONLY_MMIO_PACKAGE',
 'retention-days: 3'
)){if($wf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15E_WORKFLOW_MISSING: $x"}}
foreach($x in @('Export-PfxCertificate','-KeyExportPolicy Exportable','bcdedit','/reboot')){
 if($wf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15E_WORKFLOW_FORBIDDEN: $x"}
}
