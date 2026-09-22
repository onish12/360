$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$common=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\H15cLive-Common.ps1') -Raw
$pre=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\Collect-H15cLiveInstallPreflight.ps1') -Raw
$tx=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\Run-H15cLiveTransaction.ps1') -Raw
$rollback=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\H15C_LIVE_WINRE_ROLLBACK.txt') -Raw

foreach($required in @(
 'NtQuerySystemInformation(103',
 'TestSignAllowed=(($o -band 0x2) -ne 0)',
 'Confirm-SecureBootUEFI',
 'Get-H15cPublishedInf',
 'ExtensionId',
 'Get-AuthenticodeSignature',
 'SYS_SIGNATURE_NOT_VALID',
 'CAT_SIGNATURE_NOT_VALID',
 'IntcAudioBus'
)){
 if($common.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
  throw "H15C_LIVE_R1_COMMON_REQUIRED_MISSING: $required"
 }
}
foreach($required in @(
 'H15C_LIVE_INSTALL_READY','DriverInstall=''NO''','TrustChange=''NO''',
 'WinREStatus','TestSignAllowed'
)){
 if($pre.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
  throw "H15C_LIVE_R1_PREFLIGHT_REQUIRED_MISSING: $required"
 }
}
foreach($required in @(
 "'/export-driver'",
 "'/add-driver'","'/install'",
 "'/restart-device'",
 "'/delete-driver'","'/uninstall'","'/force'",
 'finally {',
 'emergency_rollback.txt',
 'BASELINE_IDENTITY_CHANGED_AFTER_ROLLBACK',
 'CompoundUpperFilters',
 'Collect-H15cLive.ps1',
 'SystemReboot=''NO''',
 'BcdWrite=''NO''',
 'TrustChange=''NO''',
 'PciConfigWrite=''NO'''
)){
 if($tx.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
  throw "H15C_LIVE_R1_TRANSACTION_REQUIRED_MISSING: $required"
 }
}
foreach($forbidden in @(
 '/reboot','bcdedit','Import-Certificate','certutil -addstore',
 'New-ItemProperty','Set-ItemProperty','Remove-ItemProperty',
 '/disable-device','/enable-device','Phaser360M1'
)){
 if(($pre+$tx+$common).IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0){
  throw "H15C_LIVE_R1_FORBIDDEN_MUTATION: $forbidden"
 }
}
foreach($required in @(
 'dism /Image:<WINDOWS_VOLUME>:\ /Remove-Driver /Driver:oemNN.inf',
 'Do not remove the Intel IntcAudioBus package'
)){
 if($rollback.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
  throw "H15C_LIVE_R1_WINRE_REQUIRED_MISSING: $required"
 }
}
Write-Host 'H15C_LIVE_R1_STATIC_TESTS=PASS; preflight=READ_ONLY; testsign=REQUIRED_ALREADY_ON; trust_change=NONE; baseline_export=YES; install=EXTENSION_ONLY; capture=READ_ONLY; rollback=FINALLY_PLUS_WINRE; reboot=NONE; pci_write=NONE; mmio=NONE'
