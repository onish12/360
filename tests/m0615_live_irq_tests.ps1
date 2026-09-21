$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..\m062\live_irq'
$scriptPath=Join-Path $root 'Collect-IrqEvidence.ps1'
$cmdPath=Join-Path $root 'COLLECT_IRQ_EVIDENCE.cmd'
if (-not (Test-Path -LiteralPath $scriptPath) -or -not (Test-Path -LiteralPath $cmdPath)) {
    throw 'IRQ_CAPTURE_FILES_MISSING'
}
$tokens=$null; $errors=$null
[void][System.Management.Automation.Language.Parser]::ParseFile($scriptPath,[ref]$tokens,[ref]$errors)
if (@($errors).Count -ne 0) { throw ('POWERSHELL_PARSE_ERROR: '+(($errors | ForEach-Object Message)-join '; ')) }

$text=Get-Content -LiteralPath $scriptPath -Raw
$cmd=Get-Content -LiteralPath $cmdPath -Raw
foreach($required in @("'/enum-devices'","'/instanceid'","'/resources'","PCI\VEN_8086&DEV_3198",
    'SPDRP_ALLOC_CONFIG','SetupDiGetDeviceRegistryPropertyW','SETUPAPI_SPDRP_ALLOC_CONFIG',
    'CM_RESOURCE_INTERRUPT_MESSAGE','0x0002','WINDOWS_10_21H2_OR_NEWER_REQUIRED',
    'MODE=READ_ONLY_ENUMERATION','SETUPAPI_WRITE=NO','WDF_INTERRUPT_CREATE=NO',
    'IRQ_SELECTION=DEFERRED','AUDIO_PLAYBACK=NO','Test-StableState','SHA256SUMS.txt',
    'ADMINISTRATOR_REQUIRED_FOR_READONLY_ENUMERATION')) {
    if($text.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "REQUIRED_GUARD_MISSING: $required"
    }
}
foreach($command in @('Disable-PnpDevice','Enable-PnpDevice','Remove-PnpDevice',
    'Set-PnpDevice','Restart-Computer','Set-ItemProperty','Remove-ItemProperty',
    'SetupDiSetDeviceRegistryProperty','SetupDiCallClassInstaller','CM_Modify_Res_Des',
    'CM_Add_Res_Des','CM_Free_Res_Des(')) {
    if($text -match ('(?im)^.*\b'+[regex]::Escape($command)+'\b')) {
        throw "MUTATING_COMMAND_OR_API_PRESENT: $command"
    }
}
$wrappedCount=([regex]::Matches($text,'(?i)&\s*\$PnPUtil\b')).Count
$directCount=([regex]::Matches($text,'(?i)&\s*\$pnp\b')).Count
if($wrappedCount -ne 1 -or $directCount -ne 0) {
    throw "PNPUTIL_INVOCATION_MUST_BE_SINGLE_WRAPPED_SITE: wrapped=$wrappedCount direct=$directCount"
}
if($cmd -notmatch '(?i)pause') { throw 'LAUNCHER_MUST_KEEP_CONSOLE_OPEN' }
if($cmd -match '(?i)pnputil') { throw 'LAUNCHER_MUST_NOT_CALL_PNPUTIL_DIRECTLY' }

& $scriptPath -SelfTest
Write-Host 'IRQ_CAPTURE_STATIC_TESTS=PASS; syntax=PASS; launcher_pause=YES; pnputil_wrapped=YES; setupapi_readonly=YES; win10_path=YES'
