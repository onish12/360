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
 'RtlCompareMemory(p.config,final.Snapshot().config,kPciConfigSnapshotBytes)',
 'SynchronizationScope=WdfSynchronizationScopeDevice',
 'ExecutionLevel=WdfExecutionLevelPassive')){
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

$run=Get-Content (Join-Path $root 'm062\h15h\Run-H15hTransaction.ps1') -Raw
$wf=Get-Content (Join-Path $root '.github\workflows\h15h-function-pci-policy-package.yml') -Raw
foreach($x in @(
 'UpdateDriverForPlugAndPlayDevicesW','INSTALLFLAG_FORCE',
 "[Convert]::ToUInt32('833CE468',16)",
 "[Convert]::ToUInt32('0001FFFF',16)",
 '$ResultBytes=176',
 '$ExpectedPg=[Convert]::ToUInt32(''00000010'',16)',
 '$AppliedPg=[Convert]::ToUInt32(''00000014'',16)',
 '$ExpectedCg=[Convert]::ToUInt32(''807B0DFF'',16)',
 '$AppliedCg=[Convert]::ToUInt32(''807B0DFD'',16)',
 'H15H_LIVE_TRANSACTION_VALIDATION_FAILED',
 '$writeAttempted=$true','$writeRestoreComplete=$true',
 '(( -not $writeAttempted) -or $writeRestoreComplete)',
 'H15H_FUNCTION_PCI_POLICY_AND_ROLLBACK_COMPLETE',
 "PciConfigWrite='ONLY_0x44_BIT2_AND_0x48_BIT1_WITH_EXACT_RESTORE'"
)){if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15H_RUNNER_MISSING: $x"}}
foreach($x in @('bcdedit','/reboot','WRITE_REGISTER_','PAGE_READWRITE')){
 if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15H_RUNNER_FORBIDDEN: $x"}
}
foreach($x in @(
 '-KeyExportPolicy NonExportable',
 'H15H_FUNCTION_DRIVER_PCI_POLICY_PACKAGE',
 "Mapping='PAGE_READONLY_NOCACHE_THROUGH_D0'",
 "MmioWrite='NO'",
 "PciConfigWrite='ONLY_0x44_BIT2_AND_0x48_BIT1_WITH_EXACT_RESTORE'",
 "ExpectedPgctl='0x00000010'","AppliedPgctl='0x00000014'",
 "ExpectedCgctl='0x807B0DFF'","AppliedCgctl='0x807B0DFD'",
 'PHASER360_H15H_FUNCTION_DRIVER_PCI_POLICY_PACKAGE'
)){if($wf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15H_WORKFLOW_MISSING: $x"}}
foreach($x in @('Export-PfxCertificate','-KeyExportPolicy Exportable','/reboot')){
 if($wf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15H_WORKFLOW_FORBIDDEN: $x"}
}
