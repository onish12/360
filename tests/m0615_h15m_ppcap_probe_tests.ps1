$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content (Join-Path $root 'm062\h15m\h15m_hda_ppcap_probe.h') -Raw
$src=Get-Content (Join-Path $root 'm062\h15m\h15m_hda_ppcap_probe.cpp') -Raw
$proj=Get-Content (Join-Path $root 'm062\h15m\phaser360_h15m_hda_ppcap_probe.vcxproj') -Raw
$inf=Get-Content (Join-Path $root 'm062\h15m\phaser360_h15m_hda_ppcap_probe.inf') -Raw
$run=Get-Content (Join-Path $root 'm062\h15m\Run-H15mTransaction.ps1') -Raw
$cmd=Get-Content (Join-Path $root 'm062\h15m\RUN_H15M_TRANSACTION.cmd') -Raw
$doc=Get-Content (Join-Path $root 'docs\M0615_H15M_PPCAP_DISCOVERY.md') -Raw
$wf=Get-Content (Join-Path $root '.github\workflows\h15m-ppcap-discovery-package.yml') -Raw

foreach($x in @('0x8340e480u','sizeof(H15mObservation)==48u','sizeof(H15mResultV1)==328u',
 'kH15mRequiredFlags=0x07fffcffu','kH15mMaxCaps=12u','H15mNoPpctlWrite','H15mNoEm2Write')){
 if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15M_HDR_MISSING: $x"}
}
foreach($x in @('kHdaLlch=0x0014u','kPpCapId=3u','kPpctlOffset=0x04u','kPpstsOffset=0x08u',
 'WalkCaps(c,&r)','WriteGctlCrst(c,true)','WriteGctlCrst(c,false)','PAGE_READWRITE|PAGE_NOCACHE','PAGE_READONLY|PAGE_NOCACHE')){
 if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15M_SRC_MISSING: $x"}
}
if(([regex]::Matches($src,'WRITE_REGISTER_ULONG')).Count -ne 1){throw 'H15M_WRITE_REGISTER_SITE_COUNT'}
foreach($x in @('SetBusData','PciConfigBootPolicy','WdfInterruptCreate','WdfDma','WdfCommonBuffer','WRITE_REGISTER_UCHAR','WRITE_REGISTER_USHORT')){
 if(($src+$hdr+$proj).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15M_FORBIDDEN: $x"}
}
foreach($x in @('DriverVer=09/23/2026,0.6.15.250','AddService=Phaser360H15m,0x00000002','PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06')){
 if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15M_INF_MISSING: $x"}
}
foreach($x in @("[Convert]::ToUInt32('8340E480',16)","[Convert]::ToUInt32('07FFFCFF',16)",'$ResultBytes=328',
 "'H15M_PPCAP_DISCOVERY_TRANSACTION_PACKAGE'",'$m.PpctlWrite -cne ''NO''','$m.Em2Write -cne ''NO''',
 '$llch=U32 $r 208','$ppctl=U32 $r 220','$capCount=U32 $r 228','H15M_PPCAP_DISCOVERY_AND_GCTL_ROLLBACK_COMPLETE')){
 if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15M_RUN_MISSING: $x"}
}
foreach($x in @('bcdedit','/reboot','SetBusData')){if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15M_RUN_FORBIDDEN: $x"}}
foreach($x in @('PPCTL/PPSTS, DSP si PCI: doar citire','EM2 write, DMA, IRQ ownership, firmware si playback: NU')){
 if($cmd.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15M_CMD_MISSING: $x"}
}
foreach($x in @('PPCTL.GPROCEN','does not enable it','no PPCTL write')){if($doc.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15M_DOC_MISSING: $x"}}
foreach($x in @('H15M_PPCAP_DISCOVERY_TRANSACTION_PACKAGE','PHASER360_H15M_PPCAP_DISCOVERY_TRANSACTION_PACKAGE',"-KeyExportPolicy NonExportable","PpctlWrite='NO'","Em2Write='NO'")){
 if($wf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15M_WF_MISSING: $x"}
}
Write-Host 'H15M_STATIC_TESTS=PASS; only_gctl_crst_write=YES; ppcap_read=YES; ppctl_write=NO; em2_write=NO; dsp_write=NO; pci_write=NO; dma=NO; irq=NO; firmware=NO'
