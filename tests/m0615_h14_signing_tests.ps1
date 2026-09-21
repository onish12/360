$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$root=Join-Path $PSScriptRoot '..'
$workflowPath=Join-Path $root '.github\workflows\build-m062-dma.yml'
$workflow=Get-Content -LiteralPath $workflowPath -Raw
$packagesPath=Join-Path $root 'm062\packages.config'
$packages=Get-Content -LiteralPath $packagesPath -Raw

foreach($required in @(
    'New-SelfSignedCertificate',
    '-Type CodeSigningCert',
    '-KeyExportPolicy NonExportable',
    'Export-Certificate',
    "Cert:\CurrentUser\Root",
    "Cert:\CurrentUser\TrustedPublisher",
    'signtool.exe',
    ' sign ',
    '/fd SHA256',
    'Inf2Cat.exe',
    '/os:10_VB_X64',
    ' verify ',
    '/pa',
    'H14_SIGNING=PASS',
    'PRIVATE_KEY_EXPORTED=FALSE',
    'TARGET_TRUST_MODIFIED=FALSE',
    'H14_PACKAGE_UPLOADED=FALSE',
    'H14_INSTALL_EXECUTED=FALSE',
    'H14_TOOL_DISCOVERY_BEGIN',
    'H14_SIGNTOOL_PATH=',
    'H14_INF2CAT_PATH=',
    'H14_CERT_CREATE_BEGIN',
    'H14_CERT_TRUST_BEGIN',
    'H14_SYS_SIGN_BEGIN',
    'H14_INF2CAT_BEGIN',
    'H14_CAT_SIGN_BEGIN',
    'H14_VERIFY_BEGIN',
    'H14_CLEANUP_BEGIN',
    'H14_CLEANUP_END',
    'Remove-Item -LiteralPath $package -Recurse -Force'
)) {
    if($workflow.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "H14_WORKFLOW_REQUIRED_MISSING: $required"
    }
}

foreach($forbidden in @(
    'Export-PfxCertificate',
    'Cert:\LocalMachine\',
    'bcdedit',
    'TESTSIGNING ON',
    '/add-driver',
    '/install',
    '/delete-driver',
    '/restart-device',
    '/enable-device',
    '/disable-device'
)) {
    if($workflow.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "H14_WORKFLOW_FORBIDDEN_PRESENT: $forbidden"
    }
}

if($packages.IndexOf('Microsoft.Windows.SDK.BuildTools',[StringComparison]::OrdinalIgnoreCase) -lt 0 -or
   $packages.IndexOf('version="10.0.28000.2526"',[StringComparison]::OrdinalIgnoreCase) -lt 0) {
    throw 'H14_PINNED_BUILD_TOOLS_PACKAGE_MISSING'
}
if($workflow.IndexOf("Microsoft.Windows.SDK.BuildTools.10.0.28000.2526",[StringComparison]::OrdinalIgnoreCase) -lt 0 -or
   $workflow.IndexOf("bin\10.0.28000.0\x64\signtool.exe",[StringComparison]::OrdinalIgnoreCase) -lt 0 -or
   $workflow.IndexOf("Microsoft.Windows.WDK.x64.10.0.28000.2526",[StringComparison]::OrdinalIgnoreCase) -lt 0 -or
   $workflow.IndexOf("c\bin\10.0.28000.0\x86\Inf2Cat.exe",[StringComparison]::OrdinalIgnoreCase) -lt 0) {
    throw 'H14_PINNED_TOOL_PATHS_MISSING'
}

# Signing order must be: sign SYS -> Inf2Cat -> sign CAT.
$signSys=$workflow.IndexOf('& $signtool sign /fd SHA256',[StringComparison]::OrdinalIgnoreCase)
$inf2cat=$workflow.IndexOf('& $inf2cat "/driver:$package"',[StringComparison]::OrdinalIgnoreCase)
$signCat=$workflow.IndexOf('& $signtool sign /fd SHA256',$signSys+1,[StringComparison]::OrdinalIgnoreCase)
if($signSys -lt 0 -or $inf2cat -lt 0 -or $signCat -lt 0 -or
   -not($signSys -lt $inf2cat -and $inf2cat -lt $signCat)) {
    throw "H14_SIGNING_ORDER_INVALID: sys=$signSys inf2cat=$inf2cat cat=$signCat"
}

foreach($store in @('My','Root','TrustedPublisher')) {
    $needle="Cert:\CurrentUser\$store\$thumb"
    if($workflow.IndexOf($needle,[StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "H14_CERT_CLEANUP_MISSING: $store"
    }
}

$artifactCopies=@(
    $workflow -split "\r?\n" | Where-Object {
        $_ -match '(?i)Copy-Item' -and $_ -match '(?i)_artifact_m062' -and
        $_ -match '(?i)(\.sys|\.inf|\.cat|\.cer|\.pfx|\.p12)'
    }
)
if($artifactCopies.Count -ne 0) {
    throw 'H14_SIGNED_OR_KEY_PAYLOAD_COPY_TO_ARTIFACT_FORBIDDEN'
}

Write-Host 'H14_SIGNING_STATIC_TESTS=PASS; ephemeral_cert=YES; private_key_export=NO; sign_sys_before_cat=YES; authenticode_verify=YES; target_trust=UNCHANGED; install=NO; package_upload=NO; playback=NO'
