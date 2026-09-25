$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$root=Join-Path $PSScriptRoot '..'
$workflow=Get-Content -LiteralPath (Join-Path $root '.github\workflows\build-m062-dma.yml') -Raw
$project=Get-Content -LiteralPath (Join-Path $root 'm062\driver\phaser360_m1_boot.vcxproj') -Raw
$entry=Get-Content -LiteralPath (Join-Path $root 'm062\driver\driver_entry.cpp') -Raw
$owner=Get-Content -LiteralPath (Join-Path $root 'm062\driver\device_owner.cpp') -Raw

foreach($required in @(
    'vswhere.exe',
    'H9_DUMPBIN_NOT_FOUND',
    '$dumpbinPath /headers $sys',
    '$dumpbinPath /imports $sys',
    'H9_PE_HEADERS.txt',
    'H9_PE_IMPORTS.txt',
    'H9_PE_AUDIT=PASS',
    'BCryptOpenAlgorithmProvider',
    'PE_MACHINE=X64',
    'PE_SUBSYSTEM=NATIVE',
    'AUDIO_IMPORTS=NONE',
    'USERMODE_IMPORTS=NONE',
    'Remove-Item -LiteralPath $sys -Force',
    'SYS_UPLOADED=FALSE'
)) {
    if($workflow.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "H9_WORKFLOW_REQUIRED_MISSING: $required"
    }
}
if($workflow.IndexOf('Hostx64',[StringComparison]::Ordinal) -lt 0 -or
   $workflow.IndexOf('x64',[StringComparison]::Ordinal) -lt 0) {
    throw 'H9_X64_DUMPBIN_SELECTION_MISSING'
}

foreach($required in @(
    '<ConfigurationType>Driver</ConfigurationType>',
    '<DriverType>KMDF</DriverType>',
    '<AdditionalDependencies>cng.lib;%(AdditionalDependencies)</AdditionalDependencies>'
)) {
    if($project.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "H9_DRIVER_PROJECT_REQUIRED_MISSING: $required"
    }
}

if($project -match '(?i)<Inf\b') {
    throw 'H9_INF_ITEM_FORBIDDEN'
}

foreach($forbidden in @(
    'PortCls','portcls','ACX','WaveRT','AudioEngine',
    'MAX98357','MX98357','DA7219','DLGS7219'
)) {
    if($entry.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0 -or
       $owner.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "H9_AUDIO_SOURCE_FORBIDDEN: $forbidden"
    }
}

$artifactSysCopies=@(
    $workflow -split "\r?\n" | Where-Object {
        $_ -match '(?i)Copy-Item' -and
        $_ -match '(?i)_artifact_m062' -and
        $_ -match '(?i)phaser360_m1_boot\.sys'
    }
)
if($artifactSysCopies.Count -ne 0) {
    throw 'H9_SYS_COPY_TO_ARTIFACT_FORBIDDEN'
}

Write-Host 'H9_PE_AUDIT_STATIC_TESTS=PASS; headers=REQUIRED; imports=REQUIRED; cng_import=REQUIRED; usermode_imports=FORBIDDEN; audio_imports=FORBIDDEN; sys_upload=NO; inf=ABSENT; playback=NO'
