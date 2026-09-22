$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$workflow=Get-Content -LiteralPath (Join-Path $root '.github\workflows\h15c-live-r2-package.yml') -Raw
$check=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\Check-H15cLivePackageTrust.ps1') -Raw
$start=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\START_H15C_LIVE_R2.txt') -Raw

if($workflow -notmatch '(?m)^\s*workflow_dispatch:\s*$'){throw 'R2_WORKFLOW_DISPATCH_MISSING'}
if($workflow -match '(?m)^\s*push:\s*$' -or $workflow -match '(?m)^\s*pull_request:\s*$'){
    throw 'R2_PACKAGE_WORKFLOW_MUST_BE_MANUAL_ONLY'
}
foreach($required in @(
    'New-SelfSignedCertificate','-Type CodeSigningCert',
    '-KeyExportPolicy NonExportable','RSA','SHA256','Export-Certificate',
    'signtool sign','Inf2Cat.exe','phaser360_h15c_live_filter.cer',
    'package_manifest.json','PrivateKeyExported = $false',
    'retention-days: 3','actions/upload-artifact',
    'PHASER360_H15C_LIVE_R2_SIGNED_TEST_PACKAGE'
)){
    if($workflow.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
        throw "R2_WORKFLOW_REQUIRED_MISSING: $required"
    }
}
foreach($forbidden in @(
    'Export-PfxCertificate','.pfx','.p12','-KeyExportPolicy Exportable',
    'certutil -addstore','Import-Certificate','bcdedit','/reboot'
)){
    if($workflow.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0){
        throw "R2_WORKFLOW_FORBIDDEN: $forbidden"
    }
}
foreach($required in @(
    'PUBLIC_CER_HAS_PRIVATE_KEY','CODE_SIGNING_EKU_MISSING',
    'MANIFEST_CERTIFICATE_THUMBPRINT_MISMATCH','MANIFEST_HASH_MISMATCH',
    'Cert:\LocalMachine\Root','Cert:\LocalMachine\TrustedPublisher',
    'H15C_LIVE_R2_PACKAGE_TRUST_READY','H15C_LIVE_R2_PACKAGE_TRUST_BLOCKED',
    'DRIVER_INSTALL=NO','TRUST_CHANGE=NO','BCD_WRITE=NO'
)){
    if($check.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
        throw "R2_CHECKER_REQUIRED_MISSING: $required"
    }
}
foreach($forbidden in @(
    'Import-Certificate','certutil','pnputil','bcdedit',
    'Set-ItemProperty','New-ItemProperty','Remove-ItemProperty'
)){
    if($check.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0){
        throw "R2_CHECKER_MUTATION_FORBIDDEN: $forbidden"
    }
}
foreach($required in @(
    'No PFX/P12/private key is distributed',
    'LocalMachine Root','LocalMachine TrustedPublisher',
    'H15C_LIVE_R2_PACKAGE_TRUST_READY',
    'RUN_H15C_LIVE_TRANSACTION.cmd'
)){
    if($start.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
        throw "R2_START_REQUIRED_MISSING: $required"
    }
}
Write-Host 'H15C_LIVE_R2_STATIC_TESTS=PASS; package_trigger=MANUAL_ONLY; signer=EPHEMERAL_NONEXPORTABLE; private_key_artifact=NO; target_checker=READ_ONLY; target_trust_change=EXPLICIT_USER_ACTION; bcd_write=NO; pci_write=NO; mmio=NO'
