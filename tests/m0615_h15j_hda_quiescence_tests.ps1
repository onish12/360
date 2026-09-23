$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content (Join-Path $root 'm062\h15j\h15j_hda_crst_gate.h') -Raw
$src=Get-Content (Join-Path $root 'm062\h15j\h15j_hda_crst_gate.cpp') -Raw
$proj=Get-Content (Join-Path $root 'm062\h15j\phaser360_h15j_hda_crst_gate.vcxproj') -Raw
$inf=Get-Content (Join-Path $root 'm062\h15j\phaser360_h15j_hda_crst_gate.inf') -Raw
$run=Get-Content (Join-Path $root 'm062\h15j\Run-H15jTransaction.ps1') -Raw
$doc=Get-Content (Join-Path $root 'docs\M0615_H15J_HDA_QUIESCENCE.md') -Raw
$wf=Get-Content (Join-Path $root '.github\workflows\h15j-hda-quiescence-package.yml') -Raw

foreach($x in @('0x833de470u','sizeof(H15jObservation)==48u','sizeof(H15jResultV1)==208u','kH15jRequiredFlags=0x000fffffu','H15jHdaQuiescentObserved','H15jNoHdaNonGctlWrite')){
 if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15J_HDR_MISSING: $x"}
}
foreach($x in @('kCorbctl=0x004cu','kRirbctl=0x005cu','kStreamBase=0x0080u','kStreamStride=0x20u','kRunBit=0x2u','READ_REGISTER_UCHAR(c->hda+kCorbctl)','READ_REGISTER_UCHAR(c->hda+kRirbctl)','r->streamRunMask|=(1u<<i)','dsp+kDspAdspic','dsp+kDspHipcctl','HdaQuiescent(result.ready)','crstSetWritten && readyQuiescent','PAGE_READWRITE|PAGE_NOCACHE','PAGE_READONLY|PAGE_NOCACHE')){
 if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15J_SRC_MISSING: $x"}
}
if(([regex]::Matches($src,'WRITE_REGISTER_ULONG')).Count -ne 1){throw 'H15J_WRITE_REGISTER_COUNT_NOT_ONE_SOURCE_SITE'}
foreach($x in @('WRITE_REGISTER_UCHAR','WRITE_REGISTER_USHORT','SetBusData','PciConfigBootPolicy','WdfInterruptCreate','WdfDma','WdfCommonBuffer','PinnedFirmware','GlkBoot','ColdPower','HdaTransport')){
 if(($src+$hdr+$proj).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15J_FORBIDDEN: $x"}
}
foreach($x in @('AddService=Phaser360H15j,0x00000002','PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06')){
 if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15J_INF_MISSING: $x"}
}
foreach($x in @("[Convert]::ToUInt32('833DE470',16)","[Convert]::ToUInt32('000FFFFF',16)",'$ResultBytes=208','$readyRegs=Obs $r 112','$restoredRegs=Obs $r 160','$readyRunMask=U32 $r 124','$readyStreams -ne 13 -or $readyRunMask -ne 0','H15J_HDA_QUIESCENCE_AND_ROLLBACK_COMPLETE',"MmioRead='HDA_QUIESCENCE_AND_DSP_STATUS'","HdaQuiescence='CORB_RIRB_ALL_STREAM_RUN_ZERO_BEFORE_CRST_CLEAR'","HdaNonGctlWrite='NO'")){
 if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15J_RUNNER_MISSING: $x"}
}
foreach($x in @('bcdedit','/reboot','SetBusData')){if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15J_RUNNER_FORBIDDEN: $x"}}
foreach($x in @('H15J_HDA_QUIESCENCE_TRANSACTION_PACKAGE',"MmioRead='HDA_QUIESCENCE_AND_DSP_STATUS'","HdaQuiescence='CORB_RIRB_ALL_STREAM_RUN_ZERO_BEFORE_CRST_CLEAR'","DspStatusRead='ADSPCS_ADSPIC_ADSPIS_HIPCI_HIPCIE_HIPCCTL_ROM'",'PHASER360_H15J_HDA_QUIESCENCE_TRANSACTION_PACKAGE','-KeyExportPolicy NonExportable')){
 if($wf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15J_WORKFLOW_MISSING: $x"}
}
foreach($x in @('CORBCTL.RUN','RIRBCTL.RUN','all stream RUN','ADSPIS 0x00040000','only HDA GCTL')){if($doc.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15J_DOC_MISSING: $x"}}
Write-Host 'H15J_STATIC_TESTS=PASS; hda_quiescence=REQUIRED; hda_gctl=ONLY_MMIO_WRITE; dsp_mmio_write=NO; pci_write=NO; dma=NO; irq=NO; firmware=NO; dsp_boot=NO'
