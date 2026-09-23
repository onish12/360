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

$run=Get-Content (Join-Path $root 'm062\h15i\Run-H15iTransaction.ps1') -Raw
$wf=Get-Content (Join-Path $root '.github\workflows\h15i-hda-crst-package.yml') -Raw
foreach($x in @(
 'UpdateDriverForPlugAndPlayDevicesW','INSTALLFLAG_FORCE',
 "[Convert]::ToUInt32('833DE46C',16)",
 "[Convert]::ToUInt32('0003FFFF',16)",
 '$ResultBytes=160',
 '$ExpectedPg=[Convert]::ToUInt32(''00000010'',16)',
 '$ExpectedCg=[Convert]::ToUInt32(''807B0DFF'',16)',
 'H15I_LIVE_TRANSACTION_VALIDATION_FAILED',
 'H15I_HDA_CRST_AND_ROLLBACK_COMPLETE',
 "Mapping='HDA_PAGE_READWRITE_DSP_PAGE_READONLY'",
 "MmioWrite='ONLY_HDA_GCTL_CRST_BIT0_0_TO_1_TO_0'",
 "PciConfigWrite='NO'",
 'AUTOMATIC_UNINSTALL_BLOCKED_UNPROVEN_GCTL_RESTORE'
)){if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15I_RUNNER_MISSING: $x"}}
foreach($x in @('bcdedit','/reboot','SetBusData')){
 if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15I_RUNNER_FORBIDDEN: $x"}
}
foreach($x in @(
 '-KeyExportPolicy NonExportable',
 'H15I_HDA_CRST_TRANSACTION_PACKAGE',
 "Mapping='HDA_PAGE_READWRITE_DSP_PAGE_READONLY'",
 "MmioWrite='ONLY_HDA_GCTL_CRST_BIT0_0_TO_1_TO_0'",
 "PciConfigWrite='NO'",
 "ExpectedPgctl='0x00000010'","ExpectedCgctl='0x807B0DFF'",
 'PHASER360_H15I_HDA_CRST_TRANSACTION_PACKAGE'
)){if($wf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15I_WORKFLOW_MISSING: $x"}}
foreach($x in @('Export-PfxCertificate','-KeyExportPolicy Exportable','/reboot')){
 if($wf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15I_WORKFLOW_FORBIDDEN: $x"}
}
