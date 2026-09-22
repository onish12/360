$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$common=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\H15cLive-Common.ps1') -Raw
$pre=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\Collect-H15cLiveInstallPreflight.ps1') -Raw
$tx=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\Run-H15cLiveTransaction.ps1') -Raw
$rollback=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\H15C_LIVE_WINRE_ROLLBACK.txt') -Raw

foreach($required in @(
 'Get-H15cCertificatePresence',
 'Invoke-H15cCertUtil',
 'Assert-H15cPackage([string]$PackageRoot,[switch]$RequireTrusted)',
 'SYS_SIGNER_CERT_MISMATCH','CAT_SIGNER_CERT_MISMATCH',
 'MANIFEST_HASH_MISMATCH',
 'phaser360_h15c_live_filter.cer',
 'package_manifest.json',
 'IntcAudioBus'
)){
 if($common.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
  throw "H15C_LIVE_R2_COMMON_REQUIRED_MISSING: $required"
 }
}
foreach($required in @(
 'H15C_LIVE_R2_INSTALL_READY',
 'CertificateAlreadyInRoot',
 'CertificateAlreadyInTrustedPublisher',
 'TrustChange=''NO_PREFLIGHT''',
 'DriverInstall=''NO'''
)){
 if($pre.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
  throw "H15C_LIVE_R2_PREFLIGHT_REQUIRED_MISSING: $required"
 }
}
foreach($required in @(
 "'/export-driver'",
 "'-addstore','Root'",
 "'-addstore','TrustedPublisher'",
 'Assert-H15cPackage $PackageRoot -RequireTrusted',
 "'/add-driver'","'/install'",
 "'/restart-device'",
 "'/delete-driver'","'/uninstall'","'/force'",
 "'-delstore','TrustedPublisher'",
 "'-delstore','Root'",
 'TRUST_RETAINED_FOR_SAFETY=TRUE',
 'safeToDropTrust',
 'CaptureCompleted=$captureCompleted',
 'TrustRestored=',
 'RegistryWrite=''PNP_AND_CERT_STORES_TRANSACTIONAL''',
 'TrustChange=''TEMPORARY_LOCALMACHINE_ROOT_AND_TRUSTEDPUBLISHER''',
 'PciConfigWrite=''NO''',
 'Mmio=''NO''',
 'DspBoot=''NO'''
)){
 if($tx.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
  throw "H15C_LIVE_R2_TRANSACTION_REQUIRED_MISSING: $required"
 }
}
foreach($forbidden in @(
 '/reboot','bcdedit','Import-Certificate',
 'New-ItemProperty','Set-ItemProperty','Remove-ItemProperty',
 '/disable-device','/enable-device','Phaser360M1'
)){
 if(($pre+$tx+$common).IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0){
  throw "H15C_LIVE_R2_FORBIDDEN_MUTATION: $forbidden"
 }
}
foreach($required in @(
 'dism /Image:<WINDOWS_VOLUME>:\ /Remove-Driver /Driver:oemNN.inf',
 'Do not remove the Intel IntcAudioBus package',
 'certutil -delstore TrustedPublisher <THUMBPRINT>',
 'certutil -delstore Root <THUMBPRINT>',
 'Never remove another certificate by subject-name wildcard'
)){
 if($rollback.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
  throw "H15C_LIVE_R2_WINRE_REQUIRED_MISSING: $required"
 }
}
Write-Host 'H15C_LIVE_R2_TRANSACTION_TESTS=PASS; preflight=READ_ONLY; baseline_export=YES; trust=TEMPORARY_EXACT_CERT; install=EXTENSION_ONLY; capture=READ_ONLY; rollback=DRIVER_THEN_TRUST; emergency=RETAIN_TRUST_IF_DRIVER_STATE_UNPROVEN; reboot=NONE; pci_write=NONE; mmio=NONE'
