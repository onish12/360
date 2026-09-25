#requires -Version 5.1
param([string]$OutputRoot='',[switch]$SelfTest)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

function Read-TargetContract {
    $path=Join-Path $PSScriptRoot 'target_contract.json'
    if(-not (Test-Path -LiteralPath $path -PathType Leaf)){throw 'TARGET_CONTRACT_MISSING'}
    $c=Get-Content -LiteralPath $path -Raw -Encoding UTF8|ConvertFrom-Json
    if([int]$c.schema -ne 1 -or [string]$c.milestone -cne 'M0.6.15H12'){throw 'TARGET_CONTRACT_SCHEMA_INVALID'}
    if([int]$c.windows_build_exact -ne 19044){throw 'TARGET_CONTRACT_BUILD_INVALID'}
    if([string]$c.controller_hardware_id -cne 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06'){throw 'TARGET_CONTRACT_HWID_INVALID'}
    if([int64]$c.hda_bar_bytes -ne 0x4000 -or [int64]$c.dsp_bar_bytes -ne 0x100000){throw 'TARGET_CONTRACT_BAR_LENGTH_INVALID'}
    if([int]$c.pci_interrupt_support -ne 3 -or [int]$c.pci_interrupt_message_maximum -ne 1){throw 'TARGET_CONTRACT_IRQ_INVALID'}
    if([int]$c.nhlt_bytes -ne 3684 -or [string]$c.nhlt_sha256 -cne '4764aba0316e9a039a127285cc4ff9e97e22c75bfddb6d865d9f77b59dd2a6b9'){throw 'TARGET_CONTRACT_NHLT_INVALID'}
    if([int]$c.firmware_bytes -ne 287488 -or [string]$c.firmware_sha256 -cne '40029b5a05665f19a492ef00b8c0a24c42e90d7c00fc57146e07947fd1407d5c'){throw 'TARGET_CONTRACT_FIRMWARE_INVALID'}
    if([bool]$c.speaker_amplifier_enable_authorized -or [bool]$c.codec_programming_authorized -or
       [bool]$c.audio_playback_authorized -or [bool]$c.automatic_reboot_authorized){
        throw 'TARGET_CONTRACT_SAFETY_HOLD_INVALID'
    }
    return $c
}

function Test-IsAdministrator {
    $id=[Security.Principal.WindowsIdentity]::GetCurrent()
    $p=[Security.Principal.WindowsPrincipal]::new($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-TargetState {
    $devices=@(Get-PnpDevice -PresentOnly -ErrorAction Stop|Where-Object {$_.InstanceId -like 'PCI\VEN_8086&DEV_3198*'})
    if($devices.Count -ne 1){throw "EXPECTED_ONE_PRESENT_DEV3198: count=$($devices.Count)"}
    $id=[string]$devices[0].InstanceId
    $props=@(Get-PnpDeviceProperty -InstanceId $id -ErrorAction Stop)
    $map=@{};foreach($p in $props){$map[[string]$p.KeyName]=$p.Data}
    foreach($required in @(
        'DEVPKEY_Device_HardwareIds','DEVPKEY_Device_ProblemCode',
        'DEVPKEY_Device_Service','DEVPKEY_Device_DriverInfPath',
        'DEVPKEY_Device_DriverVersion','DEVPKEY_Device_DriverProvider',
        'DEVPKEY_PciDevice_InterruptSupport','DEVPKEY_PciDevice_InterruptMessageMaximum'
    )){
        if(-not $map.ContainsKey($required)){throw "TARGET_PROPERTY_MISSING: $required"}
    }
    [pscustomobject]@{
        InstanceId=$id
        Status=[string]$devices[0].Status
        Class=[string]$devices[0].Class
        ProblemCode=[int]$map['DEVPKEY_Device_ProblemCode']
        Service=[string]$map['DEVPKEY_Device_Service']
        DriverInfPath=[string]$map['DEVPKEY_Device_DriverInfPath']
        DriverVersion=[string]$map['DEVPKEY_Device_DriverVersion']
        DriverProvider=[string]$map['DEVPKEY_Device_DriverProvider']
        HardwareIds=@($map['DEVPKEY_Device_HardwareIds'])
        InterruptSupport=[int]$map['DEVPKEY_PciDevice_InterruptSupport']
        InterruptMessageMaximum=[int]$map['DEVPKEY_PciDevice_InterruptMessageMaximum']
    }
}

function Test-TargetAgainstContract($State,$Contract){
    $reasons=New-Object System.Collections.Generic.List[string]
    if($State.ProblemCode -ne 0){$reasons.Add("PROBLEM_CODE=$($State.ProblemCode)")}
    if($State.Status -cne 'OK'){$reasons.Add("STATUS=$($State.Status)")}
    if(@($State.HardwareIds|Where-Object {$_ -ceq [string]$Contract.controller_hardware_id}).Count -ne 1){
        $reasons.Add('EXACT_HARDWARE_ID_NOT_PRESENT')
    }
    if($State.InterruptSupport -ne [int]$Contract.pci_interrupt_support){
        $reasons.Add("INTERRUPT_SUPPORT=$($State.InterruptSupport)")
    }
    if($State.InterruptMessageMaximum -ne [int]$Contract.pci_interrupt_message_maximum){
        $reasons.Add("INTERRUPT_MESSAGE_MAXIMUM=$($State.InterruptMessageMaximum)")
    }
    if([string]::IsNullOrWhiteSpace($State.Service)){$reasons.Add('BOUND_SERVICE_MISSING')}
    if([string]::IsNullOrWhiteSpace($State.DriverInfPath)){$reasons.Add('BOUND_INF_MISSING')}
    if($State.Service -ceq [string]$Contract.future_service_name){$reasons.Add('EXPERIMENTAL_SERVICE_ALREADY_BOUND')}
    [pscustomobject]@{Pass=($reasons.Count -eq 0);Reasons=@($reasons)}
}

function Parse-ReAgentStatus([string]$Text){
    if([string]::IsNullOrWhiteSpace($Text)){return 'UNKNOWN'}
    # Known English/Romanian forms; unknown localization fails closed.
    if($Text -match '(?im)Windows\s+RE.*(?:Enabled|Activat)'){return 'ENABLED'}
    if($Text -match '(?im)Windows\s+RE.*(?:Disabled|Dezactivat)'){return 'DISABLED'}
    return 'UNKNOWN'
}

function Invoke-ReAgentInfo {
    $exe=Join-Path $env:SystemRoot 'System32\reagentc.exe'
    if(-not (Test-Path -LiteralPath $exe -PathType Leaf)){throw 'REAGENTC_NOT_FOUND'}
    $old=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try{$text=(& $exe /info 2>&1|Out-String -Width 8192);$code=$LASTEXITCODE}
    finally{$ErrorActionPreference=$old}
    [pscustomobject]@{ExitCode=$code;Output=$text;Status=(Parse-ReAgentStatus $text)}
}

function Write-Hashes([string]$Directory){
    $sum=Join-Path $Directory 'SHA256SUMS.txt'
    Get-ChildItem -LiteralPath $Directory -File|Where-Object {$_.Name -ne 'SHA256SUMS.txt'}|Sort-Object Name|ForEach-Object {
        (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+$_.Name
    }|Set-Content -LiteralPath $sum -Encoding ASCII
}

if($SelfTest){
    $c=Read-TargetContract
    $good=[pscustomobject]@{
        Status='OK';ProblemCode=0;Service='IntcAudioBus';DriverInfPath='oem14.inf'
        DriverVersion='9.22.0.4832';DriverProvider='Intel(R) Corporation'
        HardwareIds=@([string]$c.controller_hardware_id);InterruptSupport=3;InterruptMessageMaximum=1
    }
    $a=Test-TargetAgainstContract $good $c
    if(-not $a.Pass -or $a.Reasons.Count -ne 0){throw 'SELFTEST_GOOD_TARGET_REJECTED'}
    $bad=$good.PSObject.Copy();$bad.InterruptMessageMaximum=4
    $b=Test-TargetAgainstContract $bad $c
    if($b.Pass -or $b.Reasons -notcontains 'INTERRUPT_MESSAGE_MAXIMUM=4'){throw 'SELFTEST_BAD_TARGET_ACCEPTED'}
    if((Parse-ReAgentStatus "Windows RE status:         Enabled") -cne 'ENABLED'){throw 'SELFTEST_REAGENT_EN'}
    if((Parse-ReAgentStatus "Stare Windows RE:          Activat") -cne 'ENABLED'){throw 'SELFTEST_REAGENT_RO'}
    if((Parse-ReAgentStatus "Windows RE status:         Disabled") -cne 'DISABLED'){throw 'SELFTEST_REAGENT_DISABLED'}
    if((Parse-ReAgentStatus 'localized unknown') -cne 'UNKNOWN'){throw 'SELFTEST_REAGENT_UNKNOWN'}
    Write-Host 'H12_M1_PREFLIGHT_SELFTEST=PASS; exact_target=YES; winre_parser=FAIL_CLOSED; mutations=NONE'
    return
}

if(-not (Test-IsAdministrator)){throw 'ADMINISTRATOR_REQUIRED_FOR_COMPLETE_READONLY_PREFLIGHT'}
if(-not [Environment]::Is64BitProcess){throw 'WINDOWS_X64_PROCESS_REQUIRED'}

$c=Read-TargetContract
$build=[Environment]::OSVersion.Version.Build
$buildOk=($build -eq [int]$c.windows_build_exact)
$state=Get-TargetState
$target=Test-TargetAgainstContract $state $c
$re=Invoke-ReAgentInfo
$winreOk=($re.ExitCode -eq 0 -and $re.Status -ceq 'ENABLED')

$ready=$buildOk -and $target.Pass -and $winreOk
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss'
$suffix=[Guid]::NewGuid().ToString('N').Substring(0,8)
if([string]::IsNullOrWhiteSpace($OutputRoot)){$OutputRoot=$PSScriptRoot}
$runDir=Join-Path $OutputRoot ('M1_PREFLIGHT_'+$stamp+'_'+$suffix)
New-Item -ItemType Directory -Path $runDir -Force|Out-Null

$c|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $runDir 'target_contract.json') -Encoding UTF8
$state|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $runDir 'target_state.json') -Encoding UTF8
$re.Output|Set-Content -LiteralPath (Join-Path $runDir 'reagentc_info.txt') -Encoding UTF8

$result=[ordered]@{
    Status=$(if($ready){'M1_PREFLIGHT_READY'}else{'M1_PREFLIGHT_BLOCKED'})
    WindowsBuild=$build
    WindowsBuildExpected=[int]$c.windows_build_exact
    BuildMatches=$buildOk
    TargetContractMatches=[bool]$target.Pass
    TargetMismatchReasons=@($target.Reasons)
    InstanceId=$state.InstanceId
    ProblemCode=$state.ProblemCode
    CurrentService=$state.Service
    CurrentDriverInf=$state.DriverInfPath
    CurrentDriverVersion=$state.DriverVersion
    CurrentDriverProvider=$state.DriverProvider
    InterruptSupport=$state.InterruptSupport
    InterruptMessageMaximum=$state.InterruptMessageMaximum
    WinRECommandExit=$re.ExitCode
    WinREStatus=$re.Status
    WinREReady=$winreOk
    DriverInstall='NO'
    DriverBindUnbind='NO'
    DeviceRestart='NO'
    DeviceEnableDisable='NO'
    RegistryWrite='NO'
    BcdWrite='NO'
    MMIO='NO'
    DSPBoot='NO'
    AudioPlayback='NO'
}
$result|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $runDir 'preflight.json') -Encoding UTF8
@(
    "STATUS=$($result.Status)",
    "WINDOWS_BUILD=$build",
    "WINDOWS_BUILD_EXPECTED=$($c.windows_build_exact)",
    "TARGET_MATCH=$($target.Pass.ToString().ToUpperInvariant())",
    "WINRE_STATUS=$($re.Status)",
    "WINRE_READY=$($winreOk.ToString().ToUpperInvariant())",
    "CURRENT_SERVICE=$($state.Service)",
    "CURRENT_INF=$($state.DriverInfPath)",
    "CURRENT_DRIVER_VERSION=$($state.DriverVersion)",
    'MODE=READ_ONLY_PREFLIGHT',
    'DRIVER_INSTALL=NO','DRIVER_BIND_UNBIND=NO','DEVICE_RESTART=NO',
    'DEVICE_ENABLE_DISABLE=NO','REGISTRY_WRITE=NO','BCD_WRITE=NO',
    'MMIO=NO','DSP_BOOT=NO','AUDIO_PLAYBACK=NO'
)|Set-Content -LiteralPath (Join-Path $runDir 'RESULT.txt') -Encoding UTF8

Write-Hashes $runDir
$zip=Join-Path $OutputRoot ('RESULT_M1_PREFLIGHT_'+$stamp+'_'+$suffix+'.zip')
Compress-Archive -Path (Join-Path $runDir '*') -DestinationPath $zip -Force
Write-Host "STATUS=$($result.Status)"
Write-Host "TARGET_MATCH=$($target.Pass.ToString().ToUpperInvariant())"
Write-Host "WINRE_STATUS=$($re.Status)"
Write-Host "CURRENT_SERVICE=$($state.Service)"
Write-Host "CURRENT_INF=$($state.DriverInfPath)"
Write-Host "Trimite fisierul: $zip"
if(-not $ready){exit 2}
