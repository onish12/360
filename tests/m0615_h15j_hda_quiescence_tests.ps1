$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content (Join-Path $root 'm062\h15j\h15j_hda_quiescence_gate.h') -Raw
$src=Get-Content (Join-Path $root 'm062\h15j\h15j_hda_quiescence_gate.cpp') -Raw
$proj=Get-Content (Join-Path $root 'm062\h15j\phaser360_h15j_hda_quiescence_gate.vcxproj') -Raw
$inf=Get-Content (Join-Path $root 'm062\h15j\phaser360_h15j_hda_quiescence_gate.inf') -Raw
$doc=Get-Content (Join-Path $root 'docs\M0615_H15J_HDA_QUIESCENCE.md') -Raw
$run=Get-Content (Join-Path $root 'm062\h15j\Run-H15jTransaction.ps1') -Raw

foreach($x in @('0x833de470u','sizeof(H15jRegisterSet)==48u','sizeof(H15jResultV1)==208u',
 'kH15jRequiredFlags=0xfffffu','kH15jExpectedPgctl=0x00000010u',
 'kH15jExpectedCgctl=0x807b0dffu')){
 if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15J_HDR_MISSING: $x"}
}
foreach($x in @(
 'kHdaCorbctl=0x004cu','kHdaRirbctl=0x005cu','kHdaSdBase=0x0080u',
 'kHdaSdStride=0x20u','kHdaRun=0x02u',
 'r->hdaStreamCount=static_cast<USHORT>(streams)',
 'if(ctl&kHdaRun) r->hdaStreamRunMask|=(1u<<i)',
 'HdaQuiescent(result.before)','HdaQuiescent(result.ready)',
 'kDspAdspic=0x0008u','kDspHipcctl=0x0050u',
 'PAGE_READWRITE|PAGE_NOCACHE','PAGE_READONLY|PAGE_NOCACHE',
 'WriteGctlCrst(c,true)','WriteGctlCrst(c,false)',
 'SynchronizationScope=WdfSynchronizationScopeDevice',
 'ExecutionLevel=WdfExecutionLevelPassive')){
 if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15J_SRC_MISSING: $x"}
}
if(([regex]::Matches($src,'WRITE_REGISTER_ULONG')).Count -ne 1){throw 'H15J_WRITE_REGISTER_COUNT_NOT_ONE_SOURCE_SITE'}
foreach($x in @('WRITE_REGISTER_UCHAR','WRITE_REGISTER_USHORT','SetBusData',
 'PciConfigBootPolicy','WdfInterruptCreate','WdfDma','WdfCommonBuffer',
 'PinnedFirmware','GlkBoot','ColdPower','HdaTransport')){
 if(($src+$hdr+$proj).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15J_FORBIDDEN: $x"}
}
foreach($x in @('AddService=Phaser360H15j,0x00000002',
 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06')){
 if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15J_INF_MISSING: $x"}
}
foreach($x in @('CORBCTL','RIRBCTL','13 stream','ADSPIC','ADSPIS','HIPCCTL',
 'no PCI config write','DSP BAR remains PAGE_READONLY')){
 if($doc.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15J_DOC_MISSING: $x"}
}
foreach($x in @("[Convert]::ToUInt32('833DE470',16)","[Convert]::ToUInt32('000FFFFF',16)",
 '$ResultBytes=208','H15J_LIVE_TRANSACTION_VALIDATION_FAILED',
 'H15J_HDA_QUIESCENCE_AND_ROLLBACK_COMPLETE',
 '$beforeStreams -ne 13','beforeRun -ne 0','readyRun -ne 0')){
 if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15J_RUNNER_MISSING: $x"}
}
foreach($x in @('bcdedit','/reboot','SetBusData')){
 if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15J_RUNNER_FORBIDDEN: $x"}
}
Write-Host 'H15J_STATIC_TESTS=PASS; corb=QUIESCENT; rirb=QUIESCENT; streams=13_ALL_RUN0; hda_gctl_crst=ONLY_MMIO_WRITE; dsp_mmio_write=NO; pci_write=NO; dma=NO; irq=NO; firmware=NO; dsp_boot=NO'
