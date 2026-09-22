#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$PackageRoot,[string]$OutputRoot='')
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
. (Join-Path $PSScriptRoot 'H15cLive-Common.ps1')

if(-not (Test-H15cAdministrator)){throw 'ADMINISTRATOR_REQUIRED'}
if(-not [Environment]::Is64BitProcess){throw 'WINDOWS_X64_PROCESS_REQUIRED'}

$target=Get-H15cTargetState
Assert-H15cExactHealthyIntelTarget $target
$existing=@(Get-H15cPublishedInf)
if($existing.Count -ne 0){throw ('H15C_LIVE_FILTER_ALREADY_PRESENT: '+($existing -join ','))}

$ci=Get-H15cCodeIntegrity
$sb=Get-H15cSecureBoot
$re=Get-H15cWinRE
$package=Assert-H15cPackage $PackageRoot

$ready=$ci.TestSignAllowed -and $re.ExitCode -eq 0 -and $re.Status -ceq 'ENABLED'
$status=if($ready){'H15C_LIVE_INSTALL_READY'}else{'H15C_LIVE_INSTALL_BLOCKED'}

if([string]::IsNullOrWhiteSpace($OutputRoot)){$OutputRoot=$PSScriptRoot}
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss'
$suffix=[Guid]::NewGuid().ToString('N').Substring(0,8)
$dir=Join-Path $OutputRoot ('H15C_LIVE_PREFLIGHT_'+$stamp+'_'+$suffix)
New-Item -ItemType Directory -Path $dir -Force|Out-Null

$target|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'target.json') -Encoding UTF8
$ci|ConvertTo-Json -Depth 6|Set-Content (Join-Path $dir 'code_integrity.json') -Encoding UTF8
$sb|ConvertTo-Json -Depth 6|Set-Content (Join-Path $dir 'secure_boot.json') -Encoding UTF8
$re.Output|Set-Content (Join-Path $dir 'reagentc.txt') -Encoding UTF8
$package|ConvertTo-Json -Depth 6|Set-Content (Join-Path $dir 'package.json') -Encoding UTF8

$result=[ordered]@{
    Status=$status;WindowsBuild=[Environment]::OSVersion.Version.Build
    InstanceId=$target.InstanceId;Service=$target.Service;BaseInf=$target.DriverInfPath
    BaseVersion=$target.DriverVersion;BaseProvider=$target.DriverProvider
    CodeIntegrityOptions=$ci.Options;TestSignAllowed=$ci.TestSignAllowed
    HvciKmciEnabled=$ci.HvciKmciEnabled;SecureBootKnown=$sb.Known
    SecureBootEnabled=$sb.Enabled;WinREStatus=$re.Status
    PackageSignerThumbprint=$package.SignerThumbprint
    PackageInfSha256=$package.InfSha256;PackageSysSha256=$package.SysSha256
    PackageCatSha256=$package.CatSha256
    DriverInstall='NO';DeviceRestart='NO';TrustChange='NO';BcdWrite='NO'
    RegistryWrite='NO';Reboot='NO';MMIO='NO';PciWrite='NO';DspBoot='NO'
}
$result|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'preflight.json') -Encoding UTF8
Write-H15cHashes $dir
$zip=Join-Path $OutputRoot ('RESULT_H15C_LIVE_PREFLIGHT_'+$stamp+'_'+$suffix+'.zip')
Compress-Archive -Path (Join-Path $dir '*') -DestinationPath $zip -Force
Write-Host "STATUS=$status"
Write-Host "TESTSIGN_ALLOWED=$($ci.TestSignAllowed.ToString().ToUpperInvariant())"
Write-Host "WINRE=$($re.Status)"
Write-Host "TARGET_SERVICE=$($target.Service)"
Write-Host "Trimite fisierul: $zip"
if(-not $ready){exit 2}
