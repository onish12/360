$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$root=Join-Path $PSScriptRoot '..'
$header=Get-Content -LiteralPath (Join-Path $root 'm062\driver\repeated_device_lifecycle.h') -Raw
$source=Get-Content -LiteralPath (Join-Path $root 'm062\driver\repeated_device_lifecycle.cpp') -Raw
$pnp=Get-Content -LiteralPath (Join-Path $root 'm062\driver\pnp_resources.cpp') -Raw
$tests=Get-Content -LiteralPath (Join-Path $root 'tests\sof_pnp_resources_tests.cpp') -Raw

if($header -match '\bremoved_\b' -or $source -match '\bremoved_\b'){
    throw 'H15A_DUPLICATE_REMOVAL_STATE_PRESENT'
}
if($header.IndexOf('bool Removed() const noexcept { return gate_.Removed(); }',[StringComparison]::Ordinal) -lt 0){
    throw 'H15A_GATE_NOT_SINGLE_REMOVAL_TRUTH'
}

$surpriseStart=$source.IndexOf('void RepeatedDeviceLifecycle::SurpriseRemoval() noexcept',[StringComparison]::Ordinal)
$surpriseEnd=$source.IndexOf('NTSTATUS RepeatedDeviceLifecycle::PreparedThunk',$surpriseStart,[StringComparison]::Ordinal)
if($surpriseStart -lt 0 -or $surpriseEnd -le $surpriseStart){throw 'H15A_SURPRISE_BODY_NOT_FOUND'}
$surprise=$source.Substring($surpriseStart,$surpriseEnd-$surpriseStart)
if($surprise.IndexOf('irq_.FenceForSurpriseRemoval()',[StringComparison]::Ordinal) -lt 0){
    throw 'H15A_SURPRISE_SOFTWARE_FENCE_MISSING'
}
if($surprise.IndexOf('irqBound_',[StringComparison]::Ordinal) -ge 0){
    throw 'H15A_SURPRISE_READS_UNSYNCHRONIZED_LIFECYCLE_STATE'
}
if($surprise.IndexOf('WRITE_REGISTER',[StringComparison]::OrdinalIgnoreCase) -ge 0 -or
   $surprise.IndexOf('MmUnmapIoSpace',[StringComparison]::OrdinalIgnoreCase) -ge 0){
    throw 'H15A_SURPRISE_HARDWARE_OR_MAPPING_MUTATION_PRESENT'
}

foreach($required in @(
    'H15A final PrepareHardware linearization point',
    'if(!gate_->Allowed())',
    'const auto releaseStatus=Release()',
    'STATUS_INVALID_DEVICE_STATE'
)){
    if($pnp.IndexOf($required,[StringComparison]::Ordinal) -lt 0){
        throw "H15A_PREPARE_RECHECK_MISSING: $required"
    }
}

foreach($required in @(
    'removeDuringPrepared=true',
    '0x512004000LL',
    'trace.sequence==std::vector<unsigned>({1,7,6})',
    'prepare(&racePreparedDevice,&rr,&rt)==STATUS_INVALID_DEVICE_STATE'
)){
    if($tests.IndexOf($required,[StringComparison]::Ordinal) -lt 0){
        throw "H15A_RACE_REGRESSION_MISSING: $required"
    }
}

Write-Host 'H15A_SURPRISE_RACE_STATIC_TESTS=PASS; removal_truth=HARDWARE_ACCESS_GATE_ATOMIC; surprise_irqbound_read=NONE; surprise_mmio=NONE; prepare_post_callback_gate_recheck=YES; prepared_race_regression=YES'
