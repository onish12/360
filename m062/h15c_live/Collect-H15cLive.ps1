#requires -Version 5.1
param([string]$OutputRoot='')
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$InterfaceGuid=[Guid]'8c1b3150-6d0c-4c88-9d36-15c000319801'
$Ioctl=[Convert]::ToUInt32('83376454',16)
$SnapshotBytes=292

function Test-IsAdministrator {
    $id=[Security.Principal.WindowsIdentity]::GetCurrent()
    $p=[Security.Principal.WindowsPrincipal]::new($id)
    $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-TargetState {
    $devices=@(Get-PnpDevice -PresentOnly -ErrorAction Stop|Where-Object {$_.InstanceId -like 'PCI\VEN_8086&DEV_3198*'})
    if($devices.Count -ne 1){throw "EXPECTED_ONE_PRESENT_DEV3198: count=$($devices.Count)"}
    $id=[string]$devices[0].InstanceId
    $properties=@(Get-PnpDeviceProperty -InstanceId $id -ErrorAction Stop)
    $map=@{};foreach($p in $properties){$map[[string]$p.KeyName]=$p.Data}
    [pscustomobject]@{
        InstanceId=$id
        Status=[string]$devices[0].Status
        ProblemCode=[int]$map['DEVPKEY_Device_ProblemCode']
        Service=[string]$map['DEVPKEY_Device_Service']
        DriverInfPath=[string]$map['DEVPKEY_Device_DriverInfPath']
        DriverVersion=[string]$map['DEVPKEY_Device_DriverVersion']
        DriverProvider=[string]$map['DEVPKEY_Device_DriverProvider']
        HardwareIds=@($map['DEVPKEY_Device_HardwareIds'])
    }
}

function Test-StableState($Before,$After){
    foreach($name in @('InstanceId','ProblemCode','Service','DriverInfPath','DriverVersion','DriverProvider')){
        if([string]$Before.$name -cne [string]$After.$name){return $false}
    }
    return $true
}

function Initialize-H15cLiveReader {
    if('Phaser360.H15cLiveNative' -as [type]){return}
    $source=@'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;

namespace Phaser360 {
    public static class H15cLiveNative {
        const uint DIGCF_PRESENT=0x00000002;
        const uint DIGCF_DEVICEINTERFACE=0x00000010;
        const uint GENERIC_READ=0x80000000;
        const uint FILE_SHARE_READ=0x00000001;
        const uint FILE_SHARE_WRITE=0x00000002;
        const uint OPEN_EXISTING=3;
        const int ERROR_NO_MORE_ITEMS=259;
        static readonly IntPtr INVALID_HANDLE_VALUE=new IntPtr(-1);

        [StructLayout(LayoutKind.Sequential)]
        struct SP_DEVICE_INTERFACE_DATA {
            public int cbSize;
            public Guid InterfaceClassGuid;
            public int Flags;
            public UIntPtr Reserved;
        }

        [DllImport("setupapi.dll",SetLastError=true)]
        static extern IntPtr SetupDiGetClassDevs(
            ref Guid ClassGuid,IntPtr Enumerator,IntPtr hwndParent,uint Flags);
        [DllImport("setupapi.dll",SetLastError=true)]
        static extern bool SetupDiEnumDeviceInterfaces(
            IntPtr DeviceInfoSet,IntPtr DeviceInfoData,ref Guid InterfaceClassGuid,
            uint MemberIndex,ref SP_DEVICE_INTERFACE_DATA DeviceInterfaceData);
        [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)]
        static extern bool SetupDiGetDeviceInterfaceDetail(
            IntPtr DeviceInfoSet,ref SP_DEVICE_INTERFACE_DATA DeviceInterfaceData,
            IntPtr DeviceInterfaceDetailData,uint DeviceInterfaceDetailDataSize,
            out uint RequiredSize,IntPtr DeviceInfoData);
        [DllImport("setupapi.dll",SetLastError=true)]
        static extern bool SetupDiDestroyDeviceInfoList(IntPtr DeviceInfoSet);
        [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
        static extern SafeFileHandle CreateFile(
            string FileName,uint DesiredAccess,uint ShareMode,IntPtr SecurityAttributes,
            uint CreationDisposition,uint FlagsAndAttributes,IntPtr TemplateFile);
        [DllImport("kernel32.dll",SetLastError=true)]
        static extern bool DeviceIoControl(
            SafeFileHandle Device,uint IoControlCode,
            IntPtr InputBuffer,uint InputBufferSize,
            byte[] OutputBuffer,uint OutputBufferSize,
            out uint BytesReturned,IntPtr Overlapped);

        static void Win32(bool ok,string op) {
            if(!ok) {
                int error=Marshal.GetLastWin32Error();
                throw new Win32Exception(error,op+"; WIN32_ERROR="+error);
            }
        }

        public static string GetSingleInterfacePath(Guid interfaceGuid) {
            if(IntPtr.Size!=8) throw new InvalidOperationException("H15C_LIVE_X64_PROCESS_REQUIRED");
            IntPtr set=SetupDiGetClassDevs(
                ref interfaceGuid,IntPtr.Zero,IntPtr.Zero,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
            if(set==INVALID_HANDLE_VALUE) throw new Win32Exception(Marshal.GetLastWin32Error(),"SetupDiGetClassDevs");
            try {
                var data=new SP_DEVICE_INTERFACE_DATA();
                data.cbSize=Marshal.SizeOf(typeof(SP_DEVICE_INTERFACE_DATA));
                Win32(SetupDiEnumDeviceInterfaces(set,IntPtr.Zero,ref interfaceGuid,0,ref data),
                      "SetupDiEnumDeviceInterfaces[0]");

                var second=new SP_DEVICE_INTERFACE_DATA();
                second.cbSize=Marshal.SizeOf(typeof(SP_DEVICE_INTERFACE_DATA));
                bool secondOk=SetupDiEnumDeviceInterfaces(set,IntPtr.Zero,ref interfaceGuid,1,ref second);
                int secondError=Marshal.GetLastWin32Error();
                if(secondOk || secondError!=ERROR_NO_MORE_ITEMS)
                    throw new InvalidOperationException("H15C_LIVE_EXPECTED_EXACTLY_ONE_INTERFACE");

                uint required=0;
                SetupDiGetDeviceInterfaceDetail(set,ref data,IntPtr.Zero,0,out required,IntPtr.Zero);
                if(required<10) throw new InvalidOperationException("H15C_LIVE_DETAIL_SIZE_INVALID");
                IntPtr detail=Marshal.AllocHGlobal((int)required);
                try {
                    for(int i=0;i<required;i++) Marshal.WriteByte(detail,i,0);
                    // On x64 the Unicode structure's cbSize value is 8 because
                    // sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W)==8, but the
                    // variable-length DevicePath field begins immediately
                    // after the DWORD cbSize at byte offset 4.
                    const int DetailCbSizeX64=8;
                    const int DevicePathOffset=4;
                    Marshal.WriteInt32(detail,0,DetailCbSizeX64);
                    Win32(SetupDiGetDeviceInterfaceDetail(
                        set,ref data,detail,required,out required,IntPtr.Zero),
                        "SetupDiGetDeviceInterfaceDetail");
                    string path=Marshal.PtrToStringUni(IntPtr.Add(detail,DevicePathOffset));
                    if(String.IsNullOrWhiteSpace(path))
                        throw new InvalidOperationException("H15C_LIVE_INTERFACE_PATH_EMPTY");
                    if(!path.StartsWith(@"\\?\",StringComparison.Ordinal))
                        throw new InvalidOperationException(
                            "H15C_LIVE_INTERFACE_PATH_PREFIX_INVALID: "+path);
                    return path;
                } finally { Marshal.FreeHGlobal(detail); }
            } finally { SetupDiDestroyDeviceInfoList(set); }
        }

        public static byte[] Query(Guid interfaceGuid,uint ioctl,int expectedBytes) {
            string path=GetSingleInterfacePath(interfaceGuid);
            using(var handle=CreateFile(
                path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,IntPtr.Zero,
                OPEN_EXISTING,0,IntPtr.Zero)) {
                if(handle.IsInvalid) {
                    int error=Marshal.GetLastWin32Error();
                    throw new Win32Exception(
                        error,"CreateFile(H15C live interface); WIN32_ERROR="+error+
                        "; ACCESS=GENERIC_READ; PATH="+path);
                }
                var output=new byte[expectedBytes];
                uint returned;
                Win32(DeviceIoControl(
                    handle,ioctl,IntPtr.Zero,0,output,(uint)output.Length,
                    out returned,IntPtr.Zero),"DeviceIoControl(H15C snapshot)");
                if(returned!=expectedBytes)
                    throw new InvalidOperationException("H15C_LIVE_SNAPSHOT_SIZE="+returned);
                return output;
            }
        }
    }
}
'@
    Add-Type -TypeDefinition $source -Language CSharp -ErrorAction Stop
}

function Read-U16([byte[]]$b,[int]$o){
    [uint16]($b[$o] -bor ($b[$o+1] -shl 8))
}
function Read-U32([byte[]]$b,[int]$o){
    [uint32]($b[$o] -bor ($b[$o+1] -shl 8) -bor ($b[$o+2] -shl 16) -bor ($b[$o+3] -shl 24))
}
function Read-I32([byte[]]$b,[int]$o){
    [BitConverter]::ToInt32($b,$o)
}
function Write-Hashes([string]$Directory){
    Get-ChildItem -LiteralPath $Directory -File|Where-Object {$_.Name -ne 'SHA256SUMS.txt'}|
        Sort-Object Name|ForEach-Object {
            (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+$_.Name
        }|Set-Content -LiteralPath (Join-Path $Directory 'SHA256SUMS.txt') -Encoding ASCII
}

if(-not (Test-IsAdministrator)){throw 'ADMINISTRATOR_REQUIRED'}
if(-not [Environment]::Is64BitProcess){throw 'WINDOWS_X64_PROCESS_REQUIRED'}
if([Environment]::OSVersion.Version.Build -ne 19044){
    throw "WINDOWS_BUILD_EXACT_19044_REQUIRED: build=$([Environment]::OSVersion.Version.Build)"
}

$before=Get-TargetState
if($before.ProblemCode -ne 0 -or $before.Status -ne 'OK'){throw 'TARGET_NOT_HEALTHY_BEFORE_CAPTURE'}
if($before.Service -eq 'Phaser360M1'){throw 'M1_FUNCTION_DRIVER_MUST_NOT_BE_BOUND_FOR_H15C_LIVE'}
Initialize-H15cLiveReader

$bytes=[Phaser360.H15cLiveNative]::Query($InterfaceGuid,$Ioctl,$SnapshotBytes)
$version=Read-U32 $bytes 0
$size=Read-U32 $bytes 4
$captureStatus=Read-I32 $bytes 8
$flags=Read-U32 $bytes 12
$generation=Read-U32 $bytes 16
$vendor=Read-U16 $bytes 20
$device=Read-U16 $bytes 22
$header=[uint32]$bytes[24]
$firstCapability=[uint32]$bytes[25]
$capabilityCount=[uint32]$bytes[26]
$pgctl=Read-U32 $bytes 28
$cgctl=Read-U32 $bytes 32
$config=[byte[]]::new(256)
[Array]::Copy($bytes,36,$config,0,256)

if($version -ne 1 -or $size -ne $SnapshotBytes){
    $head36=([BitConverter]::ToString($bytes,0,36)).Replace('-','')
    throw ("H15C_LIVE_ABI_MISMATCH: version=$version size=$size captureStatus=0x{0:X8} flags=0x{1:X8} generation=$generation head36=$head36 ioctl=0x{2:X8}" -f ([uint32]$captureStatus),$flags,$Ioctl)
}
if($captureStatus -lt 0){throw ('H15C_LIVE_CAPTURE_NTSTATUS=0x{0:X8}' -f ([uint32]$captureStatus))}
if(($flags -band 0x0f) -ne 0x0f){throw ('H15C_LIVE_FLAGS_INVALID=0x{0:X8}' -f $flags)}
if($vendor -ne 0x8086 -or $device -ne 0x3198){throw 'H15C_LIVE_TARGET_ID_MISMATCH'}
if(($header -band 0x7f) -ne 0 -or $firstCapability -lt 0x50 -or ($firstCapability -band 3)){
    throw 'H15C_LIVE_VENDOR_WINDOW_NOT_ATTESTED'
}

$after=Get-TargetState
$stable=Test-StableState $before $after
if(-not $stable){throw 'TARGET_STATE_CHANGED_DURING_H15C_LIVE_CAPTURE'}

if([string]::IsNullOrWhiteSpace($OutputRoot)){$OutputRoot=$PSScriptRoot}
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss'
$suffix=[Guid]::NewGuid().ToString('N').Substring(0,8)
$runDir=Join-Path $OutputRoot ('H15C_LIVE_'+$stamp+'_'+$suffix)
New-Item -ItemType Directory -Path $runDir -Force|Out-Null
[IO.File]::WriteAllBytes((Join-Path $runDir 'pci_config_256.bin'),$config)
$before|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $runDir 'target_before.json') -Encoding UTF8
$after|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $runDir 'target_after.json') -Encoding UTF8

$snapshot=[ordered]@{
    Schema=1;Version=$version;Size=$size;CaptureStatus=$captureStatus
    Flags=('0x{0:X8}' -f $flags);Generation=$generation
    VendorId=('0x{0:X4}' -f $vendor);DeviceId=('0x{0:X4}' -f $device)
    HeaderType=('0x{0:X2}' -f $header);FirstCapability=('0x{0:X2}' -f $firstCapability)
    CapabilityCount=$capabilityCount
    Offset44=('0x{0:X8}' -f $pgctl);Offset48=('0x{0:X8}' -f $cgctl)
    ConfigSha256=(Get-FileHash -LiteralPath (Join-Path $runDir 'pci_config_256.bin') -Algorithm SHA256).Hash.ToLowerInvariant()
    TargetStateStable=$stable
    Mode='READ_ONLY_FILTER_IOCTL'
    SetBusDataCalls=0
    Mmio='NO'
    DspBoot='NO'
}
$snapshot|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $runDir 'snapshot.json') -Encoding UTF8
@(
    'STATUS=CAPTURE_COMPLETE_STABLE',
    "TARGET=$($before.InstanceId)",
    "VENDOR=0x$('{0:X4}' -f $vendor)",
    "DEVICE=0x$('{0:X4}' -f $device)",
    "FIRST_CAPABILITY=0x$('{0:X2}' -f $firstCapability)",
    "CAPABILITY_COUNT=$capabilityCount",
    "OFFSET_44=0x$('{0:X8}' -f $pgctl)",
    "OFFSET_48=0x$('{0:X8}' -f $cgctl)",
    'PCI_CONFIG_ACCESS=GETBUSDATA_ONLY',
    'SETBUSDATA=NO','MMIO=NO','DSP_BOOT=NO','AUDIO_PLAYBACK=NO',
    'DRIVER_BIND_REPLACEMENT=NO','DEVICE_RESTART_BY_COLLECTOR=NO'
)|Set-Content -LiteralPath (Join-Path $runDir 'RESULT.txt') -Encoding UTF8
Write-Hashes $runDir

$zip=Join-Path $OutputRoot ('RESULT_H15C_LIVE_'+$stamp+'_'+$suffix+'.zip')
Compress-Archive -Path (Join-Path $runDir '*') -DestinationPath $zip -Force
Write-Host 'STATUS=CAPTURE_COMPLETE_STABLE'
Write-Host ('OFFSET_44=0x{0:X8}' -f $pgctl)
Write-Host ('OFFSET_48=0x{0:X8}' -f $cgctl)
Write-Host "Trimite fisierul: $zip"
