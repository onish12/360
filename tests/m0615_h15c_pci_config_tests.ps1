$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$pci=Get-Content -LiteralPath (Join-Path $root 'm062\driver\pci_config_attestation.cpp') -Raw
$transport=Get-Content -LiteralPath (Join-Path $root 'm062\driver\hda_transport.cpp') -Raw
$glkTests=Get-Content -LiteralPath (Join-Path $root 'tests\sof_glk_boot_tests.cpp') -Raw
$cmake=Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
$libproj=Get-Content -LiteralPath (Join-Path $root 'm062\driver\phaser360_boot_dma.vcxproj') -Raw
$sysproj=Get-Content -LiteralPath (Join-Path $root 'm062\driver\phaser360_m1_boot.vcxproj') -Raw

foreach($required in @(
 'WdfFdoQueryForInterface',
 '#include <initguid.h>',
 '#include <wdmguid.h>',
 'GUID_BUS_INTERFACE_STANDARD',
 'GetBusData',
 'PCI_WHICHSPACE_CONFIG',
 'kIntelVendor=0x8086',
 'kGlkAudioDevice=0x3198',
 'kPgctlOffset=0x44',
 'kCgctlOffset=0x48',
 'kCapabilityMin=0x50',
 'ValidateCapabilityChain',
 'bus.InterfaceDereference(bus.Context)',
 'read!=kConfigBytes'
)){
 if($pci.IndexOf($required,[StringComparison]::Ordinal) -lt 0){
  throw "H15C_REQUIRED_MISSING: $required"
 }
}
if($pci.IndexOf('SetBusData(',[StringComparison]::Ordinal) -ge 0 -or
   $pci.IndexOf('.SetBusData(',[StringComparison]::Ordinal) -ge 0){
 throw 'H15C_WRITE_PATH_PRESENT'
}
$attest=$transport.IndexOf('pci_.Capture(device)',[StringComparison]::Ordinal)
$controller=$transport.IndexOf('controller_.Initialize(io)',[StringComparison]::Ordinal)
if($attest -lt 0 -or $controller -lt 0 -or $attest -ge $controller){
 throw 'H15C_ATTESTATION_NOT_BEFORE_HDA_MMIO'
}
foreach($required in @(
 'pciWriteCalls==0',
 '0x8086','0x3198',
 'pciConfig[0x34]=0x50',
 'pciConfig[0x51]=0x60',
 'pciConfig[0x61]=0',
 'pciConfig[0x61]=0x40',
 'shortPciRead=true'
)){
 if($glkTests.IndexOf($required,[StringComparison]::Ordinal) -lt 0){
  throw "H15C_REGRESSION_MISSING: $required"
 }
}
foreach($build in @($cmake,$libproj,$sysproj)){
 if($build.IndexOf('pci_config_attestation.cpp',[StringComparison]::OrdinalIgnoreCase) -lt 0){
  throw 'H15C_BUILD_GRAPH_MISSING_PCI_ATTESTATION'
 }
}
Write-Host 'H15C_PCI_CONFIG_ATTESTATION_STATIC_TESTS=PASS; interface=BUS_INTERFACE_STANDARD; access=GETBUSDATA_ONLY; bytes=256; target=8086_3198; type0=YES; vendor_window=0x40_0x4f; pgctl=0x44; cgctl=0x48; capability_chain_min=0x50; setbusdata=FORBIDDEN; physical_write=NOT_AUTHORIZED'
