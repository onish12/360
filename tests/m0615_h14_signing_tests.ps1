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
    'H14_CERT_ROOT_ADD_BEGIN',
    'H14_CERT_ROOT_ADD_END',
    'certutil.exe',
    '-addstore Root',
    '-delstore Root',
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

# Signing order is evaluated only inside the H14 runtime step. H13 has its own
# earlier Inf2Cat invocation and must not be mistaken for the H14 catalog pass.
$h14Start=$workflow.IndexOf('- name: H14 ephemeral sign and verify package then destroy all signing material',[StringComparison]::OrdinalIgnoreCase)
$h14End=$workflow.IndexOf('- name: Verify H14 signing guards',$h14Start+1,[StringComparison]::OrdinalIgnoreCase)
if($h14Start -lt 0 -or $h14End -le $h14Start){throw 'H14_WORKFLOW_STEP_BOUNDARY_INVALID'}
$h14=$workflow.Substring($h14Start,$h14End-$h14Start)
$signSys=$h14.IndexOf('& $signtool sign /fd SHA256',[StringComparison]::OrdinalIgnoreCase)
$inf2cat=$h14.IndexOf('& $inf2cat "/driver:$package"',[StringComparison]::OrdinalIgnoreCase)
$signCat=$h14.IndexOf('& $signtool sign /fd SHA256',$signSys+1,[StringComparison]::OrdinalIgnoreCase)
if($signSys -lt 0 -or $inf2cat -lt 0 -or $signCat -lt 0 -or
   -not($signSys -lt $inf2cat -and $inf2cat -lt $signCat)) {
    throw "H14_SIGNING_ORDER_INVALID: sys=$signSys inf2cat=$inf2cat cat=$signCat"
}

if($workflow.IndexOf('Import-Certificate',[StringComparison]::OrdinalIgnoreCase) -ge 0 -or
   $workflow.IndexOf('X509Store',[StringComparison]::OrdinalIgnoreCase) -ge 0) {
    throw 'H14_BLOCKING_CERT_STORE_API_FORBIDDEN'
}
if($workflow.IndexOf('& $certutil -f -addstore Root $cer',[StringComparison]::OrdinalIgnoreCase) -lt 0 -or
   $workflow.IndexOf('& $certutil -delstore Root $thumb',[StringComparison]::OrdinalIgnoreCase) -lt 0) {
    throw 'H14_CERTUTIL_ROOT_LIFECYCLE_MISSING'
}
if($workflow.IndexOf('Cert:\CurrentUser\My\$thumb',[StringComparison]::OrdinalIgnoreCase) -lt 0) {
    throw 'H14_SIGNER_CLEANUP_MISSING'
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

Write-Host 'H14_SIGNING_STATIC_TESTS=PASS; ephemeral_cert=YES; ci_root_trust=CERTUTIL_EPHEMERAL; private_key_export=NO; sign_sys_before_cat=YES; authenticode_verify=YES; target_trust=UNCHANGED; install=NO; package_upload=NO; playback=NO'
