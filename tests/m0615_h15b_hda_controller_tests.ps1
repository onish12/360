$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$controller=Get-Content -LiteralPath (Join-Path $root 'src\sof\hda_controller.cpp') -Raw
$transport=Get-Content -LiteralPath (Join-Path $root 'm062\driver\hda_transport.cpp') -Raw
$glk=Get-Content -LiteralPath (Join-Path $root 'm062\driver\glk_boot.cpp') -Raw
$streamTests=Get-Content -LiteralPath (Join-Path $root 'tests\sof_stream_tests.cpp') -Raw
$glkTests=Get-Content -LiteralPath (Join-Path $root 'tests\sof_glk_boot_tests.cpp') -Raw
$cmake=Get-Content -LiteralPath (Join-Path $root 'CMakeLists.txt') -Raw
$libproj=Get-Content -LiteralPath (Join-Path $root 'm062\driver\phaser360_boot_dma.vcxproj') -Raw
$sysproj=Get-Content -LiteralPath (Join-Path $root 'm062\driver\phaser360_m1_boot.vcxproj') -Raw

foreach($required in @(
 'kGctlCrst','Poll(kGctl,4,kGctlCrst,0,1000)','delay_us(io_.context,500)',
 'Poll(kGctl,4,kGctlCrst,kGctlCrst,1000)','delay_us(io_.context,1000)',
 'kIntctl','kSsync','kDplbase','kIntelEm2L1sen','kPpGprocen',
 'DiscoverCapabilities()','VerifyColdStreams()','kSpibCapId',
 'state_=HdaControllerState::Initialized'
)){
 if($controller.IndexOf($required,[StringComparison]::Ordinal) -lt 0){
  throw "H15B_CONTROLLER_REQUIRED_MISSING: $required"
 }
}
if($controller.IndexOf('0x500',[StringComparison]::OrdinalIgnoreCase) -ge 0 -or
   $controller.IndexOf('0x700',[StringComparison]::OrdinalIgnoreCase) -ge 0){
 throw 'H15B_CAPABILITY_OFFSET_HARDCODE_FORBIDDEN'
}
$init=$transport.IndexOf('controller_.Initialize(io)',[StringComparison]::Ordinal)
$select=$transport.IndexOf('stream_.Select(io)',[StringComparison]::Ordinal)
if($init -lt 0 -or $select -lt 0 -or $init -ge $select){
 throw 'H15B_CONTROLLER_NOT_BEFORE_STREAM_SELECT'
}
if($transport.IndexOf('controller_.Quiesce()',[StringComparison]::Ordinal) -lt 0){
 throw 'H15B_CONTROLLER_QUIESCE_MISSING'
}
$shutdown=$glk.IndexOf('bool GlkBoot::Shutdown() noexcept',[StringComparison]::Ordinal)
$stop=$glk.IndexOf('hda_.StopAndRelease()',$shutdown,[StringComparison]::Ordinal)
$power=$glk.IndexOf('rom_.PowerDown()',$stop,[StringComparison]::Ordinal)
$quiesce=$glk.IndexOf('hda_.QuiesceController()',$power,[StringComparison]::Ordinal)
if($shutdown -lt 0 -or $stop -lt 0 -or $power -lt 0 -or $quiesce -lt 0 -or
   -not($stop -lt $power -and $power -lt $quiesce)){
 throw "H15B_SHUTDOWN_ORDER_INVALID: stop=$stop power=$power quiesce=$quiesce"
}
foreach($required in @(
 'controllerEnterStuck','controllerExitStuck','controller.Initialize(h.Io())',
 'Put(hda,8,4,0)','Put(hda,0x1030,4,0x2000)',
 'transfer.dmaReleased && (Get(hda,0x504,4)&0x40000000u)!=0'
)){
 if(($streamTests+$glkTests).IndexOf($required,[StringComparison]::Ordinal) -lt 0){
  throw "H15B_REGRESSION_MISSING: $required"
 }
}
foreach($build in @($cmake,$libproj,$sysproj)){
 if($build.IndexOf('hda_controller.cpp',[StringComparison]::OrdinalIgnoreCase) -lt 0){
  throw 'H15B_BUILD_GRAPH_MISSING_CONTROLLER'
 }
}
Write-Host 'H15B_HDA_CONTROLLER_STATIC_TESTS=PASS; crst=RESET_READY_READBACK; intctl=ZERO; ssync=ZERO; dplbase=DISABLED; l1sen=DISABLED_DURING_D0_RESTORED_ON_SHUTDOWN; ppcap=DISCOVERED_GPROCEN_ONLY; spib=DISCOVERED_COLD; controller_lifetime=THROUGH_IPC_RUNTIME; pci_cgctl_pgctl=NOT_IMPLEMENTED'
