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
    'MODE=READ_ONLY_ENUMERATION','WDF_INTERRUPT_CREATE=NO','IRQ_SELECTION=DEFERRED',
    'AUDIO_PLAYBACK=NO','Test-StableState','SHA256SUMS.txt')) {
    if($text.IndexOf($required,[StringComparison]::OrdinalIgnoreCase) -lt 0) {
        throw "REQUIRED_GUARD_MISSING: $required"
    }
}
foreach($command in @('Disable-PnpDevice','Enable-PnpDevice','Remove-PnpDevice',
    'Set-PnpDevice','Restart-Computer','Set-ItemProperty','Remove-ItemProperty')) {
    if($text -match ('(?im)^\s*'+[regex]::Escape($command)+'\b')) {
        throw "MUTATING_COMMAND_PRESENT: $command"
    }
}
if(([regex]::Matches($text,'(?i)&\s*\$pnp\b')).Count -ne 1) {
    throw 'PNPUTIL_INVOCATION_MUST_BE_SINGLE_WRAPPED_SITE'
}
if($cmd -notmatch '(?i)pause') { throw 'LAUNCHER_MUST_KEEP_CONSOLE_OPEN' }
if($cmd -match '(?i)pnputil') { throw 'LAUNCHER_MUST_NOT_CALL_PNPUTIL_DIRECTLY' }

& $scriptPath -SelfTest
if($LASTEXITCODE -ne 0) { throw "COLLECTOR_SELFTEST_FAILED_$LASTEXITCODE" }
Write-Host 'IRQ_CAPTURE_STATIC_TESTS=PASS; syntax=PASS; launcher_pause=YES; pnputil_wrapped=YES'
