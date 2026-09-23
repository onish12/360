$ErrorActionPreference='Stop';Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$r=Get-Content (Join-Path $root 'm062\h15or_r2\Run-H15orR2Recovery.ps1') -Raw
$c=Get-Content (Join-Path $root 'm062\h15or_r2\RUN_H15OR_R2_RECOVERY.cmd') -Raw
if($r.IndexOf('Set-Content',[StringComparison]::OrdinalIgnoreCase)-ge0){throw 'H15OR_R2_SET_CONTENT_FORBIDDEN'}
if($r -match '(?im)^\\s*function\\s+SC\\s*\\('){throw 'H15OR_R2_SC_ALIAS_COLLISION_FORBIDDEN'}
if($r -match '(?im)(^|[;{])\\s*SC\\s+@\\('){throw 'H15OR_R2_SC_ALIAS_INVOCATION_FORBIDDEN'}
if($r.IndexOf('function InvokeScExe(',[StringComparison]::Ordinal)-lt0){throw 'H15OR_R2_INVOKE_SC_EXE_MISSING'}
foreach($x in @('RUNNER=H15OR_R2_NONINTERACTIVE_RECOVERY','RUNNER_BUILD=h15or-r2.1-sc-alias-fix-20260923','H15OR_R2_RUNNER_SELF_HASH_MISMATCH',"Purpose -cne 'H15OR_R2_READONLY_RECOVERY_PACKAGE'",'Phaser360H15orR2')){if($r.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15OR_R2_RUNNER_MISSING:$x"}}
foreach($x in @('-NonInteractive','Run-H15orR2Recovery.ps1','RUNNER_BUILD=h15or-r2.1-sc-alias-fix-20260923')){if($c.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15OR_R2_CMD_MISSING:$x"}}
Write-Host 'H15OR_R2_STATIC_TESTS=PASS; set_content=ABSENT; powershell_noninteractive=YES; self_hash=YES'
