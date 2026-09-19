param([string]$Root = (Split-Path -Parent $PSScriptRoot))
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$rootPath = [IO.Path]::GetFullPath($Root).TrimEnd('\','/')
$seen = @{}
foreach ($line in Get-Content -LiteralPath (Join-Path $rootPath 'SHA256SUMS.txt')) {
    if ($line -notmatch '^([0-9a-fA-F]{64})  ([A-Za-z0-9_./\\-]+)$') { throw 'INVALID_HASH_MANIFEST' }
    $hash = $Matches[1]; $relative = $Matches[2]
    if ($relative -match '(^|[\\/])\.\.([\\/]|$)' -or [IO.Path]::IsPathRooted($relative)) {
        throw 'MANIFEST_PATH_ESCAPE'
    }
    $path = [IO.Path]::GetFullPath((Join-Path $rootPath $relative))
    if (-not $path.StartsWith($rootPath + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        $seen.ContainsKey($relative)) { throw 'INVALID_OR_DUPLICATE_MANIFEST_PATH' }
    if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ine $hash) { throw "HASH_MISMATCH: $relative" }
    $seen[$relative.Replace('\','/')] = $true
}
foreach ($required in @('RUN_AUDIO.cmd','runtime/RUN_AUDIO.ps1','runtime/AudioDecision.ps1','runtime/AudioWindows.ps1',
    'REPAIR_AUDIO.cmd','runtime/REPAIR_AUDIO.ps1','runtime/Repair.ps1','M051_HARDWARE_20260916.md',
    'runtime/Transition.ps1','runtime/DeviceBinding.cs','runtime/DeviceBinding.dll','CLEANUP_AUDIO.cmd',
    'BUILD_AUDIT.txt','START_AICI.txt','runtime/RUN_M051.ps1','runtime/CLEANUP_M051.ps1','runtime/VERIFY_PACKAGE.ps1',
    'runtime/Transaction.ps1','runtime/Windows.ps1','runtime/Native.cs','RUN_M051.cmd','RECOVER_WINRE.cmd',
    'driver/phaser360_m051_mmio_ro.inf','driver/phaser360_m051_mmio_ro.sys',
    'driver/phaser360_m051_mmio_ro.cat','PHASER360_M051_TEST_SIGNING.cer')) {
    if (-not $seen.ContainsKey($required)) { throw "UNMANIFESTED_REQUIRED_FILE: $required" }
}
Write-Host "PACKAGE_HASHES=PASS ($($seen.Count) files)"
