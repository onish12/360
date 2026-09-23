$ErrorActionPreference='Stop';Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$r=Get-Content (Join-Path $root 'm062\h15or_r2\Run-H15orR2Recovery.ps1') -Raw
$c=Get-Content (Join-Path $root 'm062\h15or_r2\RUN_H15OR_R2_RECOVERY.cmd') -Raw
if($r.IndexOf('Set-Content',[StringComparison]::OrdinalIgnoreCase)-ge0){throw 'H15OR_R2_SET_CONTENT_FORBIDDEN'}
if($r.IndexOf('function SC(',[StringComparison]::OrdinalIgnoreCase)-ge0){throw 'H15OR_R2_SC_ALIAS_COLLISION_FORBIDDEN'}
if($r.IndexOf('SC @(',[StringComparison]::OrdinalIgnoreCase)-ge0){throw 'H15OR_R2_SC_ALIAS_INVOCATION_FORBIDDEN'}
if($r.IndexOf('function InvokeScExe(',[StringComparison]::Ordinal)-lt0){throw 'H15OR_R2_INVOKE_SC_EXE_MISSING'}
foreach($x in @('RUNNER=H15OR_R2_NONINTERACTIVE_RECOVERY','RUNNER_BUILD=h15or-r2.2-package-contract-fix-20260923','H15OR_R2_RUNNER_SELF_HASH_MISMATCH',"Purpose -cne 'H15OR_R2_READONLY_RECOVERY_PACKAGE'",'Phaser360H15orR2')){if($r.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15OR_R2_RUNNER_MISSING:$x"}}
foreach($x in @('-NonInteractive','Run-H15orR2Recovery.ps1','RUNNER_BUILD=h15or-r2.2-package-contract-fix-20260923')){if($c.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15OR_R2_CMD_MISSING:$x"}}
foreach($name in @('phaser360_h15or_r2_readonly_recovery_probe.sys','phaser360_h15or_r2_readonly_recovery_probe.cer')){
  if($r.IndexOf($name,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15OR_R2_RUNNER_PACKAGE_NAME_MISSING:$name"}
  if($w.IndexOf($name,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15OR_R2_WORKFLOW_PACKAGE_NAME_MISSING:$name"}
}
foreach($legacy in @("'phaser360_h15or_readonly_recovery_probe.sys'","'phaser360_h15or_readonly_recovery_probe.cer'")){
  if($w.IndexOf($legacy,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15OR_R2_LEGACY_PACKAGE_NAME_FORBIDDEN:$legacy"}
}
Write-Host 'H15OR_R2_STATIC_TESTS=PASS; set_content=ABSENT; powershell_noninteractive=YES; self_hash=YES; package_contract=EXACT'
