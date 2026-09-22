#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$PackageRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
. (Join-Path $PSScriptRoot 'H15cLive-Common.ps1')

$ExpectedSubject='CN=PHASER360 H15C-LIVE R2 Ephemeral Test Signing'
$CodeSigningOid='1.3.6.1.5.5.7.3.3'

if(-not [Environment]::Is64BitProcess){throw 'WINDOWS_X64_PROCESS_REQUIRED'}
$target=Get-H15cTargetState
Assert-H15cExactHealthyIntelTarget $target

$root=(Resolve-Path -LiteralPath $PackageRoot -ErrorAction Stop).Path
$inf=Join-Path $root 'phaser360_h15c_live_filter.inf'
$sys=Join-Path $root 'phaser360_h15c_live_filter.sys'
$cat=Join-Path $root 'phaser360_h15c_live_filter.cat'
$cer=Join-Path $root 'phaser360_h15c_live_filter.cer'
$manifestPath=Join-Path $root 'package_manifest.json'
foreach($p in @($inf,$sys,$cat,$cer,$manifestPath)){
    if(-not (Test-Path -LiteralPath $p -PathType Leaf)){throw "PACKAGE_FILE_MISSING: $p"}
}

$cert=Get-PfxCertificate -FilePath $cer
if(-not $cert){throw 'CERTIFICATE_PARSE_FAILED'}
if($cert.HasPrivateKey){throw 'PUBLIC_CER_HAS_PRIVATE_KEY'}
if($cert.Subject -cne $ExpectedSubject -or $cert.Issuer -cne $ExpectedSubject){
    throw "CERTIFICATE_IDENTITY_INVALID: $($cert.Subject)"
}
$now=[DateTime]::UtcNow
if($cert.NotBefore.ToUniversalTime() -gt $now -or $cert.NotAfter.ToUniversalTime() -le $now){
    throw 'CERTIFICATE_NOT_CURRENTLY_VALID'
}
$eku=New-Object System.Collections.Generic.List[string]
foreach($extension in $cert.Extensions){
    if($extension.Oid.Value -eq '2.5.29.37'){
        $typed=[System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]$extension
        foreach($oid in $typed.EnhancedKeyUsages){$eku.Add([string]$oid.Value)}
    }
}
if(@($eku|Where-Object {$_ -ceq $CodeSigningOid}).Count -ne 1){
    throw 'CODE_SIGNING_EKU_MISSING'
}

$manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
if([string]$manifest.CertificateThumbprint -cne [string]$cert.Thumbprint){
    throw 'MANIFEST_CERTIFICATE_THUMBPRINT_MISMATCH'
}
foreach($entry in @(
    @('INF',$inf,[string]$manifest.InfSha256),
    @('SYS',$sys,[string]$manifest.SysSha256),
    @('CAT',$cat,[string]$manifest.CatSha256),
    @('CER',$cer,[string]$manifest.CerSha256)
)){
    $actual=(Get-FileHash -LiteralPath $entry[1] -Algorithm SHA256).Hash.ToLowerInvariant()
    if($actual -cne $entry[2].ToLowerInvariant()){throw "MANIFEST_HASH_MISMATCH: $($entry[0])"}
}

$sysSig=Get-AuthenticodeSignature -LiteralPath $sys
$catSig=Get-AuthenticodeSignature -LiteralPath $cat
foreach($pair in @(@('SYS',$sysSig),@('CAT',$catSig))){
    if(-not $pair[1].SignerCertificate){throw "$($pair[0])_SIGNER_MISSING"}
    if([string]$pair[1].SignerCertificate.Thumbprint -cne [string]$cert.Thumbprint){
        throw "$($pair[0])_SIGNER_CERT_MISMATCH"
    }
    if($pair[1].Status -eq 'HashMismatch' -or $pair[1].Status -eq 'NotSigned'){
        throw "$($pair[0])_SIGNATURE_DAMAGED: $($pair[1].Status)"
    }
}

$thumb=[string]$cert.Thumbprint
$rootTrusted=Test-Path -LiteralPath ("Cert:\LocalMachine\Root\{0}" -f $thumb)
$publisherTrusted=Test-Path -LiteralPath ("Cert:\LocalMachine\TrustedPublisher\{0}" -f $thumb)
$trusted=$rootTrusted -and $publisherTrusted
$validAfterTrust=($sysSig.Status -eq 'Valid' -and $catSig.Status -eq 'Valid')
if($trusted){
    $sysSig=Get-AuthenticodeSignature -LiteralPath $sys
    $catSig=Get-AuthenticodeSignature -LiteralPath $cat
    $validAfterTrust=($sysSig.Status -eq 'Valid' -and $catSig.Status -eq 'Valid')
}

$status=if($trusted -and $validAfterTrust){'H15C_LIVE_R2_PACKAGE_TRUST_READY'}else{'H15C_LIVE_R2_PACKAGE_TRUST_BLOCKED'}
Write-Host "STATUS=$status"
Write-Host "SOURCE_COMMIT=$([string]$manifest.SourceCommit)"
Write-Host "CERT_THUMBPRINT=$thumb"
Write-Host "CERT_NOT_AFTER_UTC=$($cert.NotAfter.ToUniversalTime().ToString('o'))"
Write-Host "MACHINE_ROOT_PRESENT=$($rootTrusted.ToString().ToUpperInvariant())"
Write-Host "MACHINE_TRUSTEDPUBLISHER_PRESENT=$($publisherTrusted.ToString().ToUpperInvariant())"
Write-Host "SYS_SIGNATURE=$($sysSig.Status)"
Write-Host "CAT_SIGNATURE=$($catSig.Status)"
Write-Host "DRIVER_INSTALL=NO"
Write-Host "TRUST_CHANGE=NO"
Write-Host "BCD_WRITE=NO"
if($status -ne 'H15C_LIVE_R2_PACKAGE_TRUST_READY'){exit 2}
