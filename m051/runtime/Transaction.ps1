# Pure transaction logic. All machine operations are injected and tested offline.
Set-StrictMode -Version 2

function Assert-M051CleanTarget($Target) {
    if ($Target.HardwareIds -notcontains 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06') {
        throw 'TARGET_MISMATCH: controlerul nu este cel Phaser360 stabilit.'
    }
    if ($Target.Problem -ne 28 -or -not [string]::IsNullOrWhiteSpace($Target.Service) -or
        -not [string]::IsNullOrWhiteSpace($Target.Inf)) {
        throw 'BASELINE_NOT_CLEAN: controlerul trebuie sa fie fara driver (Code 28, fara service/INF).'
    }
}

function Find-M051OwnedPackage($Before, $After, [string]$InfHash) {
    $matching = @($After | Where-Object { $_.OriginalName -ieq 'phaser360_m051_mmio_ro.inf' })
    if ($matching.Count -eq 0) { return $null }
    if ($matching.Count -ne 1) { throw 'PACKAGE_OWNERSHIP_AMBIGUOUS' }
    $p = $matching[0]
    if ($p.PublishedName -notmatch '^oem[0-9]+\.inf$' -or
        $Before -contains $p.PublishedName -or $p.Hash -ine $InfHash) {
        throw 'PACKAGE_OWNERSHIP_UNPROVEN'
    }
    return $p
}

function Invoke-M051Transaction([hashtable]$Ops) {
    $s = [ordered]@{
        Version = '0.5.1'; Phase = 'preflight'; TrustAttempted = $false
        StageAttempted = $false; BindAttempted = $false; OwnedPackage = $null
        Snapshot = $null; Error = $null; CleanupErrors = @(); Clean = $false
    }
    try {
        & $Ops.Preflight $s
        & $Ops.Save $s
        $s.Phase = 'trust'; $s.TrustAttempted = $true
        & $Ops.Save $s
        & $Ops.Trust $s
        $s.Phase = 'stage'; $s.StageAttempted = $true
        & $Ops.Save $s
        & $Ops.Stage $s
        $s.OwnedPackage = & $Ops.Discover $s
        if ($null -eq $s.OwnedPackage) { throw 'STAGED_PACKAGE_NOT_FOUND' }
        & $Ops.Save $s
        # Recheck the live unbound device after staging, before allowing binding.
        & $Ops.BeforeBind $s
        $s.Phase = 'bind'; $s.BindAttempted = $true
        & $Ops.Save $s
        & $Ops.Bind $s
        $s.Phase = 'snapshot'
        & $Ops.Save $s
        $s.Snapshot = & $Ops.Snapshot $s
    } catch { $s.Error = $_.Exception.Message }
    finally {
        $s.Phase = 'cleanup'
        if ($s.StageAttempted) {
            try {
                # Staging can add the package and still return an error.
                # Rediscover before deletion: never trust a stale OEM number.
                $s.OwnedPackage = & $Ops.Discover $s
                if ($null -ne $s.OwnedPackage) { & $Ops.Remove $s }
                & $Ops.CheckClean $s
            } catch { $s.CleanupErrors += $_.Exception.Message }
        }
        if ($s.TrustAttempted) {
            try { & $Ops.Untrust $s }
            catch { $s.CleanupErrors += $_.Exception.Message }
        }
        $s.Clean = ($s.CleanupErrors.Count -eq 0)
        $s.Phase = if (-not $s.Clean) { 'RECOVERY_REQUIRED' }
                   elseif ($null -ne $s.Error) { 'STOPPED_CLEAN' }
                   else { 'SNAPSHOT_COMPLETE_CLEAN' }
        try { & $Ops.Save $s } catch { $s.CleanupErrors += $_.Exception.Message; $s.Clean = $false }
    }
    return [pscustomobject]$s
}
