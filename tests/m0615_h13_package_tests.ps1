$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$root=Join-Path $PSScriptRoot '..'
$infPath=Join-Path $root 'm062\m1\phaser360_m1_boot.inf'
$backupPath=Join-Path $root 'm062\m1\Backup-BaselineDriver.ps1'
$contractPath=Join-Path $root 'm062\m1\target_contract.json'
$workflowPath=Join-Path $root '.github\workflows\build-m062-dma.yml'

$inf=Get-Content -LiteralPath $infPath -Raw
$backup=Get-Content -LiteralPath $backupPath -Raw
$contract=Get-Content -LiteralPath $contractPath -Raw|ConvertFrom-Json
$workflow=Get-Content -LiteralPath $workflowPath -Raw

$exact=[string]$contract.controller_hardware_id
$escaped=[regex]::Escape($exact)
$matches=[regex]::Matches($inf,$escaped,[Text.RegularExpressions.RegexOptions]::IgnoreCase)
if($matches.Count -ne 1){throw "H13_EXACT_HWID_COUNT_INVALID: $($matches.Count)"}

foreach($required in @(
    'Signature="$WINDOWS NT$"',
    'Class=MEDIA',
    'ClassGuid={4d36e96c-e325-11ce-bfc1-08002be10318}',
    'CatalogFile=phaser360_m1_boot.cat',
    'PnpLockdown=1',
    'AddService=Phaser360M1,0x00000002,Phaser360M1_Service',
    'ServiceType=1',
    'StartType=3',
    'ErrorControl=1',
    'ServiceBinary=%13%\phaser360_m1_boot.sys',
    'KmdfService=Phaser360M1,Phaser360M1_Wdf',
    'KmdfLibraryVersion=1.31'
)) {
    if($inf.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "H13_INF_REQUIRED_MISSING: $required"
    }
}

foreach($forbidden in @(
    'KmdfLibraryVersion=1.33',
    'UpperFilters',
    'LowerFilters',
    'AddInterface',
    'KSCATEGORY_AUDIO',
    'WaveRT',
    'PortCls',
    'ACX',
    'MAX98357',
    'DA7219',
    'MX98357',
    'DLGS7219'
)) {
    if($inf.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "H13_INF_FORBIDDEN_PRESENT: $forbidden"
    }
}

# Reject a broad DEV_3198-only model line. The only PCI model must be the exact
# SUBSYS/REV identity in target_contract.json.
$modelLines=@($inf -split "\r?\n" | Where-Object {$_ -match '(?i)PCI\\VEN_8086&DEV_3198'})
if($modelLines.Count -ne 1 -or $modelLines[0].IndexOf($exact,[StringComparison]::OrdinalIgnoreCase) -lt 0) {
    throw 'H13_MODEL_NOT_EXACT_TARGET_ONLY'
}

# TargetOSVersion BuildNumber is a minimum. Keep exactly two model decorations:
# 19044 contains the exact device; 19045 is intentionally empty so later
# builds select the empty section instead of falling back to the 19044 model.
$manufacturerLine=@($inf -split "\r?\n" | Where-Object {
    $_ -match '(?i)^\s*%ProviderName%\s*=\s*Phaser360M1\.Models,'
})
$expectedManufacturer='%ProviderName%=Phaser360M1.Models,NTamd64.10.0...19045,NTamd64.10.0...19044'
if($manufacturerLine.Count -ne 1 -or $manufacturerLine[0].Trim() -cne $expectedManufacturer){
    throw 'H13_TARGETOS_MANUFACTURER_DECORATIONS_INVALID'
}

$infLines=@($inf -split "\r?\n")
$sectionBuilds=New-Object System.Collections.Generic.List[int]
$sectionBodies=@{}
$currentBuild=$null
foreach($line in $infLines){
    if($line -match '(?i)^\[Phaser360M1\.Models\.NTamd64\.10\.0\.\.\.(\d+)\]\s*$'){
        $currentBuild=[int]$Matches[1]
        $sectionBuilds.Add($currentBuild)
        $sectionBodies[$currentBuild]=New-Object System.Collections.Generic.List[string]
        continue
    }
    if($line -match '^\['){$currentBuild=$null}
    if($null -ne $currentBuild){$sectionBodies[$currentBuild].Add($line)}
}
$sortedBuilds=@($sectionBuilds | Sort-Object)
if($sortedBuilds.Count -ne 2 -or ($sortedBuilds -join ',') -cne '19044,19045'){
    throw ('H13_TARGETOS_SECTIONS_INVALID: '+($sortedBuilds -join ','))
}

function Resolve-H13ModelBuild([int]$Build){
    $eligible=@($sectionBuilds | Where-Object {$_ -le $Build} | Sort-Object -Descending)
    if($eligible.Count -eq 0){return $null}
    return [int]$eligible[0]
}
if($null -ne (Resolve-H13ModelBuild 19043)){throw 'H13_BUILD_19043_MUST_BE_UNSUPPORTED'}
if((Resolve-H13ModelBuild 19044) -ne 19044){throw 'H13_BUILD_19044_MUST_SELECT_19044'}
if((Resolve-H13ModelBuild 19045) -ne 19045){throw 'H13_BUILD_19045_MUST_SELECT_EMPTY_BLOCK'}
if((Resolve-H13ModelBuild 22621) -ne 19045){throw 'H13_LATER_BUILD_MUST_SELECT_EMPTY_BLOCK'}

$blockedBody=@($sectionBodies[19045] | Where-Object {
    -not [string]::IsNullOrWhiteSpace($_) -and $_ -notmatch '^\s*;'
})
if($blockedBody.Count -ne 0){throw 'H13_BUILD_19045_SECTION_NOT_EMPTY'}
$allowedBody=($sectionBodies[19044] -join "`n")
if($allowedBody.IndexOf($exact,[StringComparison]::OrdinalIgnoreCase) -lt 0){
    throw 'H13_BUILD_19044_EXACT_MODEL_MISSING'
}

foreach($required in @(
    'EXACT_WINDOWS_BUILD_19044_REQUIRED',
    'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06',
    '/export-driver',
    'SYSTEM_MUTATION=NONE',
    "DriverInstall='NO'",
    "DriverUninstall='NO'",
    "DeviceRestart='NO'",
    "Reboot='NO'",
    "MMIO='NO'",
    "DSPBoot='NO'",
    "AudioPlayback='NO'"
)) {
    if($backup.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "H13_BASELINE_BACKUP_REQUIRED_MISSING: $required"
    }
}

foreach($forbidden in @(
    '/add-driver','/delete-driver','/install','/uninstall',
    '/restart-device','/disable-device','/enable-device',
    'bcdedit','reagentc /disable','dism /remove-driver',
    'devcon','sc.exe','Set-PnpDevice'
)) {
    if($backup.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0) {
        throw "H13_BASELINE_BACKUP_MUTATION_PRESENT: $forbidden"
    }
}

foreach($required in @(
    'Inf2Cat.exe',
    '/os:10_VB_X64',
    'H13_INF2CAT=PASS',
    'phaser360_m1_boot.cat',
    'H13_PACKAGE_UPLOADED=FALSE',
    'H13_INSTALL_EXECUTED=FALSE',
    'Remove-Item -LiteralPath $package -Recurse -Force'
)) {
    if($workflow.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "H13_WORKFLOW_REQUIRED_MISSING: $required"
    }
}

$artifactPackageCopies=@(
    $workflow -split "\r?\n" | Where-Object {
        $_ -match '(?i)Copy-Item' -and
        $_ -match '(?i)_artifact_m062' -and
        $_ -match '(?i)(phaser360_m1_boot\.inf|phaser360_m1_boot\.cat|phaser360_m1_boot\.sys)'
    }
)
if($artifactPackageCopies.Count -ne 0) {
    throw 'H13_PACKAGE_PAYLOAD_COPY_TO_ARTIFACT_FORBIDDEN'
}

Write-Host 'H13_PACKAGE_STATIC_TESTS=PASS; exact_hwid=YES; exact_os_build=19044; build_19045_plus=BLOCKED_BY_EMPTY_SECTION; kmdf=1.31; service_start=DEMAND; filters=NONE; endpoints=NONE; baseline_export=READ_ONLY_SYSTEM; install=NO; package_upload=NO; playback=NO'
