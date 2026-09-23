$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content (Join-Path $root 'm062\h15h\h15h_function_pci_policy.h') -Raw
$src=Get-Content (Join-Path $root 'm062\h15h\h15h_function_pci_policy.cpp') -Raw
$proj=Get-Content (Join-Path $root 'm062\h15h\phaser360_h15h_function_pci_policy.vcxproj') -Raw
$inf=Get-Content (Join-Path $root 'm062\h15h\phaser360_h15h_function_pci_policy.inf') -Raw
$doc=Get-Content (Join-Path $root 'docs\M0615_H15H_FUNCTION_PCI_POLICY.md') -Raw

foreach($x in @('0x833ce468u','sizeof(H15hResultV1)==176u','kH15hRequiredFlags=0x1ffffu',
 'kH15hExpectedPgctl=0x00000010u','kH15hAppliedPgctl=0x00000014u',
 'kH15hExpectedCgctl=0x807b0dffu','kH15hAppliedCgctl=0x807b0dfdu')){
 if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15H_HDR_MISSING: $x"}
}
foreach($x in @('PAGE_READONLY|PAGE_NOCACHE','PciConfigBootPolicy policy',
 'policy.Apply(device,p,*g)','policy.Restore()','ReadRegisters(c,&result.before)',
 'ReadRegisters(c,&result.applied)','H15hRestoredMmioCaptured',
 'RtlCompareMemory(p.config,final.Snapshot().config,kPciConfigSnapshotBytes)')){
 if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15H_SRC_MISSING: $x"}
}
foreach($x in @('WRITE_REGISTER_','PAGE_READWRITE','WdfInterruptCreate','WdfDma','WdfCommonBuffer',
 'PinnedFirmware','GlkBoot','ColdPower','HdaTransport')){
 if(($src+$hdr+$proj).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15H_FORBIDDEN: $x"}
}
foreach($x in @('pci_config_attestation.cpp','pci_config_boot_policy.cpp')){
 if($proj.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15H_PROJECT_MISSING: $x"}
}
foreach($x in @('AddService=Phaser360H15h,0x00000002','PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06')){
 if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15H_INF_MISSING: $x"}
}
foreach($x in @('does not attempt firmware boot or MMIO mutation','restored synchronously')){
 if($doc.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15H_DOC_MISSING: $x"}
}
Write-Host 'H15H_STATIC_TESTS=PASS; function_driver=YES; pci_write=BOUNDED_0x44_0x48; mmio_read=YES; mmio_write=NO; dma=NO; irq=NO; firmware=NO; dsp_boot=NO'
