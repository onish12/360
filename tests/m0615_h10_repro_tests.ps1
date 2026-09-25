$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$root=Join-Path $PSScriptRoot '..'
$project=Get-Content -LiteralPath (Join-Path $root 'm062\driver\phaser360_m1_boot.vcxproj') -Raw
$workflow=Get-Content -LiteralPath (Join-Path $root '.github\workflows\build-m062-dma.yml') -Raw
$audit=Get-Content -LiteralPath (Join-Path $root 'scripts\audit-pe-repro.py') -Raw

foreach($required in @(
    '/Brepro',
    '<ConfigurationType>Driver</ConfigurationType>',
    '<DriverType>KMDF</DriverType>'
)) {
    if($project.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "H10_PROJECT_REQUIRED_MISSING: $required"
    }
}

foreach($required in @(
    'H10_REPRO_FIRST_SHA256=',
    'H10_REPRO_SECOND_SHA256=',
    'H10_REPRODUCIBLE_BUILD=PASS',
    'H10_PE_REPRO_FIRST.json',
    'H10_PE_REPRO_SECOND.json',
    'audit-pe-repro.py',
    'H10_REPRO_HASH_MISMATCH',
    'H10_REPRO_SIZE_MISMATCH',
    'Remove-Item -LiteralPath $sys -Force',
    'SYS_UPLOADED=FALSE',
    'INF=ABSENT',
    'AUDIO_PLAYBACK=NOT_IMPLEMENTED'
)) {
    if($workflow.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "H10_WORKFLOW_REQUIRED_MISSING: $required"
    }
}

if(([regex]::Matches($workflow,'msbuild .*phaser360_m1_boot\.vcxproj',
    [Text.RegularExpressions.RegexOptions]::IgnoreCase)).Count -lt 2) {
    throw 'H10_TWO_DRIVER_LINKS_REQUIRED'
}

foreach($required in @(
    'IMAGE_DEBUG_TYPE_REPRO = 16',
    'image_debug_type_repro',
    'debug directory absent or malformed',
    'IMAGE_DEBUG_TYPE_REPRO marker missing'
)) {
    if($audit.IndexOf($required,[StringComparison]::Ordinal) -lt 0) {
        throw "H10_PE_AUDIT_REQUIRED_MISSING: $required"
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
    throw 'H10_SYS_COPY_TO_ARTIFACT_FORBIDDEN'
}
if($project -match '(?i)<Inf\b') {
    throw 'H10_INF_ITEM_FORBIDDEN'
}

Write-Host 'H10_REPRO_STATIC_TESTS=PASS; brepro=YES; two_clean_links=YES; sha256_equal=REQUIRED; image_debug_type_repro=REQUIRED; sys_upload=NO; inf=ABSENT; playback=NO'
