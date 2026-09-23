$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content (Join-Path $root 'm062\h15i\h15i_hda_crst_gate.h') -Raw
$src=Get-Content (Join-Path $root 'm062\h15i\h15i_hda_crst_gate.cpp') -Raw
$proj=Get-Content (Join-Path $root 'm062\h15i\phaser360_h15i_hda_crst_gate.vcxproj') -Raw
$inf=Get-Content (Join-Path $root 'm062\h15i\phaser360_h15i_hda_crst_gate.inf') -Raw
$doc=Get-Content (Join-Path $root 'docs\M0615_H15I_HDA_CRST_GATE.md') -Raw

foreach($x in @('0x833de46cu','sizeof(H15iResultV1)==160u','kH15iRequiredFlags=0x3ffffu',
 'kH15iExpectedPgctl=0x00000010u','kH15iExpectedCgctl=0x807b0dffu')){
 if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15I_HDR_MISSING: $x"}
}
foreach($x in @(
 'PAGE_READWRITE|PAGE_NOCACHE','PAGE_READONLY|PAGE_NOCACHE',
 'ExactKnownBaseline(result.before)','DelayUs(500)','DelayUs(1000)',
 'WriteGctlCrst(c,true)','PollGctlCrst(c,true)',
 'WriteGctlCrst(c,false)','PollGctlCrst(c,false)',
 'result.restored.hdaGctl==result.before.hdaGctl',
 'SynchronizationScope=WdfSynchronizationScopeDevice',
 'ExecutionLevel=WdfExecutionLevelPassive')){
 if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15I_SRC_MISSING: $x"}
}
if(([regex]::Matches($src,'WRITE_REGISTER_ULONG')).Count -ne 1){throw 'H15I_WRITE_REGISTER_COUNT_NOT_ONE_SOURCE_SITE'}
foreach($x in @('WRITE_REGISTER_UCHAR','WRITE_REGISTER_USHORT','SetBusData',
 'PciConfigBootPolicy','WdfInterruptCreate','WdfDma','WdfCommonBuffer',
 'PinnedFirmware','GlkBoot','ColdPower','HdaTransport')){
 if(($src+$hdr+$proj).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15I_FORBIDDEN: $x"}
}
foreach($x in @('pci_config_attestation.cpp')){
 if($proj.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15I_PROJECT_MISSING: $x"}
}
foreach($x in @('AddService=Phaser360H15i,0x00000002','PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06')){
 if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15I_INF_MISSING: $x"}
}
foreach($x in @('writes only GCTL bit 0 from 0 to 1','writes only GCTL bit 0 back to 0',
 'no DSP BAR write','no ADSPCS write')){
 if($doc.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15I_DOC_MISSING: $x"}
}
Write-Host 'H15I_STATIC_TESTS=PASS; hda_gctl_crst=ONLY_MMIO_WRITE; dsp_mmio_write=NO; pci_write=NO; dma=NO; irq=NO; firmware=NO; dsp_boot=NO'
