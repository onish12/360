# Offline tests: extract functions/finally with the PowerShell AST. Never run
# the runner's entry point; all device, native bind and certificate calls are mocks.
#requires -Version 5.1
$ErrorActionPreference='Stop';Set-StrictMode -Version 2
$path=Join-Path $PSScriptRoot '..\m062\m1_fast\Run-M1FastSafe.ps1'
$tokens=$null;$parseErrors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Resolve-Path $path).Path,[ref]$tokens,[ref]$parseErrors)
if($parseErrors.Count){throw ($parseErrors|Out-String)}
function Check([bool]$ok,[string]$name){if(-not$ok){throw "RUNNER_CONTRACT_FAILED: $name"}}
foreach($name in @('DecodeStageTrace','StopStageTrace')){
  $fn=$ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst] -and $n.Name -ceq $name},$true)
  Check ($fn.Count-eq1) "function $name"
  . ([scriptblock]::Create($fn[0].Extent.Text))
}
$outer=@($ast.FindAll({param($n) $n -is [Management.Automation.Language.TryStatementAst] -and $n.Finally -and $n.Finally.Extent.Text.Contains('emergency_rollback.txt')},$true))
Check ($outer.Count-eq1) 'unique recovery finally'
$body=$outer[0].Finally.Extent.Text.Trim()
$recovery=[scriptblock]::Create($body.Substring(1,$body.Length-2))
$script:writes=@{}
function WriteUtf8([string]$p,$v){$script:writes[[IO.Path]::GetFileName($p)]=[string]$v}
function Logman($a){[pscustomobject]@{ExitCode=5;Output='MOCK_STOP_FAILED'}}
$dir=Join-Path ([IO.Path]::GetTempPath()) ('phaser360-runner-test-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $dir|Out-Null
try{
  # Direct Data is how EtwWriteString is represented by tracerpt in R7.
  $xml=Join-Path $dir 'trace.xml'
  $event='<Event xmlns="http://schemas.microsoft.com/win/2004/08/events/event"><System><TimeCreated SystemTime="RAW_TIME"/></System><Data>PHASER360_R8_CANDIDATE {0} status=0x00000000</Data></Event>'
  [IO.File]::WriteAllText($xml,('<Events>'+($event-f'A00_DRIVER_ENTRY')+($event-f'D30_FIRMWARE_ENTER')+'</Events>'))
  $decoded=DecodeStageTrace $xml $dir
  Check ($decoded.EventCount-eq2 -and $decoded.DriverEntries-eq1 -and $decoded.BootEntries-eq1) 'decode direct Data and count boot'
  $rows=@($script:writes['stage_trace_decoded.json']|ConvertFrom-Json)
  Check ($rows[0].TimestampRaw-ceq'RAW_TIME') 'preserve original timestamp'
  [IO.File]::WriteAllText($xml,('<Events>'+($event-f'D30_FIRMWARE_ENTER')+($event-f'D30_FIRMWARE_ENTER')+'</Events>'))
  $decoded=DecodeStageTrace $xml $dir
  Check ($decoded.BootEntries-eq2 -and $decoded.DriverEntries-eq0) 'detect repeated D0 without reload'
  [IO.File]::WriteAllText($xml,'<Events/>')
  $decoded=DecodeStageTrace $xml $dir
  Check ($decoded.EventCount-eq0) 'empty trace is not evidence'
  Check (-not$decoded.LossChecked) 'missing header cannot prove lossless capture'
  $header='<Event xmlns="http://schemas.microsoft.com/win/2004/08/events/event"><System><Provider Guid="{9e814aad-3204-11d2-9a82-006008a86939}"/><Opcode>0</Opcode></System><EventData><Data Name="EventsLost">0</Data><Data Name="BuffersLost">0</Data></EventData></Event>'
  [IO.File]::WriteAllText($xml,('<Events>'+$header+'</Events>'))
  $decoded=DecodeStageTrace $xml $dir
  Check ($decoded.LossChecked -and $decoded.EventsLost-eq0 -and $decoded.BuffersLost-eq0) 'loss header decoded'
  [IO.File]::WriteAllText($xml,('<Events>'+$header.Replace('Name="EventsLost">0','Name="EventsLost">7')+'</Events>'))
  $decoded=DecodeStageTrace $xml $dir
  Check ($decoded.EventsLost-eq7) 'reported lost events remain visible'
  $trace=[pscustomobject]@{Name='MOCK';Started=$true;Stopped=$false;StopExitCode=$null}
  StopStageTrace $trace $dir
  Check (-not$trace.Stopped -and $trace.StopExitCode-eq5) 'failed ETW stop cannot report stopped'

  Add-Type -TypeDefinition @'
using System;
namespace Phaser360 { public static class M1FastNative {
 public static int Calls; public static bool Fail;
 public static bool ForceUpdate(string hwid,string inf) {
  Calls++; if(Fail) throw new Exception("MOCK_BIND_FAILURE"); return false;
 }
} }
'@
  function StopStageTrace($trace,[string]$dir){throw 'MOCK_TRACE_IO_FAILURE'}
  function PublishedM1{if($script:remaining){'oem29.inf'}}
  function PnP($a){if(-not$script:keepPackage){$script:remaining=$false};[pscustomobject]@{ExitCode=0;Output='MOCK_DELETE'}}
  function WaitIntel([string]$instance,[int]$seconds){
    if($script:autoIntel -or [Phaser360.M1FastNative]::Calls-gt0){return [pscustomobject]@{Status='OK'}}
    throw 'MOCK_INTEL_TIMEOUT'
  }
  function IsIntel($s){$null-ne$s -and $s.Status-ceq'OK'}
  function CertUtil($a){++$script:certCalls;[pscustomobject]@{ExitCode=0;Output='MOCK_CERT_DELETE'}}
  $export=Join-Path $dir 'intel.inf';[IO.File]::WriteAllText($export,'MOCK')
  foreach($scenario in @('auto','fallback','bind_failure','package_remains','export_missing','trace_failure')){
    $script:writes=@{};$script:remaining=$true;$script:certCalls=0
    $script:keepPackage=($scenario-ceq'package_remains');$script:autoIntel=($scenario-ceq'auto')
    [Phaser360.M1FastNative]::Calls=0;[Phaser360.M1FastNative]::Fail=($scenario-ceq'bind_failure')
    $rollbackComplete=$false;$published=$true;$bindAttempted=$true
    $fallbackAttempted=$false;$fallbackIntel=$false;$fallbackRebootSignalled=$false
    $pubAdded=$true;$rootAdded=$true;$ExactHwid='MOCK_HWID'
    $before=[pscustomobject]@{InstanceId='MOCK_INSTANCE'};$pkg=[pscustomobject]@{Thumb='MOCK_THUMB'}
    $baselineExportInf=if($scenario-ceq'export_missing'){Join-Path $dir 'missing.inf'}else{$export}
    $stageTrace=if($scenario-ceq'trace_failure'){[pscustomobject]@{Stopped=$false}}else{$null}
    . $recovery
    $expectSafe=$scenario-in@('auto','fallback','trace_failure')
    $expectCalls=if($scenario-in@('fallback','bind_failure','trace_failure')){1}else{0}
    Check ($safe-eq$expectSafe) "$scenario safe state"
    Check ([Phaser360.M1FastNative]::Calls-eq$expectCalls) "$scenario bounded Intel bind"
    Check ($script:certCalls-eq$(if($expectSafe){2}else{0})) "$scenario trust policy"
    Check ($script:writes.ContainsKey('emergency_rollback.txt')) "$scenario recovery log"
    Check (-not$fallbackRebootSignalled) "$scenario no automatic reboot"
  }
  Write-Host 'M1_R8_RUNNER_CONTRACT_TESTS=PASS; device=MOCKED; native_bind=MOCKED; trace_cases=6; rollback_cases=6'
}finally{
  Remove-Item -LiteralPath $dir -Recurse -Force
}
