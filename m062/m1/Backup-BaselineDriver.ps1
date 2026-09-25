#requires -Version 5.1
param([string]$OutputRoot='')
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

function Test-IsAdministrator {
    $id=[Security.Principal.WindowsIdentity]::GetCurrent()
    $p=[Security.Principal.WindowsPrincipal]::new($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
function Write-Hashes([string]$Directory){
    Get-ChildItem -LiteralPath $Directory -Recurse -File |
      Where-Object {$_.Name -ne 'SHA256SUMS.txt'} |
      Sort-Object FullName |
      ForEach-Object {
        (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()+
        '  '+$_.FullName.Substring($Directory.Length).TrimStart('\')
      } | Set-Content -LiteralPath (Join-Path $Directory 'SHA256SUMS.txt') -Encoding ASCII
}

if(-not (Test-IsAdministrator)){throw 'ADMINISTRATOR_REQUIRED'}
if(-not [Environment]::Is64BitProcess){throw 'WINDOWS_X64_PROCESS_REQUIRED'}
if([Environment]::OSVersion.Version.Build -ne 19044){throw 'EXACT_WINDOWS_BUILD_19044_REQUIRED'}

$devices=@(Get-PnpDevice -PresentOnly -ErrorAction Stop |
  Where-Object {$_.InstanceId -like 'PCI\VEN_8086&DEV_3198*'})
if($devices.Count -ne 1){throw "EXPECTED_ONE_PRESENT_DEV3198: count=$($devices.Count)"}
$id=[string]$devices[0].InstanceId
$props=@(Get-PnpDeviceProperty -InstanceId $id -ErrorAction Stop)
$map=@{}; foreach($p in $props){$map[[string]$p.KeyName]=$p.Data}
$exact='PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06'
$ids=@($map['DEVPKEY_Device_HardwareIds'])
if(@($ids|Where-Object {$_ -ceq $exact}).Count -ne 1){throw 'EXACT_HARDWARE_ID_NOT_PRESENT'}
if([int]$map['DEVPKEY_Device_ProblemCode'] -ne 0 -or [string]$devices[0].Status -cne 'OK'){
    throw 'TARGET_NOT_HEALTHY'
}
$currentInf=[string]$map['DEVPKEY_Device_DriverInfPath']
$currentService=[string]$map['DEVPKEY_Device_Service']
if([string]::IsNullOrWhiteSpace($currentInf) -or [string]::IsNullOrWhiteSpace($currentService)){
    throw 'CURRENT_DRIVER_IDENTITY_MISSING'
}
if($currentService -ceq 'Phaser360M1'){throw 'EXPERIMENTAL_DRIVER_ALREADY_BOUND'}

if([string]::IsNullOrWhiteSpace($OutputRoot)){$OutputRoot=$PSScriptRoot}
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss'
$root=Join-Path $OutputRoot ("M1_BASELINE_BACKUP_"+$stamp)
$export=Join-Path $root 'driver_export'
New-Item -ItemType Directory -Path $export -Force|Out-Null

$pnputil=Join-Path $env:SystemRoot 'System32\pnputil.exe'
if(-not (Test-Path -LiteralPath $pnputil -PathType Leaf)){throw 'PNPUTIL_NOT_FOUND'}
& $pnputil /export-driver $currentInf $export
if($LASTEXITCODE -ne 0){throw "PNPUTIL_EXPORT_FAILED: $LASTEXITCODE"}

$exported=@(Get-ChildItem -LiteralPath $export -Recurse -File)
if($exported.Count -eq 0){throw 'BASELINE_EXPORT_EMPTY'}

$result=[ordered]@{
    Status='M1_BASELINE_BACKUP_COMPLETE'
    WindowsBuild=19044
    InstanceId=$id
    CurrentService=$currentService
    CurrentInf=$currentInf
    CurrentDriverVersion=[string]$map['DEVPKEY_Device_DriverVersion']
    CurrentDriverProvider=[string]$map['DEVPKEY_Device_DriverProvider']
    ExactHardwareId=$exact
    ExportedFileCount=$exported.Count
    DriverInstall='NO'
    DriverUninstall='NO'
    DeviceRestart='NO'
    DeviceEnableDisable='NO'
    RegistryWrite='NO'
    BcdWrite='NO'
    Reboot='NO'
    MMIO='NO'
    DSPBoot='NO'
    AudioPlayback='NO'
}
$result|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $root 'baseline_backup.json') -Encoding UTF8
Write-Hashes $root
$zip=Join-Path $OutputRoot ("RESULT_M1_BASELINE_BACKUP_"+$stamp+'.zip')
Compress-Archive -Path (Join-Path $root '*') -DestinationPath $zip -Force
Write-Host 'STATUS=M1_BASELINE_BACKUP_COMPLETE'
Write-Host "CURRENT_SERVICE=$currentService"
Write-Host "CURRENT_INF=$currentInf"
Write-Host "EXPORTED_FILES=$($exported.Count)"
Write-Host 'SYSTEM_MUTATION=NONE'
Write-Host "Trimite fisierul: $zip"
