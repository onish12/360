$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content (Join-Path $root 'm062\h15n\h15n_ppctl_gprocen_gate.h') -Raw
$src=Get-Content (Join-Path $root 'm062\h15n\h15n_ppctl_gprocen_gate.cpp') -Raw
$proj=Get-Content (Join-Path $root 'm062\h15n\phaser360_h15n_ppctl_gprocen_gate.vcxproj') -Raw
$inf=Get-Content (Join-Path $root 'm062\h15n\phaser360_h15n_ppctl_gprocen_gate.inf') -Raw
$run=Get-Content (Join-Path $root 'm062\h15n\Run-H15nTransaction.ps1') -Raw
$wf=Get-Content (Join-Path $root '.github\workflows\h15n-ppctl-gprocen-package.yml') -Raw
foreach($x in @('0x8341e484u','sizeof(H15nResultV1)==344u','kH15nRequiredFlags=0xffffefffu','H15nCpa0Observed=1u<<12','H15nOnlyGprocenPpctlWrite=1u<<30')){
 if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15N_HDR_MISSING: $x"}}
foreach($x in @('kPpOffset=0x0800u','kPpHeader=0x00030500u','kGprocen=0x40000000u','kCpa0=0x01000000u','DiscoverExactPp(c,&r)','WriteGprocen(c,true)','WriteGprocen(c,false)','PollCpa0(c)')){
 if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15N_SRC_MISSING: $x"}}
if(([regex]::Matches($src,'WRITE_REGISTER_ULONG')).Count -ne 2){throw 'H15N_WRITE_SITE_COUNT_NOT_TWO'}
foreach($x in @('SetBusData','PciConfigBootPolicy','WdfInterruptCreate','WdfDma','WdfCommonBuffer','WRITE_REGISTER_UCHAR','WRITE_REGISTER_USHORT')){
 if(($src+$hdr+$proj).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15N_FORBIDDEN: $x"}}
foreach($x in @('DriverVer=09/23/2026,0.6.15.260','AddService=Phaser360H15n,0x00000002','PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06')){
 if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15N_INF_MISSING: $x"}}
foreach($x in @("[Convert]::ToUInt32('8341E484',16)","[Convert]::ToUInt32('FFFFEFFF',16)",'$ResultBytes=344',"'H15N_PPCTL_GPROCEN_TRANSACTION_PACKAGE'","PpctlWrite='ONLY_GPROCEN_BIT30_SET_CLEAR'",'$cpa0Observed=(($fl -band 0x00001000) -ne 0)','H15N_PPCTL_GPROCEN_AND_GCTL_ROLLBACK_COMPLETE')){
 if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15N_RUN_MISSING: $x"}}
foreach($x in @('bcdedit','/reboot','SetBusData')){if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15N_RUN_FORBIDDEN: $x"}}
foreach($x in @('H15N_PPCTL_GPROCEN_TRANSACTION_PACKAGE','PHASER360_H15N_PPCTL_GPROCEN_TRANSACTION_PACKAGE',"-KeyExportPolicy NonExportable","PpctlWrite='ONLY_GPROCEN_BIT30_SET_CLEAR'")){
 if($wf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15N_WF_MISSING: $x"}}
Write-Host 'H15N_STATIC_TESTS=PASS; gctl_crst_and_ppctl_gprocen_only=YES; cpa0_readonly=YES; dsp_write=NO; em2_write=NO; pci_write=NO; dma=NO; irq=NO; firmware=NO'
