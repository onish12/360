$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

$required = @(
    'PHASER360-Open-Audio.sln',
    'packages.config',
    'Directory.Build.props',
    'src/probe/phaser360_adsp_probe.vcxproj',
    'src/probe/driver.cpp',
    'src/probe/adsp_bus_contract.h',
    'src/probe/phaser360_adsp_probe.inf'
)

foreach ($rel in $required) {
    $p = Join-Path $root $rel
    if (-not (Test-Path $p)) { throw "Missing: $rel" }
    Write-Host "OK $rel"
}

$src = Get-Content (Join-Path $root 'src/probe/driver.cpp') -Raw
if ($src -notmatch 'PHASER_ENABLE_HARDWARE_CALLS\s+0') {
    throw 'Safety gate is not locked to zero.'
}

$forbiddenCalls = @(
    'SetDSPPowerState\s*\(',
    'GetResources\s*\(',
    'RegisterInterrupt\s*\(',
    'GetRenderStream\s*\(',
    'GetCaptureStream\s*\(',
    'PrepareDSP\s*\(',
    'TriggerDSP\s*\(',
    'DSPEnableSPIB\s*\('
)
foreach ($pattern in $forbiddenCalls) {
    if ($src -match $pattern) { throw "M0.1 forbidden hardware callback invocation detected: $pattern" }
}

Write-Host 'BOOTSTRAP STATIC SAFETY CHECK: PASS' -ForegroundColor Green
