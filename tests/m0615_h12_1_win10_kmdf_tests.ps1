$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$root=Join-Path $PSScriptRoot '..'
$staticProject=Get-Content -LiteralPath (Join-Path $root 'm062\driver\phaser360_boot_dma.vcxproj') -Raw
$driverProject=Get-Content -LiteralPath (Join-Path $root 'm062\driver\phaser360_m1_boot.vcxproj') -Raw
$workflow=Get-Content -LiteralPath (Join-Path $root '.github\workflows\build-m062-dma.yml') -Raw
$contract=Get-Content -LiteralPath (Join-Path $root 'm062\m1\target_contract.json') -Raw | ConvertFrom-Json

foreach($pair in @(
    @{Name='static';Text=$staticProject},
    @{Name='driver';Text=$driverProject}
)){
    if($pair.Text.IndexOf('<KMDF_VERSION_MINOR>31</KMDF_VERSION_MINOR>',
                          [StringComparison]::Ordinal) -lt 0){
        throw "H12_1_KMDF31_PROJECT_MISSING: $($pair.Name)"
    }
    if($pair.Text.IndexOf('<KMDF_VERSION_MINOR>33</KMDF_VERSION_MINOR>',
                          [StringComparison]::Ordinal) -ge 0){
        throw "H12_1_KMDF33_PROJECT_FORBIDDEN: $($pair.Name)"
    }
}

$buildLines=@([regex]::Matches(
    $workflow,
    '(?im)^\s*msbuild\s+\.\\m062\\driver\\phaser360_(?:boot_dma|m1_boot)\.vcxproj[^\r\n]*'
) | ForEach-Object {$_.Value})
if($buildLines.Count -lt 3){
    throw "H12_1_EXPECTED_REAL_WDK_LINKS: count=$($buildLines.Count)"
}
foreach($line in $buildLines){
    if($line.IndexOf('/p:KMDF_VERSION_MINOR=31',[StringComparison]::Ordinal) -lt 0){
        throw "H12_1_BUILD_NOT_KMDF31: $line"
    }
    if($line.IndexOf('/p:KMDF_VERSION_MINOR=33',[StringComparison]::Ordinal) -ge 0){
        throw "H12_1_BUILD_KMDF33_FORBIDDEN: $line"
    }
}

if([int]$contract.windows_build_exact -ne 19044){
    throw 'H12_1_TARGET_MUST_REMAIN_BUILD_19044'
}
if([string]$contract.controller_hardware_id -cne
   'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06'){
    throw 'H12_1_TARGET_HWID_CHANGED'
}

foreach($required in @(
    'H12_1_WIN10_KMDF_COMPAT=PASS',
    'TARGET_OS=WINDOWS10_21H2_19044',
    'KMDF_TARGET=1.31',
    'KMDF_1_33=FORBIDDEN_FOR_WIN10_19044',
    'M1_PREFLIGHT=READ_ONLY_EXACT_TARGET_WIN10_19044_WINRE_REQUIRED',
    'SYS_UPLOADED=FALSE',
    'INF=ABSENT',
    'PHYSICAL_EXECUTION=NOT_AUTHORIZED',
    'AUDIO_PLAYBACK=NOT_IMPLEMENTED'
)){
    if($workflow.IndexOf($required,[StringComparison]::Ordinal) -lt 0){
        throw "H12_1_WORKFLOW_MARKER_MISSING: $required"
    }
}

if($workflow -match '(?im)Copy-Item[^\r\n]*\.(sys|inf|cat|cer|pfx|p12|pvk)\b'){
    throw 'H12_1_PACKAGE_COPY_FORBIDDEN'
}

Write-Host 'H12_1_WIN10_KMDF_COMPAT=PASS; target_build=19044; kmdf_target=1.31; kmdf_1_33=FORBIDDEN; real_wdk_links=ALL_1_31; sys_upload=NO; inf=ABSENT; physical_execution=NO; playback=NO'
