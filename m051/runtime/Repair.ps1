Set-StrictMode -Version 2
function Assert-ReviewedRepairRecord($Record,[string]$Directory) {
    $c=$Record.Context; $t=$Record.Transaction
    if ($t.Version -ne '0.5.1' -or $t.Kind -ne 'INTEL_HANDOFF_1' -or
        $c.Mode -ne 'INTEL_HANDOFF_1' -or $t.StageAttempted -ne $true -or
        $c.Handoff.NullAttempted -ne $true -or $c.Handoff.NullRebootRequired -ne $false -or
        $c.Handoff.BindRebootRequired -ne $false -or $null -eq $t.Snapshot -or
        $c.InfHash -ine 'B9314489AB096311E5A01DC652A6FDFCDF5E7A8A5B8E7938C96110CF27A7D51D' -or
        $c.CertThumbprint -ine '5AC2064FA901334A31316ACF7742AC102F4EAE6C' -or
        $t.OwnedPackage.Hash -ine $c.InfHash -or
        $t.OwnedPackage.OriginalName -ine 'phaser360_m051_mmio_ro.inf' -or
        $t.OwnedPackage.PublishedName -notmatch '^oem[0-9]+\.inf$' -or
        @($c.BeforePackages) -contains $t.OwnedPackage.PublishedName -or
        [IO.Path]::GetFullPath($c.RunDir) -ine [IO.Path]::GetFullPath($Directory) -or
        (Split-Path -Leaf $Directory) -ne '20260916_211227_13c9e569') {
        throw 'REPAIR_JOURNAL_NOT_THE_REVIEWED_RUN'
    }
    if (@($c.Target.HardwareIds) -notcontains 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06' -or
        $t.Snapshot.InstanceId -ine $c.Target.InstanceId) { throw 'REPAIR_TARGET_RECORD_MISMATCH' }
}
function Remove-ReviewedDetachedPackage($State) {
    # Re-read the live binding and package immediately before deletion.
    $now=Get-M051Target
    if ($now.InstanceId -ine $script:M051.Target.InstanceId) { throw 'REPAIR_TARGET_CHANGED' }
    Assert-HandoffDetachedTarget $now
    $p=Find-M051CurrentOwned
    if ($null -eq $p -or $p.PublishedName -ine $State.OwnedPackage.PublishedName) { throw 'PACKAGE_OWNERSHIP_CHANGED' }
    Write-M051Journal $State
    Disable-M051OwnedService $p
    Invoke-M051Pnp @('/delete-driver',$p.PublishedName)
}
function Invoke-DetachedRepair($Record,[hashtable]$Ops) {
    $r=[ordered]@{ Version='1.1.1'; Status='IN_PROGRESS'; SnapshotAlreadyObtained=$true
        ProbeRepeated=$false; PackageDeleted=$false; InitialTarget=$null; FinalTarget=$null; IntelPackagePreserved=$null
        Error=$null; AudioPlayback='NOT_IMPLEMENTED' }
    try {
        & $Ops.Save $r
        & $Ops.Validate
        $target=& $Ops.Target
        $r.InitialTarget=$target
        if ($target.InstanceId -ine $Record.Context.Target.InstanceId) { throw 'REPAIR_TARGET_CHANGED' }
        Assert-HandoffDetachedTarget $target
        # No bind/null driver APIs, trust import, signing changes or Intel writes.
        $p=& $Ops.Discover
        if ($null -ne $p) {
            if ($p.PublishedName -ine $Record.Transaction.OwnedPackage.PublishedName) { throw 'REPAIR_PACKAGE_IDENTITY_CHANGED' }
            & $Ops.Remove $Record.Transaction
            $r.PackageDeleted=$true
        }
        & $Ops.Check $Record.Transaction
        & $Ops.Untrust
        $r.FinalTarget=& $Ops.Target
        Assert-HandoffDetachedTarget $r.FinalTarget
        if ($r.FinalTarget.InstanceId -ine $Record.Context.Target.InstanceId) { throw 'REPAIR_FINAL_TARGET_CHANGED' }
        $r.IntelPackagePreserved=$true
        $r.Status='RECOVERY_CLEANUP_COMPLETE'
    } catch { $r.Status='RECOVERY_REQUIRED'; $r.Error=$_.Exception.Message }
    finally { try { & $Ops.Save $r } catch { $r.Status='REPORT_WRITE_FAILED'; $r.Error=$_.Exception.Message } }
    [pscustomobject]$r
}
