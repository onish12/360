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
$trustBefore=Get-H15cCertificatePresence $package.CertificateThumbprint
if($trustBefore.Root -or $trustBefore.TrustedPublisher){
    throw 'H15C_LIVE_PACKAGE_CERT_ALREADY_TRUSTED'
}

if([string]::IsNullOrWhiteSpace($OutputRoot)){$OutputRoot=$PSScriptRoot}
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss'
$suffix=[Guid]::NewGuid().ToString('N').Substring(0,8)
$runDir=Join-Path $OutputRoot ('H15C_LIVE_R2_TRANSACTION_'+$stamp+'_'+$suffix)
$backupDir=Join-Path $runDir 'intel_baseline_export'
$captureDir=Join-Path $runDir 'capture'
New-Item -ItemType Directory -Path $backupDir,$captureDir -Force|Out-Null

$before|ConvertTo-Json -Depth 8|Set-Content (Join-Path $runDir 'target_before.json') -Encoding UTF8
$ci|ConvertTo-Json -Depth 6|Set-Content (Join-Path $runDir 'code_integrity.json') -Encoding UTF8
$package|ConvertTo-Json -Depth 6|Set-Content (Join-Path $runDir 'package.json') -Encoding UTF8
$trustBefore|ConvertTo-Json -Depth 4|Set-Content (Join-Path $runDir 'trust_before.json') -Encoding UTF8
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'H15C_LIVE_WINRE_ROLLBACK.txt') -Destination $runDir

$export=Invoke-H15cPnPUtil @('/export-driver',$before.DriverInfPath,$backupDir)
$export.Output|Set-Content (Join-Path $runDir 'pnputil_export_intel.txt') -Encoding UTF8
if($export.ExitCode -ne 0){throw "BASELINE_EXPORT_FAILED: $($export.ExitCode)"}
if(@(Get-ChildItem -LiteralPath $backupDir -Recurse -File).Count -eq 0){throw 'BASELINE_EXPORT_EMPTY'}

$publishedInf=$null
$rootAdded=$false
$publisherAdded=$false
$installStarted=$false
$captureCompleted=$false
$compoundFilterObserved=$false
$normalRollbackComplete=$false
$transactionError=$null

try {
    $rootAdd=Invoke-H15cCertUtil @('-f','-addstore','Root',$package.Certificate)
    $rootAdd.Output|Set-Content (Join-Path $runDir 'certutil_add_root.txt') -Encoding UTF8
    if($rootAdd.ExitCode -ne 0){throw "CERT_ROOT_ADD_FAILED: $($rootAdd.ExitCode)"}
    $rootAdded=$true

    $publisherAdd=Invoke-H15cCertUtil @('-f','-addstore','TrustedPublisher',$package.Certificate)
    $publisherAdd.Output|Set-Content (Join-Path $runDir 'certutil_add_trustedpublisher.txt') -Encoding UTF8
    if($publisherAdd.ExitCode -ne 0){throw "CERT_TRUSTEDPUBLISHER_ADD_FAILED: $($publisherAdd.ExitCode)"}
    $publisherAdded=$true

    $trustedNow=Get-H15cCertificatePresence $package.CertificateThumbprint
    if(-not $trustedNow.Root -or -not $trustedNow.TrustedPublisher){
        throw 'CERT_TRUST_NOT_PRESENT_AFTER_ADD'
    }
    $trustedPackage=Assert-H15cPackage $PackageRoot -RequireTrusted
    $trustedPackage|ConvertTo-Json -Depth 6|Set-Content (Join-Path $runDir 'package_after_trust.json') -Encoding UTF8

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
    $compoundFilterObserved=(@($withFilter.CompoundUpperFilters|Where-Object {$_ -ceq $script:H15cService}).Count -eq 1)
    $withFilter|ConvertTo-Json -Depth 8|Set-Content (Join-Path $runDir 'target_with_filter.json') -Encoding UTF8
    [pscustomobject]@{
        Service=$script:H15cService
        Observed=$compoundFilterObserved
        Values=@($withFilter.CompoundUpperFilters)
        Source=$withFilter.CompoundUpperFiltersSource
        QueryError=$withFilter.CompoundUpperFiltersQueryError
        Role='ADVISORY_TELEMETRY_ONLY'
        PrimaryProof='DEVICE_INTERFACE_PLUS_READ_ONLY_IOCTL'
    }|ConvertTo-Json -Depth 6|Set-Content (Join-Path $runDir 'compound_upper_filters_observation.json') -Encoding UTF8
    try {
        $svc=Get-Service -Name $script:H15cService -ErrorAction Stop
        [pscustomobject]@{Found=$true;Name=$svc.Name;Status=[string]$svc.Status}|
            ConvertTo-Json -Depth 4|Set-Content (Join-Path $runDir 'filter_service_after_restart.json') -Encoding UTF8
    } catch {
        [pscustomobject]@{Found=$false;Error=$_.Exception.Message}|
            ConvertTo-Json -Depth 4|Set-Content (Join-Path $runDir 'filter_service_after_restart.json') -Encoding UTF8
    }

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
    $captureCompleted=$true

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
    $after|ConvertTo-Json -Depth 8|Set-Content (Join-Path $runDir 'target_after_driver_rollback.json') -Encoding UTF8

    $publisherDel=Invoke-H15cCertUtil @('-delstore','TrustedPublisher',$package.CertificateThumbprint)
    $publisherDel.Output|Set-Content (Join-Path $runDir 'certutil_delete_trustedpublisher.txt') -Encoding UTF8
    if($publisherDel.ExitCode -ne 0){throw "CERT_TRUSTEDPUBLISHER_REMOVE_FAILED: $($publisherDel.ExitCode)"}
    $publisherAdded=$false

    $rootDel=Invoke-H15cCertUtil @('-delstore','Root',$package.CertificateThumbprint)
    $rootDel.Output|Set-Content (Join-Path $runDir 'certutil_delete_root.txt') -Encoding UTF8
    if($rootDel.ExitCode -ne 0){throw "CERT_ROOT_REMOVE_FAILED: $($rootDel.ExitCode)"}
    $rootAdded=$false

    $trustAfter=Get-H15cCertificatePresence $package.CertificateThumbprint
    if($trustAfter.Root -or $trustAfter.TrustedPublisher){throw 'CERT_TRUST_REMAINS_AFTER_ROLLBACK'}
    $trustAfter|ConvertTo-Json -Depth 4|Set-Content (Join-Path $runDir 'trust_after.json') -Encoding UTF8
    $after|ConvertTo-Json -Depth 8|Set-Content (Join-Path $runDir 'target_after.json') -Encoding UTF8
    $normalRollbackComplete=$true
} catch {
    $transactionError=$_.Exception
} finally {
    if(-not $normalRollbackComplete){
        $rollbackLog=New-Object System.Collections.Generic.List[string]

        if($installStarted){
            $emergency=@(Get-H15cPublishedInf)
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
        }

        $remainingForTrust=@(Get-H15cPublishedInf)
        $rollbackTarget=$null
        try{$rollbackTarget=Get-H15cTargetState}catch{
            $rollbackLog.Add("TARGET_CHECK_EXCEPTION=$($_.Exception.Message)")
        }
        $safeToDropTrust=(-not $installStarted) -or
            ($remainingForTrust.Count -eq 0 -and $rollbackTarget -and
             $rollbackTarget.ProblemCode -eq 0 -and $rollbackTarget.Status -ceq 'OK' -and
             $rollbackTarget.InstanceId -ceq $before.InstanceId -and
             $rollbackTarget.Service -ceq $before.Service -and
             $rollbackTarget.DriverInfPath -ceq $before.DriverInfPath -and
             $rollbackTarget.DriverVersion -ceq $before.DriverVersion -and
             $rollbackTarget.DriverProvider -ceq $before.DriverProvider -and
             @($rollbackTarget.HardwareIds|Where-Object {$_ -ceq $script:H15cExactHwid}).Count -eq 1 -and
             @($rollbackTarget.CompoundUpperFilters|Where-Object {$_ -ceq $script:H15cService}).Count -eq 0)

        if($safeToDropTrust){
            if($publisherAdded){
                $cr=Invoke-H15cCertUtil @('-delstore','TrustedPublisher',$package.CertificateThumbprint)
                $rollbackLog.Add("CERT_DELETE TrustedPublisher EXIT=$($cr.ExitCode)")
                $rollbackLog.Add($cr.Output)
                if($cr.ExitCode -eq 0){$publisherAdded=$false}
            }
            if($rootAdded){
                $cr=Invoke-H15cCertUtil @('-delstore','Root',$package.CertificateThumbprint)
                $rollbackLog.Add("CERT_DELETE Root EXIT=$($cr.ExitCode)")
                $rollbackLog.Add($cr.Output)
                if($cr.ExitCode -eq 0){$rootAdded=$false}
            }
        } else {
            $rollbackLog.Add('TRUST_RETAINED_FOR_SAFETY=TRUE')
            $rollbackLog.Add('Reason: filter package/device state was not proven restored; retaining signer trust avoids making a remaining test driver unloadable on the next start.')
        }

        $rollbackLog|Set-Content (Join-Path $runDir 'emergency_rollback.txt') -Encoding UTF8
    }
}

$final=$null
try{$final=Get-H15cTargetState}catch{}
$remaining=@(Get-H15cPublishedInf)
$finalTrust=Get-H15cCertificatePresence $package.CertificateThumbprint
$baselineRestored=$false
if($final){
    $baselineRestored=($final.ProblemCode -eq 0 -and $final.Status -ceq 'OK' -and
        $final.InstanceId -ceq $before.InstanceId -and
        $final.Service -ceq $before.Service -and
        $final.DriverInfPath -ceq $before.DriverInfPath -and
        $final.DriverVersion -ceq $before.DriverVersion -and
        $final.DriverProvider -ceq $before.DriverProvider -and
        $remaining.Count -eq 0 -and
        @($final.CompoundUpperFilters|Where-Object {$_ -ceq $script:H15cService}).Count -eq 0 -and
        -not $finalTrust.Root -and -not $finalTrust.TrustedPublisher)
}
if($final){$final|ConvertTo-Json -Depth 8|Set-Content (Join-Path $runDir 'target_final.json') -Encoding UTF8}
$finalTrust|ConvertTo-Json -Depth 4|Set-Content (Join-Path $runDir 'trust_final.json') -Encoding UTF8

$result=[ordered]@{
    Status=$(if($normalRollbackComplete -and $baselineRestored -and -not $transactionError){
        'H15C_LIVE_R2_CAPTURE_AND_ROLLBACK_COMPLETE'
    }else{'H15C_LIVE_R2_TRANSACTION_FAILED'})
    PublishedInf=$publishedInf
    CaptureCompleted=$captureCompleted
    CompoundUpperFilterObserved=$compoundFilterObserved
    BaselineRestored=$baselineRestored
    TrustRestored=(-not $finalTrust.Root -and -not $finalTrust.TrustedPublisher)
    TransactionError=$(if($transactionError){$transactionError.Message}else{$null})
    BaseService=$before.Service;BaseInf=$before.DriverInfPath;BaseVersion=$before.DriverVersion
    BaseProvider=$before.DriverProvider
    TestSignAllowed=$ci.TestSignAllowed
    PackageCertificateThumbprint=$package.CertificateThumbprint
    SystemReboot='NO';BcdWrite='NO'
    TrustChange='TEMPORARY_LOCALMACHINE_ROOT_AND_TRUSTEDPUBLISHER'
    RegistryWrite='PNP_AND_CERT_STORES_TRANSACTIONAL'
    PciConfigWrite='NO';Mmio='NO';DspBoot='NO';AudioPlayback='NO'
    DeviceRestarts='TARGET_DEV3198_ONLY'
}
$result|ConvertTo-Json -Depth 8|Set-Content (Join-Path $runDir 'transaction.json') -Encoding UTF8
Write-H15cHashes $runDir
$zip=Join-Path $OutputRoot ('RESULT_H15C_LIVE_R2_TRANSACTION_'+$stamp+'_'+$suffix+'.zip')
Compress-Archive -Path (Join-Path $runDir '*') -DestinationPath $zip -Force

Write-Host "STATUS=$($result.Status)"
Write-Host "CAPTURE_COMPLETED=$($captureCompleted.ToString().ToUpperInvariant())"
Write-Host "BASELINE_RESTORED=$($baselineRestored.ToString().ToUpperInvariant())"
Write-Host "TRUST_RESTORED=$($result.TrustRestored.ToString().ToUpperInvariant())"
Write-Host "PUBLISHED_INF=$publishedInf"
Write-Host "Trimite fisierul: $zip"
if($transactionError){throw $transactionError}
if(-not $normalRollbackComplete -or -not $baselineRestored){exit 3}
