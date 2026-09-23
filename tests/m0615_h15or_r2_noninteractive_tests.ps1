$ErrorActionPreference='Stop';Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$r=Get-Content (Join-Path $root 'm062\h15or_r2\Run-H15orR2Recovery.ps1') -Raw
$c=Get-Content (Join-Path $root 'm062\h15or_r2\RUN_H15OR_R2_RECOVERY.cmd') -Raw
if($r.IndexOf('Set-Content',[StringComparison]::OrdinalIgnoreCase)-ge0){throw 'H15OR_R2_SET_CONTENT_FORBIDDEN'}
foreach($x in @('RUNNER=H15OR_R2_NONINTERACTIVE_RECOVERY','RUNNER_BUILD=b6abe77d29c1092f2d99d5848a45312c53987431-r2','H15OR_R2_RUNNER_SELF_HASH_MISMATCH',"Purpose -cne 'H15OR_R2_READONLY_RECOVERY_PACKAGE'",'Phaser360H15orR2')){if($r.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15OR_R2_RUNNER_MISSING:$x"}}
foreach($x in @('-NonInteractive','Run-H15orR2Recovery.ps1','RUNNER_BUILD=b6abe77d29c1092f2d99d5848a45312c53987431-r2')){if($c.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15OR_R2_CMD_MISSING:$x"}}
Write-Host 'H15OR_R2_STATIC_TESTS=PASS; set_content=ABSENT; powershell_noninteractive=YES; self_hash=YES'
