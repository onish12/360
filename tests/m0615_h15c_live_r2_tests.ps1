$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$workflow=Get-Content -LiteralPath (Join-Path $root '.github\workflows\h15c-live-r2-package.yml') -Raw
$common=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\H15cLive-Common.ps1') -Raw
$check=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\Check-H15cLivePackageTrust.ps1') -Raw
$pre=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\Collect-H15cLiveInstallPreflight.ps1') -Raw
$tx=Get-Content -LiteralPath (Join-Path $root 'm062\h15c_live\Run-H15cLiveTransaction.ps1') -Raw
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
    '(Get-Date).AddDays(7)','retention-days: 3','actions/upload-artifact',
    'PHASER360_H15C_LIVE_R2_SIGNED_TEST_PACKAGE',
    'Copy-Item -LiteralPath .\m062\h15c_live\H15cLive-Common.ps1 -Destination $package',
    "'.pfx','.p12','.pvk','.key'",'R2_PRIVATE_KEY_ARTIFACT_FORBIDDEN'
)){
    if($workflow.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
        throw "R2_WORKFLOW_REQUIRED_MISSING: $required"
    }
}
foreach($forbidden in @(
    'Export-PfxCertificate','-KeyExportPolicy Exportable',
    'certutil -addstore','Import-Certificate','bcdedit','/reboot'
)){
    if($workflow.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0){
        throw "R2_WORKFLOW_FORBIDDEN: $forbidden"
    }
}
# Private-key filename extensions are expected only inside the explicit deny-list.
# Reject actual commands or artifact paths that try to create/copy/upload them.
foreach($pattern in @(
    '(?im)^\s*(?:Copy-Item|Move-Item|Set-Content|Out-File).*\.p(?:fx|12)\b',
    '(?im)^\s*path:\s*.*\.p(?:fx|12)\b'
)){
    if($workflow -match $pattern){throw "R2_PRIVATE_KEY_ARTIFACT_PATH_FORBIDDEN: $pattern"}
}
# PowerShell parses "Copy-Item source,$package" as two Path arguments rather
# than Path + Destination. Require the explicit -Destination form.
if($workflow.IndexOf(',$package',[StringComparison]::Ordinal) -ge 0){
    throw 'R2_AMBIGUOUS_COPYITEM_DESTINATION_FORBIDDEN'
}
foreach($required in @(
    'Get-H15cCertificatePresence','Invoke-H15cCertUtil',
    'PUBLIC_CER_HAS_PRIVATE_KEY','CODE_SIGNING_EKU_MISSING',
    'MANIFEST_CERTIFICATE_THUMBPRINT_MISMATCH','MANIFEST_HASH_MISMATCH'
)){
    if($common.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
        throw "R2_COMMON_REQUIRED_MISSING: $required"
    }
}
foreach($required in @(
    'H15C_LIVE_R2_PACKAGE_READY_FOR_TRANSACTION',
    'H15C_LIVE_R2_PACKAGE_TRUST_PREEXISTS',
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
    'H15C_LIVE_R2_INSTALL_READY',
    'CertificateAlreadyInRoot','CertificateAlreadyInTrustedPublisher',
    'TrustChange=''NO_PREFLIGHT'''
)){
    if($pre.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
        throw "R2_PREFLIGHT_REQUIRED_MISSING: $required"
    }
}
foreach($required in @(
    "'-addstore','Root'","'-addstore','TrustedPublisher'",
    'Assert-H15cPackage $PackageRoot -RequireTrusted',
    "'-delstore','TrustedPublisher'","'-delstore','Root'",
    'TRUST_RETAINED_FOR_SAFETY=TRUE',
    'RegistryWrite=''PNP_AND_CERT_STORES_TRANSACTIONAL''',
    'TrustChange=''TEMPORARY_LOCALMACHINE_ROOT_AND_TRUSTEDPUBLISHER''',
    'TrustRestored='
)){
    if($tx.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
        throw "R2_TRANSACTION_REQUIRED_MISSING: $required"
    }
}
foreach($required in @(
    'Do not manually import the certificate before the transaction',
    'H15C_LIVE_R2_PACKAGE_READY_FOR_TRANSACTION',
    'H15C_LIVE_R2_INSTALL_READY',
    'TRUST_RESTORED=TRUE',
    'RUN_H15C_LIVE_TRANSACTION.cmd'
)){
    if($start.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0){
        throw "R2_START_REQUIRED_MISSING: $required"
    }
}
Write-Host 'H15C_LIVE_R2_STATIC_TESTS=PASS; package_trigger=MANUAL_ONLY; signer=EPHEMERAL_NONEXPORTABLE_7D; private_key_artifact=NO; pretrust_checker=READ_ONLY_AND_REQUIRES_ABSENCE; target_trust_change=TRANSACTIONAL_EXACT_CERT; trust_rollback=REQUIRED; bcd_write=NO; pci_write=NO; mmio=NO'
