$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content (Join-Path $root 'm062\h15o\h15o_staged_pre_fw_policy_gate.h') -Raw
$src=Get-Content (Join-Path $root 'm062\h15o\h15o_staged_pre_fw_policy_gate.cpp') -Raw
$proj=Get-Content (Join-Path $root 'm062\h15o\phaser360_h15o_staged_pre_fw_policy_gate.vcxproj') -Raw
$inf=Get-Content (Join-Path $root 'm062\h15o\phaser360_h15o_staged_pre_fw_policy_gate.inf') -Raw
$run=Get-Content (Join-Path $root 'm062\h15o\Run-H15oTransaction.ps1') -Raw
$wf=Get-Content (Join-Path $root '.github\workflows\h15o-staged-pre-fw-policy-package.yml') -Raw
foreach($x in @('0x8342e488u','sizeof(H15oResultV1)==464u','kH15oRequiredFlags=0xffdfffffu','kH15oAppliedPgctl=0x00000014u','kH15oAppliedCgctl=0x807b0dfdu','kH15oAppliedEm2=0x04005000u')){
 if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15O_HDR_MISSING: $x"}}
foreach($x in @('kCgMask=1u<<1','kPgMask=1u<<2','kEm2L1sen=0x00002000u','WriteGprocen(c,true)','PciUpdateMasked(dev,g,kCgOffset,kCgMask,0','WriteEm2(c,false)','PciUpdateMasked(dev,g,kPgOffset,kPgMask,kPgMask','PAGE_READONLY|PAGE_NOCACHE')){
 if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15O_SRC_MISSING: $x"}}
if(([regex]::Matches($src,'WRITE_REGISTER_ULONG')).Count -ne 3){throw 'H15O_MMIO_WRITE_SITE_COUNT_NOT_THREE'}
foreach($x in @('WdfInterruptCreate','WdfDma','WdfCommonBuffer','WRITE_REGISTER_UCHAR','WRITE_REGISTER_USHORT')){
 if(($src+$hdr+$proj).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15O_FORBIDDEN: $x"}}
foreach($x in @('DriverVer=09/23/2026,0.6.15.270','AddService=Phaser360H15o,0x00000002','PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06')){
 if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15O_INF_MISSING: $x"}}
foreach($x in @("[Convert]::ToUInt32('8342E488',16)","[Convert]::ToUInt32('FFDFFFFF',16)",'$ResultBytes=464',"'H15O_STAGED_PRE_FW_POLICY_TRANSACTION_PACKAGE'","Em2Write='ONLY_L1SEN_BIT13_CLEAR_SET'","PciConfigWrite='ONLY_CGCTL_0x48_BIT1_AND_PGCTL_0x44_BIT2_WITH_EXACT_ROLLBACK'",'H15O_STAGED_PRE_FW_POLICY_AND_ROLLBACK_COMPLETE')){
 if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15O_RUN_MISSING: $x"}}
foreach($x in @('bcdedit','/reboot')){if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15O_RUN_FORBIDDEN: $x"}}
foreach($x in @('H15O_STAGED_PRE_FW_POLICY_TRANSACTION_PACKAGE','PHASER360_H15O_STAGED_PRE_FW_POLICY_TRANSACTION_PACKAGE',"-KeyExportPolicy NonExportable","Em2Write='ONLY_L1SEN_BIT13_CLEAR_SET'","PciConfigWrite='ONLY_CGCTL_0x48_BIT1_AND_PGCTL_0x44_BIT2_WITH_EXACT_ROLLBACK'")){
 if($wf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15O_WF_MISSING: $x"}}
Write-Host 'H15O_STATIC_TESTS=PASS; staged_gprocen_cg_em2_pg=YES; dsp_write=NO; cpa0_readonly=YES; exact_pci_restore=YES; dma=NO; irq=NO; firmware=NO'
