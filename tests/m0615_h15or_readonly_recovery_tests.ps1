$ErrorActionPreference='Stop';Set-StrictMode -Version 2;$root=Join-Path $PSScriptRoot '..'
$h=Get-Content (Join-Path $root 'm062\h15or\h15or_readonly_recovery_probe.h') -Raw
$s=Get-Content (Join-Path $root 'm062\h15or\h15or_readonly_recovery_probe.cpp') -Raw
$p=Get-Content (Join-Path $root 'm062\h15or\phaser360_h15or_readonly_recovery_probe.vcxproj') -Raw
$r=Get-Content (Join-Path $root 'm062\h15or\Run-H15orRecovery.ps1') -Raw
foreach($x in @('0x8343e48cu','sizeof(H15orResultV1)==120u','H15orExactSafeForHandoff=1u<<9')){if($h.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15OR_HDR_MISSING:$x"}}
foreach($x in @('HalGetBusDataByOffset(PCIConfiguration','PAGE_READONLY|PAGE_NOCACHE','kHdaPhys=0x00000000CEEE0000ull','kDspPhys=0x00000000CEF00000ull','H15orExactSafeForHandoff')){if($s.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15OR_SRC_MISSING:$x"}}
foreach($x in @('WRITE_REGISTER_','SetBusData','WdfInterruptCreate','WdfDma','WdfCommonBuffer')){if(($s+$h+$p).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15OR_FORBIDDEN:$x"}}
foreach($x in @("'H15OR_READONLY_RECOVERY_PACKAGE'",'$ResultBytes=120',"H15OR_DIRTY_OR_NONBASELINE_STATE_DETECTED_H15O_RETAINED","'/delete-driver',$before.DriverInfPath,'/uninstall','/force'")){if($r.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15OR_RUN_MISSING:$x"}}
if($r.IndexOf('bcdedit',[StringComparison]::OrdinalIgnoreCase)-ge0 -or $r.IndexOf('/reboot',[StringComparison]::OrdinalIgnoreCase)-ge0){throw 'H15OR_RUN_FORBIDDEN'}
Write-Host 'H15OR_STATIC_TESTS=PASS; probe_mmio_write=NO; pci_write=NO; conditional_h15o_uninstall=YES'
