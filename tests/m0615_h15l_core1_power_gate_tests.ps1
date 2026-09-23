$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content (Join-Path $root 'm062\h15l\h15l_core1_power_gate.h') -Raw
$src=Get-Content (Join-Path $root 'm062\h15l\h15l_core1_power_gate.cpp') -Raw
$inf=Get-Content (Join-Path $root 'm062\h15l\phaser360_h15l_core1_power_gate.inf') -Raw
$run=Get-Content (Join-Path $root 'm062\h15l\Run-H15lTransaction.ps1') -Raw
$cmd=Get-Content (Join-Path $root 'm062\h15l\RUN_H15L_TRANSACTION.cmd') -Raw
$wf=Get-Content (Join-Path $root '.github\workflows\h15l-core1-power-package.yml') -Raw

foreach($x in @(
 'sizeof(H15lResultV1)==352u','H15lObservation immediate','H15lObservation after10us',
 'H15lObservation after100us','H15lObservation after500us','H15lNoCpaWrite'
)){if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_R3_HDR_MISSING: $x"}}

foreach($x in @(
 'kAllowedWriteMask=kSpa1','WriteMasked(c,kSpa1,kSpa1)','WriteMasked(c,kSpa1,0)',
 'DelayUs(10)','DelayUs(90)','DelayUs(400)','r.version=3u',
 'ReadObs(c,&r.immediate)','ReadObs(c,&r.after10us)','ReadObs(c,&r.after100us)','ReadObs(c,&r.after500us)'
)){if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_R3_SRC_MISSING: $x"}}

foreach($x in @('kCstall1','kCrst1','WdfInterruptCreate','WdfDma','WdfCommonBuffer','SetBusData')){
 if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15L_R3_FORBIDDEN: $x"}
}
if(([regex]::Matches($src,'WRITE_REGISTER_ULONG')).Count -ne 1){throw 'H15L_R3_WRITE_REGISTER_SITE_COUNT'}

foreach($x in @('DriverVer=09/23/2026,0.6.15.242','PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06')){
 if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_R3_INF_MISSING: $x"}
}

foreach($x in @(
 '$CertSubject=''CN=PHASER360 H15L R3 Ephemeral Test Signing''',
 "'H15L_R3_SPA_WRITE_READBACK_TELEMETRY_PACKAGE'",
 '$ResultBytes=352','Put32 $req 0 3',
 '$immediateRegs=Obs $r 112','$after10usRegs=Obs $r 160',
 '$after100usRegs=Obs $r 208','$after500usRegs=Obs $r 256','$restoredRegs=Obs $r 304',
 '$restoredAdspcs=U32 $r 324','H15L_R3_SPA_WRITE_READBACK_TELEMETRY_COMPLETE'
)){if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_R3_RUN_MISSING: $x"}}

foreach($x in @('bcdedit','/reboot','SetBusData')){
 if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15L_R3_RUN_FORBIDDEN: $x"}
}
foreach($x in @('Capture: imediat, +10us, +100us, +500us','CSTALL/CRST, Core0, CPA write')){
 if($cmd.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_R3_CMD_MISSING: $x"}
}
foreach($x in @('H15L_R3_SPA_WRITE_READBACK_TELEMETRY_PACKAGE','PHASER360_H15L_R3_SPA_WRITE_READBACK_TELEMETRY_PACKAGE','-KeyExportPolicy NonExportable')){
 if($wf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15L_R3_WF_MISSING: $x"}
}
Write-Host 'H15L_R3_STATIC_TESTS=PASS; spa1_write_only=YES; timed_readback=YES; rollback_exact_required=YES'
