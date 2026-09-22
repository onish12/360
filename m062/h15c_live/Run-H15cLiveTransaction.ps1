#requires -Version 5.1
param(
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [string]$OutputRoot=''
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
. (Join-Path $PSScriptRoot 'H15cLive-Common.ps1')

if(-not (Test-H15cAdministrator)){throw 'ADMINISTRATOR_REQUIRED'}
if(-not [Environment]::Is64BitProcess){throw 'WINDOWS_X64_PROCESS_REQUIRED'}

$before=Get-H15cTargetState
Assert-H15cExactHealthyIntelTarget $before
if(@(Get-H15cPublishedInf).Count -ne 0){throw 'H15C_LIVE_FILTER_ALREADY_PRESENT'}

$ci=Get-H15cCodeIntegrity
if(-not $ci.TestSignAllowed){throw 'CODE_INTEGRITY_TESTSIGN_NOT_ALLOWED'}
$re=Get-H15cWinRE
if($re.ExitCode -ne 0 -or $re.Status -cne 'ENABLED'){throw 'WINRE_NOT_READY'}
$package=Assert-H15cPackage $PackageRoot

if([string]::IsNullOrWhiteSpace($OutputRoot)){$OutputRoot=$PSScriptRoot}
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss'
$suffix=[Guid]::NewGuid().ToString('N').Substring(0,8)
$runDir=Join-Path $OutputRoot ('H15C_LIVE_TRANSACTION_'+$stamp+'_'+$suffix)
$backupDir=Join-Path $runDir 'intel_baseline_export'
$captureDir=Join-Path $runDir 'capture'
New-Item -ItemType Directory -Path $backupDir,$captureDir -Force|Out-Null

$before|ConvertTo-Json -Depth 8|Set-Content (Join-Path $runDir 'target_before.json') -Encoding UTF8
$ci|ConvertTo-Json -Depth 6|Set-Content (Join-Path $runDir 'code_integrity.json') -Encoding UTF8
$package|ConvertTo-Json -Depth 6|Set-Content (Join-Path $runDir 'package.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'H15C_LIVE_WINRE_ROLLBACK.txt') -Destination $runDir

$export=Invoke-H15cPnPUtil @('/export-driver',$before.DriverInfPath,$backupDir)
$export.Output|Set-Content (Join-Path $runDir 'pnputil_export_intel.txt') -Encoding UTF8
if($export.ExitCode -ne 0){throw "BASELINE_EXPORT_FAILED: $($export.ExitCode)"}
if(@(Get-ChildItem -LiteralPath $backupDir -Recurse -File).Count -eq 0){throw 'BASELINE_EXPORT_EMPTY'}

$publishedInf=$null
$installStarted=$false
$normalRollbackComplete=$false
$transactionError=$null
try {
    $installStarted=$true
    $install=Invoke-H15cPnPUtil @('/add-driver',$package.Inf,'/install')
    $install.Output|Set-Content (Join-Path $runDir 'pnputil_add_install.txt') -Encoding UTF8
    if($install.ExitCode -ne 0){throw "FILTER_INSTALL_FAILED: $($install.ExitCode)"}

    $published=@(Get-H15cPublishedInf)
    if($published.Count -ne 1){throw "EXPECTED_ONE_H15C_PUBLISHED_INF_AFTER_INSTALL: count=$($published.Count)"}
    $publishedInf=$published[0]
    $publishedInf|Set-Content (Join-Path $runDir 'H15cPublishedInf.txt') -Encoding ASCII

    $restart=Invoke-H15cPnPUtil @('/restart-device',$before.InstanceId)
    $restart.Output|Set-Content (Join-Path $runDir 'pnputil_restart_with_filter.txt') -Encoding UTF8
    if($restart.ExitCode -ne 0){throw "FILTER_DEVICE_RESTART_FAILED: $($restart.ExitCode)"}

    $withFilter=Wait-H15cTargetHealthy $before.InstanceId
    if(@($withFilter.CompoundUpperFilters|Where-Object {$_ -ceq $script:H15cService}).Count -ne 1){
        throw 'H15C_FILTER_NOT_PRESENT_IN_COMPOUND_UPPER_FILTERS'
    }
    $withFilter|ConvertTo-Json -Depth 8|Set-Content (Join-Path $runDir 'target_with_filter.json') -Encoding UTF8

    $collector=Join-Path $PSScriptRoot 'Collect-H15cLive.ps1'
    $ps=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    $old=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try{
        $collectorOutput=(& $ps -NoLogo -NoProfile -ExecutionPolicy Bypass -File $collector -OutputRoot $captureDir 2>&1|Out-String -Width 8192)
        $collectorCode=$LASTEXITCODE
    } finally {$ErrorActionPreference=$old}
    $collectorOutput|Set-Content (Join-Path $runDir 'collector_console.txt') -Encoding UTF8
    if($collectorCode -ne 0){throw "H15C_LIVE_COLLECTOR_FAILED: $collectorCode"}
    $captureZips=@(Get-ChildItem -LiteralPath $captureDir -Filter 'RESULT_H15C_LIVE_*.zip' -File)
    if($captureZips.Count -ne 1){throw "EXPECTED_ONE_CAPTURE_ZIP: count=$($captureZips.Count)"}

    $remove=Invoke-H15cPnPUtil @('/delete-driver',$publishedInf,'/uninstall','/force')
    $remove.Output|Set-Content (Join-Path $runDir 'pnputil_delete_filter.txt') -Encoding UTF8
    if($remove.ExitCode -ne 0){throw "FILTER_UNINSTALL_FAILED: $($remove.ExitCode)"}

    $restartBase=Invoke-H15cPnPUtil @('/restart-device',$before.InstanceId)
    $restartBase.Output|Set-Content (Join-Path $runDir 'pnputil_restart_baseline.txt') -Encoding UTF8
    if($restartBase.ExitCode -ne 0){throw "BASELINE_DEVICE_RESTART_FAILED: $($restartBase.ExitCode)"}

    $after=Wait-H15cTargetHealthy $before.InstanceId
    if(@(Get-H15cPublishedInf).Count -ne 0){throw 'H15C_FILTER_PACKAGE_REMAINS_AFTER_ROLLBACK'}
    foreach($name in @('InstanceId','Service','DriverInfPath','DriverVersion','DriverProvider')){
        if([string]$before.$name -cne [string]$after.$name){
            throw "BASELINE_IDENTITY_CHANGED_AFTER_ROLLBACK: $name"
        }
    }
    if(@($after.CompoundUpperFilters|Where-Object {$_ -ceq $script:H15cService}).Count -ne 0){
        throw 'H15C_FILTER_REMAINS_IN_COMPOUND_UPPER_FILTERS'
    }
    $after|ConvertTo-Json -Depth 8|Set-Content (Join-Path $runDir 'target_after.json') -Encoding UTF8
    $normalRollbackComplete=$true
} catch {
    $transactionError=$_.Exception
} finally {
    if($installStarted -and -not $normalRollbackComplete){
        $emergency=@(Get-H15cPublishedInf)
        $rollbackLog=New-Object System.Collections.Generic.List[string]
        foreach($inf in $emergency){
            $rr=Invoke-H15cPnPUtil @('/delete-driver',$inf,'/uninstall','/force')
            $rollbackLog.Add("DELETE $inf EXIT=$($rr.ExitCode)")
            $rollbackLog.Add($rr.Output)
        }
        try{
            $rr2=Invoke-H15cPnPUtil @('/restart-device',$before.InstanceId)
            $rollbackLog.Add("RESTART EXIT=$($rr2.ExitCode)")
            $rollbackLog.Add($rr2.Output)
        }catch{
            $rollbackLog.Add("RESTART_EXCEPTION=$($_.Exception.Message)")
        }
        $rollbackLog|Set-Content (Join-Path $runDir 'emergency_rollback.txt') -Encoding UTF8
    }
}

$final=$null
try{$final=Get-H15cTargetState}catch{}
$remaining=@(Get-H15cPublishedInf)
$baselineRestored=$false
if($final){
    $baselineRestored=($final.ProblemCode -eq 0 -and $final.Status -ceq 'OK' -and
        $final.InstanceId -ceq $before.InstanceId -and
        $final.Service -ceq $before.Service -and
        $final.DriverInfPath -ceq $before.DriverInfPath -and
        $final.DriverVersion -ceq $before.DriverVersion -and
        $final.DriverProvider -ceq $before.DriverProvider -and
        $remaining.Count -eq 0 -and
        @($final.CompoundUpperFilters|Where-Object {$_ -ceq $script:H15cService}).Count -eq 0)
}
if($final){$final|ConvertTo-Json -Depth 8|Set-Content (Join-Path $runDir 'target_final.json') -Encoding UTF8}

$result=[ordered]@{
    Status=$(if($normalRollbackComplete -and $baselineRestored -and -not $transactionError){
        'H15C_LIVE_CAPTURE_AND_ROLLBACK_COMPLETE'
    }else{'H15C_LIVE_TRANSACTION_FAILED'})
    PublishedInf=$publishedInf
    CaptureCompleted=$normalRollbackComplete
    BaselineRestored=$baselineRestored
    TransactionError=$(if($transactionError){$transactionError.Message}else{$null})
    BaseService=$before.Service;BaseInf=$before.DriverInfPath;BaseVersion=$before.DriverVersion
    TestSignAllowed=$ci.TestSignAllowed
    SystemReboot='NO';BcdWrite='NO';TrustChange='NO';RegistryWrite='NO'
    PciConfigWrite='NO';Mmio='NO';DspBoot='NO';AudioPlayback='NO'
    DeviceRestarts='TARGET_DEV3198_ONLY'
}
$result|ConvertTo-Json -Depth 8|Set-Content (Join-Path $runDir 'transaction.json') -Encoding UTF8
Write-H15cHashes $runDir
$zip=Join-Path $OutputRoot ('RESULT_H15C_LIVE_TRANSACTION_'+$stamp+'_'+$suffix+'.zip')
Compress-Archive -Path (Join-Path $runDir '*') -DestinationPath $zip -Force

Write-Host "STATUS=$($result.Status)"
Write-Host "BASELINE_RESTORED=$($baselineRestored.ToString().ToUpperInvariant())"
Write-Host "PUBLISHED_INF=$publishedInf"
Write-Host "Trimite fisierul: $zip"
if($transactionError){throw $transactionError}
if(-not $normalRollbackComplete -or -not $baselineRestored){exit 3}
