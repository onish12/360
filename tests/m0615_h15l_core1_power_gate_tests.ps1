$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content (Join-Path $root 'm062\h15l\h15l_core1_power_gate.h') -Raw
$src=Get-Content (Join-Path $root 'm062\h15l\h15l_core1_power_gate.cpp') -Raw
$proj=Get-Content (Join-Path $root 'm062\h15l\phaser360_h15l_core1_power_gate.vcxproj') -Raw
$inf=Get-Content (Join-Path $root 'm062\h15l\phaser360_h15l_core1_power_gate.inf') -Raw
$run=Get-Content (Join-Path $root 'm062\h15l\Run-H15lTransaction.ps1') -Raw
$cmd=Get-Content (Join-Path $root 'm062\h15l\RUN_H15L_TRANSACTION.cmd') -Raw
$doc=Get-Content (Join-Path $root 'docs\M0615_H15L_CORE1_POWER_GATE.md') -Raw
$wf=Get-Content (Join-Path $root '.github\workflows\h15l-core1-power-package.yml') -Raw

foreach($x in @(
 '0x833fe47cu','sizeof(H15lObservation)==48u','sizeof(H15lResultV1)==304u',
 'kH15lRequiredFlags=0xffffffffu','H15lNoCpaWrite','H15lNoCstallWrite',
 'H15lNoCrstWrite','H15lCore0Untouched','H15lWriteScopeEnforced',
 'H15lObservation requested'
)){
 if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_R2_HDR_MISSING: $x"}
}

foreach($x in @(
 'kSpa1=1u<<17,kCpa1=1u<<25',
 'kCore0Fields=(1u<<0)|(1u<<8)|(1u<<16)|(1u<<24)',
 'kAllowedWriteMask=kSpa1',
 'kBaseline=0x001d003cu',
 'WriteMasked(c,kSpa1,kSpa1)',
 'WriteMasked(c,kSpa1,0)',
 'Poll(c,kSpa1,kSpa1)',
 'Poll(c,kSpa1,0)',
 'Poll(c,kCpa1,kCpa1)',
 'Poll(c,kCpa1,0)',
 'kBaseline|kSpa1|kCpa1',
 'PAGE_READONLY|PAGE_NOCACHE',
 'PAGE_READWRITE|PAGE_NOCACHE',
 'r.version=2u'
)){
 if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_R2_SRC_MISSING: $x"}
}

if(([regex]::Matches($src,'WRITE_REGISTER_ULONG')).Count -ne 1){
 throw 'H15L_R2_WRITE_REGISTER_COUNT_NOT_ONE_SITE'
}
foreach($x in @(
 'kCstall1','kCrst1','WRITE_REGISTER_UCHAR','WRITE_REGISTER_USHORT',
 'SetBusData','WdfInterruptCreate','WdfDma','WdfCommonBuffer',
 'PinnedFirmware','GlkBoot','ColdPower'
)){
 if(($src+$hdr+$proj).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){
  throw "H15L_R2_FORBIDDEN: $x"
 }
}

foreach($x in @(
 'AddService=Phaser360H15l,0x00000002',
 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06',
 'DriverVer=09/23/2026,0.6.15.241'
)){
 if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_R2_INF_MISSING: $x"}
}

foreach($x in @(
 "[Convert]::ToUInt32('833FE47C',16)",
 "[Convert]::ToUInt32('FFFFFFFF',16)",
 '$ResultBytes=304',
 'Put32 $req 0 2',
 '$requestedRegs=Obs $r 112',
 '$requestedAdspcs=U32 $r 132',
 '$poweredAdspcs=U32 $r 180',
 '$depoweredAdspcs=U32 $r 228',
 '$restoredAdspcs=U32 $r 276',
 "'021F003C'",
 "'001D003C'",
 "MmioWrite='ONLY_DSP_ADSPCS_CORE1_SPA_WITH_CPA_READONLY_ROLLBACK'",
 'H15L_R2_CORE1_SPA_CPA_HANDSHAKE_AND_ROLLBACK_COMPLETE',
 'H15L_R2_LIVE_TRANSACTION_VALIDATION_FAILED'
)){
 if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_R2_RUNNER_MISSING: $x"}
}
foreach($x in @('bcdedit','/reboot','SetBusData')){
 if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15L_R2_RUNNER_FORBIDDEN: $x"}
}

foreach($x in @(
 'MMIO write: numai DSP ADSPCS SPA1',
 'CSTALL/CRST, Core0, CPA write',
 'Nu modifica BCD si nu reporneste Windows'
)){
 if($cmd.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_R2_CMD_MISSING: $x"}
}

foreach($x in @(
 'Flags=0xFFFFB01F','ADSPCS=0x001D003C','0x021F003C',
 'CPA1 is read-only','CSTALL and CRST are not written',
 'HDA BAR remains PAGE_READONLY'
)){
 if($doc.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_R2_DOC_MISSING: $x"}
}

foreach($x in @(
 'H15L_R2_CORE1_SPA_CPA_HANDSHAKE_TRANSACTION_PACKAGE',
 'PHASER360_H15L_R2_CORE1_SPA_CPA_HANDSHAKE_TRANSACTION_PACKAGE',
 "MmioWrite='ONLY_DSP_ADSPCS_CORE1_SPA_WITH_CPA_READONLY_ROLLBACK'",
 "Core0Write='NO'","CpaWrite='NO'",'-KeyExportPolicy NonExportable'
)){
 if($wf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_R2_WORKFLOW_MISSING: $x"}
}

Write-Host 'H15L_R2_STATIC_TESTS=PASS; spa1_write=YES; cstall_write=NO; crst_write=NO; cpa_write=NO; core0_write=NO; hda_mmio_write=NO; pci_write=NO; dma=NO; irq=NO; firmware=NO; dsp_boot=NO; playback=NO'
