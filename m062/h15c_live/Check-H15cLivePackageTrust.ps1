#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$PackageRoot)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
. (Join-Path $PSScriptRoot 'H15cLive-Common.ps1')

if(-not [Environment]::Is64BitProcess){throw 'WINDOWS_X64_PROCESS_REQUIRED'}
$target=Get-H15cTargetState
Assert-H15cExactHealthyIntelTarget $target

$package=Assert-H15cPackage $PackageRoot
$trust=Get-H15cCertificatePresence $package.CertificateThumbprint

$status=if(-not $trust.Root -and -not $trust.TrustedPublisher){
    'H15C_LIVE_R2_PACKAGE_READY_FOR_TRANSACTION'
}else{
    'H15C_LIVE_R2_PACKAGE_TRUST_PREEXISTS'
}

Write-Host "STATUS=$status"
Write-Host "SOURCE_COMMIT=$($package.SourceCommit)"
Write-Host "CERT_THUMBPRINT=$($package.CertificateThumbprint)"
Write-Host "CERT_NOT_AFTER_UTC=$($package.CertificateNotAfter)"
Write-Host "MACHINE_ROOT_PRESENT=$($trust.Root.ToString().ToUpperInvariant())"
Write-Host "MACHINE_TRUSTEDPUBLISHER_PRESENT=$($trust.TrustedPublisher.ToString().ToUpperInvariant())"
Write-Host "SYS_SIGNATURE=$($package.SysAuthenticode)"
Write-Host "CAT_SIGNATURE=$($package.CatAuthenticode)"
Write-Host "DRIVER_INSTALL=NO"
Write-Host "TRUST_CHANGE=NO"
Write-Host "BCD_WRITE=NO"
if($status -ne 'H15C_LIVE_R2_PACKAGE_READY_FOR_TRANSACTION'){exit 2}
