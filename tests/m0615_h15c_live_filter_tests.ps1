$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$src=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\h15c_live_filter.cpp') -Raw
$hdr=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\h15c_live_filter.h') -Raw
$inf=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\phaser360_h15c_live_filter.inf') -Raw
$proj=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\phaser360_h15c_live_filter.vcxproj') -Raw
$reader=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\Collect-H15cLive.ps1') -Raw
$pci=Get-Content -LiteralPath (Join-Path $root 'm062\driver\pci_config_attestation.cpp') -Raw

foreach($required in @(
 'WdfFdoInitSetFilter(deviceInit)',
 'PciConfigAttestation attestation',
 'WdfDeviceConfigureRequestDispatching',
 'WdfRequestTypeDeviceControl',
 'WdfRequestFormatRequestUsingCurrentType(request)',
 'WdfRequestSend(',
 'WdfDeviceGetIoTarget(device)',
 'queueConfig.PowerManaged=WdfFalse',
 'H15cLiveGetBusDataOnly',
 'H15cLiveNoPciWrite'
)){
 if(($src+$hdr).IndexOf($required,[StringComparison]::Ordinal) -lt 0){
  throw "H15C_LIVE_FILTER_REQUIRED_MISSING: $required"
 }
}
foreach($forbidden in @(
 'SetBusData(','READ_REGISTER_','WRITE_REGISTER_','MmMapIoSpace',
 'WdfInterruptCreate','WdfDmaEnablerCreate','WdfCommonBufferCreate',
 'GlkBoot','HdaController','BootStream'
)){
 if(($src+$hdr).IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0){
  throw "H15C_LIVE_FILTER_FORBIDDEN_API: $forbidden"
 }
}
if($pci.IndexOf('SetBusData(',[StringComparison]::Ordinal) -ge 0 -or
   $pci.IndexOf('.SetBusData(',[StringComparison]::Ordinal) -ge 0){
 throw 'H15C_LIVE_SHARED_ATTESTATION_WRITE_PATH_PRESENT'
}

foreach($required in @(
 'Class=Extension',
 'ClassGuid={e2f84ce7-8efa-411c-aa69-97454ca4cb57}',
 'ExtensionId={53f678f1-2b3c-4b2e-a15c-360031980001}',
 '%ProviderName%=H15cLive.Models,NTamd64.10.0...19045,NTamd64.10.0...19044',
 '[H15cLive.Models.NTamd64.10.0...19045]',
 '[H15cLive.Models.NTamd64.10.0...19044]',
 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06',
 'AddService=Phaser360H15cLive,0x00000000',
 'AddFilter=Phaser360H15cLive,,H15cLive_Filter',
 'FilterPosition=Upper',
 'KmdfLibraryVersion=1.31'
)){
 if($inf.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
  throw "H15C_LIVE_INF_REQUIRED_MISSING: $required"
 }
}
if($inf -match '(?im)^\s*[^;].*PCI\\VEN_8086&DEV_3198\s*$'){
 throw 'H15C_LIVE_BROAD_HWID_FORBIDDEN'
}
if($inf.IndexOf('SPSVCINST_ASSOCSERVICE',[StringComparison]::OrdinalIgnoreCase) -ge 0 -or
   $inf.IndexOf('AddService=Phaser360H15cLive,0x00000002',[StringComparison]::OrdinalIgnoreCase) -ge 0){
 throw 'H15C_LIVE_ASSOCIATED_FUNCTION_SERVICE_FORBIDDEN'
}

foreach($required in @('h15c_live_filter.cpp','pci_config_attestation.cpp','KMDF_VERSION_MINOR>31')){
 if($proj.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
  throw "H15C_LIVE_PROJECT_REQUIRED_MISSING: $required"
 }
}
foreach($forbidden in @('glk_boot.cpp','hda_transport.cpp','ipc_interrupt.cpp','boot_dma.cpp','firmware')){
 if($proj.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0){
  throw "H15C_LIVE_PROJECT_FORBIDDEN_COMPONENT: $forbidden"
 }
}

foreach($required in @(
 'DeviceIoControl(','0x00226004','SnapshotBytes=292',
 'pci_config_256.bin','OFFSET_44','OFFSET_48',
 'SetBusDataCalls=0','DRIVER_BIND_REPLACEMENT=NO'
)){
 if($reader.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
  throw "H15C_LIVE_READER_REQUIRED_MISSING: $required"
 }
}
foreach($forbidden in @(
 'pnputil /add-driver','pnputil.exe /add-driver','/restart-device',
 '/disable-device','/enable-device','sc.exe','reg.exe','Set-ItemProperty',
 'New-ItemProperty','Remove-ItemProperty','bcdedit','devcon'
)){
 if($reader.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0){
  throw "H15C_LIVE_READER_MUTATION_FORBIDDEN: $forbidden"
 }
}

Write-Host 'H15C_LIVE_STATIC_TESTS=PASS; role=EXACT_TARGET_EXTENSION_UPPER_FILTER; function_driver_replacement=NO; pci_access=GETBUSDATA_ONLY; setbusdata=FORBIDDEN; mmio=NONE; dma=NONE; irq=NONE; unknown_ioctl=FORWARDED; reader_install_actions=NONE'
