$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content (Join-Path $root 'm062\h15l\h15l_core1_power_gate.h') -Raw
$src=Get-Content (Join-Path $root 'm062\h15l\h15l_core1_power_gate.cpp') -Raw
$proj=Get-Content (Join-Path $root 'm062\h15l\phaser360_h15l_core1_power_gate.vcxproj') -Raw
$inf=Get-Content (Join-Path $root 'm062\h15l\phaser360_h15l_core1_power_gate.inf') -Raw
$run=Get-Content (Join-Path $root 'm062\h15l\Run-H15lTransaction.ps1') -Raw
$doc=Get-Content (Join-Path $root 'docs\M0615_H15L_CORE1_POWER_GATE.md') -Raw
$wf=Get-Content (Join-Path $root '.github\workflows\h15l-core1-power-package.yml') -Raw

foreach($x in @('0x833fe47cu','sizeof(H15lObservation)==48u','sizeof(H15lResultV1)==304u',
 'kH15lRequiredFlags=0xffffffffu','H15lNoCpaWrite','H15lCore0Untouched')){
 if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_HDR_MISSING: $x"}
}
foreach($x in @(
 'kCrst1=1u<<1','kCstall1=1u<<9','kSpa1=1u<<17','kCpa1=1u<<25',
 'kCore0Fields=(1u<<0)|(1u<<8)|(1u<<16)|(1u<<24)',
 'kAllowedWriteMask=kCrst1|kCstall1|kSpa1',
 'kBaseline=0x001d003cu','kReset=0x001d023eu',
 'WriteMasked(c,kSpa1,kSpa1)','WriteMasked(c,kSpa1,0)',
 'Poll(c,kCpa1,kCpa1)','Poll(c,kCpa1,0)',
 'WriteMasked(c,kCrst1,0)','WriteMasked(c,kCstall1,0)',
 'PAGE_READONLY|PAGE_NOCACHE','PAGE_READWRITE|PAGE_NOCACHE')){
 if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_SRC_MISSING: $x"}
}
if(([regex]::Matches($src,'WRITE_REGISTER_ULONG')).Count -ne 1){throw 'H15L_WRITE_REGISTER_COUNT_NOT_ONE_SITE'}
foreach($x in @('WRITE_REGISTER_UCHAR','WRITE_REGISTER_USHORT','SetBusData',
 'WdfInterruptCreate','WdfDma','WdfCommonBuffer','PinnedFirmware','GlkBoot','ColdPower')){
 if(($src+$hdr+$proj).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15L_FORBIDDEN: $x"}
}
foreach($x in @('AddService=Phaser360H15l,0x00000002','PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06')){
 if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_INF_MISSING: $x"}
}
foreach($x in @("[Convert]::ToUInt32('833FE47C',16)","[Convert]::ToUInt32('FFFFFFFF',16)",
 '$ResultBytes=304','$resetRegs=Obs $r 112','$poweredRegs=Obs $r 160',
 '$depoweredRegs=Obs $r 208','$restoredRegs=Obs $r 256',
 '$poweredAdspcs=U32 $r 180','$depoweredAdspcs=U32 $r 228','$restoredAdspcs=U32 $r 276',
 "'(?im)^\s*AddService\s*=\s*Phaser360H15l\s*,'",
 "MmioWrite='ONLY_DSP_ADSPCS_CORE1_CSTALL_CRST_SPA_WITH_CPA_READONLY_ROLLBACK'",
 "Core0Write='NO';CpaWrite='NO'",
 'H15L_CORE1_POWER_HANDSHAKE_AND_ROLLBACK_COMPLETE')){
 if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_RUNNER_MISSING: $x"}
}
foreach($x in @('bcdedit','/reboot','SetBusData')){if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15L_RUNNER_FORBIDDEN: $x"}}
foreach($x in @('core0: SPA=1, CPA=0','core1: SPA=0, CPA=0','0x001D023E',
 'CPA is never written','HDA BAR remains PAGE_READONLY')){
 if($doc.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_DOC_MISSING: $x"}
}
foreach($x in @('H15L_CORE1_POWER_HANDSHAKE_TRANSACTION_PACKAGE',
 'PHASER360_H15L_CORE1_POWER_HANDSHAKE_TRANSACTION_PACKAGE',
 "MmioWrite='ONLY_DSP_ADSPCS_CORE1_CSTALL_CRST_SPA_WITH_CPA_READONLY_ROLLBACK'",
 "Core0Write='NO'","CpaWrite='NO'",'-KeyExportPolicy NonExportable')){
 if($wf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_WORKFLOW_MISSING: $x"}
}
Write-Host 'H15L_STATIC_TESTS=PASS; core1_spa_write=YES; cpa_write=NO; core0_write=NO; hda_mmio_write=NO; pci_write=NO; dma=NO; irq=NO; firmware=NO; dsp_boot=NO'
