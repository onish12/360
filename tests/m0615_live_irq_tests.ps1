$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..\m062\live_irq'
$scriptPath=Join-Path $root 'Collect-IrqEvidence.ps1'
$cmdPath=Join-Path $root 'COLLECT_IRQ_EVIDENCE.cmd'
if(-not(Test-Path -LiteralPath $scriptPath)-or-not(Test-Path -LiteralPath $cmdPath)){throw 'IRQ_CAPTURE_FILES_MISSING'}
$tokens=$null;$errors=$null
[void][System.Management.Automation.Language.Parser]::ParseFile($scriptPath,[ref]$tokens,[ref]$errors)
if(@($errors).Count -ne 0){throw ('POWERSHELL_PARSE_ERROR: '+(($errors|ForEach-Object Message)-join '; '))}
$text=Get-Content -LiteralPath $scriptPath -Raw
$cmd=Get-Content -LiteralPath $cmdPath -Raw
foreach($required in @("'/enum-devices'","'/instanceid'","'/resources'","PCI\VEN_8086&DEV_3198",
 'CM_Locate_DevNodeW','CM_Get_First_Log_Conf','CM_Get_Next_Res_Des','CM_Get_Res_Des_Data_Size',
 'CM_Get_Res_Des_Data','CM_Free_Res_Des_Handle','CM_Free_Log_Conf_Handle','ALLOC_LOG_CONF',
 'CFGMGR32_ALLOC_LOG_CONF','CFGMGR_IRQ_SIGNALING=UNDETERMINED_BY_THIS_API',
 'WINDOWS_10_21H2_OR_NEWER_REQUIRED','MODE=READ_ONLY_ENUMERATION','CFGMGR_WRITE=NO',
 'WDF_INTERRUPT_CREATE=NO','IRQ_SELECTION=DEFERRED','AUDIO_PLAYBACK=NO',
 'Test-StableState','SHA256SUMS.txt','ADMINISTRATOR_REQUIRED_FOR_READONLY_ENUMERATION')){
 if($text.IndexOf($required,[StringComparison]::OrdinalIgnoreCase)-lt 0){throw "REQUIRED_GUARD_MISSING: $required"}
}
foreach($command in @('Disable-PnpDevice','Enable-PnpDevice','Remove-PnpDevice','Set-PnpDevice',
 'Restart-Computer','Set-ItemProperty','Remove-ItemProperty','SetupDiSetDeviceRegistryProperty',
 'SetupDiCallClassInstaller','CM_Modify_Res_Des','CM_Add_Res_Des','CM_Free_Res_Des(',
 'CM_Add_Empty_Log_Conf','CM_Free_Log_Conf(')){
 if($text -match ('(?im)^.*\b'+[regex]::Escape($command)+'\b')){throw "MUTATING_COMMAND_OR_API_PRESENT: $command"}
}
$wrapped=([regex]::Matches($text,'(?i)&\s*\$PnPUtil\b')).Count
$direct=([regex]::Matches($text,'(?i)&\s*\$pnp\b')).Count
if($wrapped -ne 1 -or $direct -ne 0){throw "PNPUTIL_INVOCATION_MUST_BE_SINGLE_WRAPPED_SITE: wrapped=$wrapped direct=$direct"}
if($cmd -notmatch '(?i)pause'){throw 'LAUNCHER_MUST_KEEP_CONSOLE_OPEN'}
if($cmd -match '(?i)pnputil'){throw 'LAUNCHER_MUST_NOT_CALL_PNPUTIL_DIRECTLY'}
& $scriptPath -SelfTest
Write-Host 'IRQ_CAPTURE_STATIC_TESTS=PASS; syntax=PASS; cfgmgr_readonly=YES; alloc_log_conf=YES; mutation_api_guard=YES; win10_path=YES'
