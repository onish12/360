#requires -Version 5.1
param([string]$OutputRoot='',[switch]$SelfTest)
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

function Assert-PnpReadOnlyArguments([string[]]$Arguments) {
    if($null -eq $Arguments -or $Arguments.Count -lt 3 -or $Arguments[0] -ine '/enum-devices'){
        throw 'PNPUTIL_READONLY_ENUM_ONLY'
    }
    $allowed=@('/enum-devices','/instanceid','/deviceids','/services','/stack','/drivers','/properties','/resources','/connected')
    foreach($arg in $Arguments){
        if($arg.StartsWith('/') -and $arg.ToLowerInvariant() -notin $allowed){
            throw "PNPUTIL_ARGUMENT_NOT_ALLOWED: $arg"
        }
    }
    $i=[Array]::IndexOf($Arguments,'/instanceid')
    if($i -lt 0 -or $i+1 -ge $Arguments.Count){throw 'PNPUTIL_INSTANCE_REQUIRED'}
    if($Arguments[$i+1] -notlike 'PCI\VEN_8086&DEV_3198*'){throw 'PNPUTIL_TARGET_NOT_DEV3198'}
}

function Invoke-PnpReadOnly([string]$PnPUtil,[string[]]$Arguments,[string]$Destination){
    Assert-PnpReadOnlyArguments $Arguments
    $old=$ErrorActionPreference
    $ErrorActionPreference='Continue'
    try{$text=(& $PnPUtil @Arguments 2>&1|Out-String -Width 8192);$code=$LASTEXITCODE}
    finally{$ErrorActionPreference=$old}
    @('COMMAND=pnputil.exe '+($Arguments -join ' '),"EXIT=$code",'--- OUTPUT ---',$text) |
        Set-Content -LiteralPath $Destination -Encoding UTF8
    [pscustomobject]@{ExitCode=$code;Output=$text}
}

function Initialize-CfgMgrReader {
    if('Phaser360.ReadOnlyCfgMgr' -as [type]){return}
    $source=@'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Phaser360 {
    public sealed class CfgResourceRecord {
        public uint ResourceId;
        public byte[] Data;
    }

    public static class ReadOnlyCfgMgr {
        public const uint CR_SUCCESS = 0x00;
        public const uint CR_NO_MORE_RES_DES = 0x0f;
        public const uint ALLOC_LOG_CONF = 0x00000002;
        public const uint ResType_All = 0x00000000;
        public const uint ResType_IRQ = 0x00000004;

        [DllImport("cfgmgr32.dll", CharSet=CharSet.Unicode)]
        private static extern uint CM_Locate_DevNodeW(out uint devInst,string deviceId,uint flags);
        [DllImport("cfgmgr32.dll")]
        private static extern uint CM_Get_First_Log_Conf(out UIntPtr logConf,uint devInst,uint flags);
        [DllImport("cfgmgr32.dll")]
        private static extern uint CM_Get_Next_Res_Des(out UIntPtr next,UIntPtr current,uint forResource,out uint resourceId,uint flags);
        [DllImport("cfgmgr32.dll")]
        private static extern uint CM_Get_Res_Des_Data_Size(out uint size,UIntPtr resDes,uint flags);
        [DllImport("cfgmgr32.dll")]
        private static extern uint CM_Get_Res_Des_Data(UIntPtr resDes,byte[] buffer,uint bufferLen,uint flags);
        [DllImport("cfgmgr32.dll")]
        private static extern uint CM_Free_Res_Des_Handle(UIntPtr resDes);
        [DllImport("cfgmgr32.dll")]
        private static extern uint CM_Free_Log_Conf_Handle(UIntPtr logConf);

        private static void Check(uint cr,string name){
            if(cr!=CR_SUCCESS) throw new InvalidOperationException(name+" CR=0x"+cr.ToString("X8"));
        }

        public static CfgResourceRecord[] ReadAllocatedResources(string instanceId){
            if(IntPtr.Size!=8) throw new InvalidOperationException("CFGMGR_X64_PROCESS_REQUIRED");
            uint devInst;
            Check(CM_Locate_DevNodeW(out devInst,instanceId,0),"CM_Locate_DevNodeW");

            UIntPtr logConf;
            Check(CM_Get_First_Log_Conf(out logConf,devInst,ALLOC_LOG_CONF),"CM_Get_First_Log_Conf(ALLOC_LOG_CONF)");

            var records=new List<CfgResourceRecord>();
            UIntPtr cursor=logConf;
            UIntPtr held=UIntPtr.Zero;
            try{
                for(int guard=0;guard<128;++guard){
                    UIntPtr next;
                    uint resourceId;
                    uint cr=CM_Get_Next_Res_Des(out next,cursor,ResType_All,out resourceId,0);
                    if(cr==CR_NO_MORE_RES_DES) break;
                    Check(cr,"CM_Get_Next_Res_Des");

                    if(held!=UIntPtr.Zero){
                        Check(CM_Free_Res_Des_Handle(held),"CM_Free_Res_Des_Handle(previous)");
                        held=UIntPtr.Zero;
                    }
                    held=next;
                    cursor=next;

                    uint size;
                    Check(CM_Get_Res_Des_Data_Size(out size,next,0),"CM_Get_Res_Des_Data_Size");
                    if(size==0 || size>(1024u*1024u))
                        throw new InvalidOperationException("CFGMGR_RESOURCE_SIZE_INVALID="+size);

                    var data=new byte[size];
                    Check(CM_Get_Res_Des_Data(next,data,size,0),"CM_Get_Res_Des_Data");
                    records.Add(new CfgResourceRecord{ResourceId=resourceId,Data=data});

                    if(guard==127) throw new InvalidOperationException("CFGMGR_RESOURCE_GUARD_EXHAUSTED");
                }
            }
            finally{
                if(held!=UIntPtr.Zero) CM_Free_Res_Des_Handle(held);
                CM_Free_Log_Conf_Handle(logConf);
            }
            return records.ToArray();
        }
    }
}
'@
    Add-Type -TypeDefinition $source -Language CSharp -ErrorAction Stop
}

function Get-PhaserTargetState {
    $devices=@(Get-PnpDevice -PresentOnly -ErrorAction Stop|Where-Object {$_.InstanceId -like 'PCI\VEN_8086&DEV_3198*'})
    if($devices.Count -ne 1){throw "EXPECTED_ONE_PRESENT_DEV3198: count=$($devices.Count)"}
    $id=[string]$devices[0].InstanceId
    $properties=@(Get-PnpDeviceProperty -InstanceId $id -ErrorAction Stop)
    $map=@{};foreach($p in $properties){$map[[string]$p.KeyName]=$p.Data}
    if(-not $map.ContainsKey('DEVPKEY_Device_HardwareIds') -or -not $map.ContainsKey('DEVPKEY_Device_ProblemCode')){
        throw 'REQUIRED_DEVICE_PROPERTY_MISSING'
    }
    [pscustomobject]@{
        InstanceId=$id;Status=[string]$devices[0].Status;Class=[string]$devices[0].Class
        ProblemCode=[int]$map['DEVPKEY_Device_ProblemCode'];Service=[string]$map['DEVPKEY_Device_Service']
        DriverInfPath=[string]$map['DEVPKEY_Device_DriverInfPath'];DriverVersion=[string]$map['DEVPKEY_Device_DriverVersion']
        DriverProvider=[string]$map['DEVPKEY_Device_DriverProvider'];HardwareIds=@($map['DEVPKEY_Device_HardwareIds'])
    }
}

function Test-StableState($Before,$After){
    foreach($name in @('InstanceId','ProblemCode','Service','DriverInfPath','DriverVersion','DriverProvider')){
        if([string]$Before.$name -cne [string]$After.$name){return $false}
    }
    return $true
}

function Test-IsAdministrator {
    $id=[Security.Principal.WindowsIdentity]::GetCurrent()
    $p=[Security.Principal.WindowsPrincipal]::new($id)
    $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Write-Hashes([string]$Directory){
    $sum=Join-Path $Directory 'SHA256SUMS.txt'
    Get-ChildItem -LiteralPath $Directory -File|Where-Object {$_.Name -ne 'SHA256SUMS.txt'}|Sort-Object Name|ForEach-Object {
        (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+$_.Name
    }|Set-Content -LiteralPath $sum -Encoding ASCII
}

function Save-CfgMgrEvidence([object[]]$Records,[string]$RunDir){
    $summary=@()
    $irqCount=0
    for($i=0;$i -lt $Records.Count;$i++){
        $r=$Records[$i]
        $rid=[uint32]$r.ResourceId
        [byte[]]$data=$r.Data
        if($rid -eq [Phaser360.ReadOnlyCfgMgr]::ResType_IRQ){$irqCount++}
        $name=('cfgmgr_res_{0:D3}_type_{1:X8}.bin' -f $i,$rid)
        [IO.File]::WriteAllBytes((Join-Path $RunDir $name),$data)
        $summary += [pscustomobject]@{
            Index=$i;ResourceId=$rid;Bytes=$data.Length
            Kind=$(if($rid -eq 4){'IRQ_RESOURCE_SIGNALING_UNDETERMINED'}else{'OTHER'})
            RawSha256=(Get-FileHash -LiteralPath (Join-Path $RunDir $name) -Algorithm SHA256).Hash.ToLowerInvariant()
        }
    }
    $summary|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $RunDir 'cfgmgr_alloc_resources.json') -Encoding UTF8
    [pscustomobject]@{RecordCount=$Records.Count;IrqCount=$irqCount}
}

if($SelfTest){
    Initialize-CfgMgrReader
    if([Phaser360.ReadOnlyCfgMgr]::ALLOC_LOG_CONF -ne 2 -or
       [Phaser360.ReadOnlyCfgMgr]::ResType_All -ne 0 -or
       [Phaser360.ReadOnlyCfgMgr]::ResType_IRQ -ne 4 -or
       [Phaser360.ReadOnlyCfgMgr]::CR_NO_MORE_RES_DES -ne 0x0f){
        throw 'SELFTEST_CFGMGR_CONSTANTS'
    }
    $fixture='PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06\FIXTURE'
    Assert-PnpReadOnlyArguments @('/enum-devices','/instanceid',$fixture,'/resources')
    $rejected=0
    foreach($bad in @(
        @('/restart-device',$fixture),
        @('/enum-devices','/deviceid','PCI\VEN_8086&DEV_3198','/resources'),
        @('/enum-devices','/instanceid','PCI\VEN_1234&DEV_5678\X','/resources'),
        @('/enum-devices','/instanceid',$fixture,'/enable-device')
    )){
        try{Assert-PnpReadOnlyArguments $bad}catch{$rejected++}
    }
    if($rejected -ne 4){throw "SELFTEST_REJECTION_COUNT=$rejected"}

    $fake=@(
        [pscustomobject]@{ResourceId=[uint32]1;Data=[byte[]](1,2,3,4)},
        [pscustomobject]@{ResourceId=[uint32]4;Data=[byte[]](5,6,7,8,9)}
    )
    $tmp=Join-Path ([IO.Path]::GetTempPath()) ('phaser_irq_selftest_'+[Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $tmp|Out-Null
    try{
        $s=Save-CfgMgrEvidence $fake $tmp
        if($s.RecordCount -ne 2 -or $s.IrqCount -ne 1){throw 'SELFTEST_CFGMGR_SERIALIZATION'}
    }finally{Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue}
    Write-Host 'IRQ_CAPTURE_SELFTEST=PASS; cfgmgr_alloc_log_conf=YES; cfgmgr_readonly=YES; irq_signal_type=UNDETERMINED; mutation_commands=REJECTED'
    return
}

if(-not (Test-IsAdministrator)){throw 'ADMINISTRATOR_REQUIRED_FOR_READONLY_ENUMERATION'}
if(-not [Environment]::Is64BitProcess){throw 'WINDOWS_X64_PROCESS_REQUIRED'}
$build=[Environment]::OSVersion.Version.Build
if($build -lt 19044){throw "WINDOWS_10_21H2_OR_NEWER_REQUIRED: build=$build"}

$before=Get-PhaserTargetState
$id=$before.InstanceId
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss'
$suffix=[Guid]::NewGuid().ToString('N').Substring(0,8)
if([string]::IsNullOrWhiteSpace($OutputRoot)){$OutputRoot=$PSScriptRoot}
$runDir=Join-Path $OutputRoot ('IRQ_EVIDENCE_'+$stamp+'_'+$suffix)
New-Item -ItemType Directory -Path $runDir -Force|Out-Null
$before|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $runDir 'target_before.json') -Encoding UTF8

$allProperties=@(Get-PnpDeviceProperty -InstanceId $id -ErrorAction Stop|ForEach-Object {
    [pscustomobject]@{KeyName=[string]$_.KeyName;Type=[string]$_.Type;Data=$_.Data}
})
$allProperties|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $runDir 'device_properties.json') -Encoding UTF8

$captureComplete=$false;$capturePath='';$irqCount=0;$recordCount=0
$resourceExit='NOT_RUN';$fullExit='NOT_RUN'

if($build -ge 22621){
    $capturePath='PNPUTIL_RESOURCES'
    $pnp=Join-Path $env:SystemRoot 'System32\pnputil.exe'
    if(-not (Test-Path -LiteralPath $pnp -PathType Leaf)){throw 'PNPUTIL_NOT_FOUND'}
    $resource=Invoke-PnpReadOnly $pnp @('/enum-devices','/instanceid',$id,'/resources') (Join-Path $runDir 'pnputil_resources.txt')
    $full=Invoke-PnpReadOnly $pnp @('/enum-devices','/instanceid',$id,'/deviceids','/services','/stack','/drivers','/properties','/resources') (Join-Path $runDir 'pnputil_full.txt')
    $resourceExit=[string]$resource.ExitCode;$fullExit=[string]$full.ExitCode
    $captureComplete=($resource.ExitCode -eq 0 -and $full.ExitCode -eq 0 -and
        -not [string]::IsNullOrWhiteSpace($resource.Output) -and -not [string]::IsNullOrWhiteSpace($full.Output))
}else{
    $capturePath='CFGMGR32_ALLOC_LOG_CONF'
    Initialize-CfgMgrReader
    $records=@([Phaser360.ReadOnlyCfgMgr]::ReadAllocatedResources($id))
    $saved=Save-CfgMgrEvidence $records $runDir
    $recordCount=[int]$saved.RecordCount;$irqCount=[int]$saved.IrqCount
    $captureComplete=($recordCount -gt 0 -and $irqCount -gt 0)
}

$after=Get-PhaserTargetState
$after|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $runDir 'target_after.json') -Encoding UTF8
$stable=Test-StableState $before $after
$status=if($captureComplete -and $stable){'CAPTURE_COMPLETE_STABLE'}elseif(-not $stable){'STATE_CHANGED_DURING_CAPTURE'}else{'CAPTURE_PARTIAL'}

@(
    "STATUS=$status","WINDOWS_BUILD=$build","CAPTURE_PATH=$capturePath","TARGET_INSTANCE=$id",
    "TARGET_PROBLEM_BEFORE=$($before.ProblemCode)","TARGET_PROBLEM_AFTER=$($after.ProblemCode)",
    "TARGET_SERVICE_BEFORE=$($before.Service)","TARGET_SERVICE_AFTER=$($after.Service)",
    "TARGET_INF_BEFORE=$($before.DriverInfPath)","TARGET_INF_AFTER=$($after.DriverInfPath)",
    "PNPUTIL_RESOURCES_EXIT=$resourceExit","PNPUTIL_FULL_EXIT=$fullExit",
    "CFGMGR_RECORD_COUNT=$recordCount","CFGMGR_IRQ_RESOURCE_COUNT=$irqCount",
    'CFGMGR_IRQ_SIGNALING=UNDETERMINED_BY_THIS_API',
    "STATE_STABLE=$($stable.ToString().ToUpperInvariant())",
    'MODE=READ_ONLY_ENUMERATION','DRIVER_INSTALL=NO','DRIVER_BIND_UNBIND=NO',
    'DEVICE_RESTART=NO','DEVICE_ENABLE_DISABLE=NO','REGISTRY_WRITE=NO',
    'SETUPAPI_WRITE=NO','CFGMGR_WRITE=NO','MMIO=NO','DSP_BOOT=NO',
    'WDF_INTERRUPT_CREATE=NO','IRQ_SELECTION=DEFERRED','AUDIO_PLAYBACK=NO'
)|Set-Content -LiteralPath (Join-Path $runDir 'RESULT.txt') -Encoding UTF8

Write-Hashes $runDir
$zip=Join-Path $OutputRoot ('RESULT_IRQ_READONLY_'+$stamp+'_'+$suffix+'.zip')
Compress-Archive -Path (Join-Path $runDir '*') -DestinationPath $zip -Force
Write-Host "STATUS=$status"
Write-Host "CAPTURE_PATH=$capturePath"
Write-Host "TARGET=$id"
if($capturePath -eq 'CFGMGR32_ALLOC_LOG_CONF'){
    Write-Host "RESOURCE_RECORDS=$recordCount"
    Write-Host "IRQ_RESOURCES=$irqCount"
    Write-Host 'IRQ_SIGNALING=UNDETERMINED_BY_THIS_API'
}
Write-Host "Trimite fisierul: $zip"
if($status -ne 'CAPTURE_COMPLETE_STABLE'){exit 2}
