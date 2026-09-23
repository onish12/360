$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content (Join-Path $root 'm062\h15k\h15k_adspcs_reset_gate.h') -Raw
$src=Get-Content (Join-Path $root 'm062\h15k\h15k_adspcs_reset_gate.cpp') -Raw
$proj=Get-Content (Join-Path $root 'm062\h15k\phaser360_h15k_adspcs_reset_gate.vcxproj') -Raw
$inf=Get-Content (Join-Path $root 'm062\h15k\phaser360_h15k_adspcs_reset_gate.inf') -Raw
$run=Get-Content (Join-Path $root 'm062\h15k\Run-H15kTransaction.ps1') -Raw
$doc=Get-Content (Join-Path $root 'docs\M0615_H15K_ADSPCS_RESET_GATE.md') -Raw
$wf=Get-Content (Join-Path $root '.github\workflows\h15k-adspcs-reset-package.yml') -Raw

foreach($x in @('0x833de474u','sizeof(H15kObservation)==48u','sizeof(H15kResultV1)==256u',
 'kH15kRequiredFlags=0x03ffffffu','H15kAdspcsRestoredExact','H15kNoSpaWrite')){
 if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15K_HDR_MISSING: $x"}
}
foreach($x in @('kCrst01=(kCore01<<0)','kCstall01=(kCore01<<8)',
 'kForbiddenSpaCpa01=(kCore01<<16)|(kCore01<<24)',
 'kAllowedAdspcsWriteMask=kCrst01|kCstall01',
 'kExpectedBeforeAdspcs=0x001d003cu','kExpectedStalledAdspcs=0x001d033cu',
 'kExpectedResetAdspcs=0x001d033fu','WriteAdspcsMasked(c,kCstall01,kCstall01)',
 'WriteAdspcsMasked(c,kCrst01,kCrst01)',
 'WriteAdspcsMasked(c,kCrst01,result.before.dspAdspcs&kCrst01)',
 'WriteAdspcsMasked(c,kCstall01,result.before.dspAdspcs&kCstall01)',
 'PAGE_READONLY|PAGE_NOCACHE','PAGE_READWRITE|PAGE_NOCACHE')){
 if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15K_SRC_MISSING: $x"}
}
if(([regex]::Matches($src,'WRITE_REGISTER_ULONG')).Count -ne 1){throw 'H15K_WRITE_REGISTER_COUNT_NOT_ONE_SOURCE_SITE'}
foreach($x in @('WRITE_REGISTER_UCHAR','WRITE_REGISTER_USHORT','SetBusData',
 'PciConfigBootPolicy','WdfInterruptCreate','WdfDma','WdfCommonBuffer',
 'PinnedFirmware','GlkBoot','ColdPower','HdaTransport')){
 if(($src+$hdr+$proj).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15K_FORBIDDEN: $x"}
}
foreach($x in @('AddService=Phaser360H15k,0x00000002',
 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06')){
 if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15K_INF_MISSING: $x"}
}
foreach($x in @("[Convert]::ToUInt32('833DE474',16)","[Convert]::ToUInt32('03FFFFFF',16)",
 '$ResultBytes=256','$stalledRegs=Obs $r 112','$resetRegs=Obs $r 160',
 '$restoredRegs=Obs $r 208','$stalledAdspcs=U32 $r 132',
 '$resetAdspcs=U32 $r 180','$restoredAdspcs=U32 $r 228',
 '$writeRestoreComplete=(($fl -band',
 'H15K_ADSPCS_STALL_RESET_AND_ROLLBACK_COMPLETE',
 "Mapping='HDA_PAGE_READONLY_DSP_PAGE_READWRITE'",
 "MmioWrite='ONLY_DSP_ADSPCS_CSTALL_CRST_CORES01_WITH_EXACT_ROLLBACK'",
 "SpaCpaWrite='NO'")){
 if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15K_RUNNER_MISSING: $x"}
}
foreach($x in @('bcdedit','/reboot','SetBusData')){if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15K_RUNNER_FORBIDDEN: $x"}}
foreach($x in @('SPA(core0)=1','CPA(core0)=0','0x001D033C','0x001D033F',
 'HDA BAR is PAGE_READONLY','does not authorize firmware')){
 if($doc.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15K_DOC_MISSING: $x"}
}
foreach($x in @('H15K_ADSPCS_STALL_RESET_TRANSACTION_PACKAGE',
 'PHASER360_H15K_ADSPCS_STALL_RESET_TRANSACTION_PACKAGE',
 "Mapping='HDA_PAGE_READONLY_DSP_PAGE_READWRITE'",
 "MmioWrite='ONLY_DSP_ADSPCS_CSTALL_CRST_CORES01_WITH_EXACT_ROLLBACK'",
 '-KeyExportPolicy NonExportable')){
 if($wf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15K_WORKFLOW_MISSING: $x"}
}
Write-Host 'H15K_STATIC_TESTS=PASS; adspcs_write=ONLY_CSTALL_CRST_CORE01; spa_cpa_write=NO; hda_mmio_write=NO; pci_write=NO; dma=NO; irq=NO; firmware=NO; dsp_boot=NO'
