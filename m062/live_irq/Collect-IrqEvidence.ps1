#requires -Version 5.1
param(
    [string]$OutputRoot = '',
    [switch]$SelfTest
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2

function Assert-PnpReadOnlyArguments([string[]]$Arguments) {
    if ($null -eq $Arguments -or $Arguments.Count -lt 3 -or
        $Arguments[0] -ine '/enum-devices') {
        throw 'PNPUTIL_READONLY_ENUM_ONLY'
    }
    $allowed = @('/enum-devices','/instanceid','/deviceids','/services','/stack',
        '/drivers','/properties','/resources','/connected')
    foreach ($arg in $Arguments) {
        if ($arg.StartsWith('/')) {
            if ($arg.ToLowerInvariant() -notin $allowed) {
                throw "PNPUTIL_ARGUMENT_NOT_ALLOWED: $arg"
            }
        }
    }
    $instanceIndex = [Array]::IndexOf($Arguments, '/instanceid')
    if ($instanceIndex -lt 0 -or $instanceIndex + 1 -ge $Arguments.Count) {
        throw 'PNPUTIL_INSTANCE_REQUIRED'
    }
    if ($Arguments[$instanceIndex + 1] -notlike 'PCI\VEN_8086&DEV_3198*') {
        throw 'PNPUTIL_TARGET_NOT_DEV3198'
    }
}

function Invoke-PnpReadOnly([string]$PnPUtil,[string[]]$Arguments,[string]$Destination) {
    Assert-PnpReadOnlyArguments $Arguments
    $oldPreference = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        $text = (& $PnPUtil @Arguments 2>&1 | Out-String -Width 8192)
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $oldPreference
    }
    @(
        'COMMAND=pnputil.exe ' + ($Arguments -join ' ')
        "EXIT=$code"
        '--- OUTPUT ---'
        $text
    ) | Set-Content -LiteralPath $Destination -Encoding UTF8
    return [pscustomobject]@{ ExitCode=$code; Output=$text }
}

function Initialize-SetupApiReader {
    if ('Phaser360.ReadOnlySetupApi' -as [type]) { return }
    $source = @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;

namespace Phaser360 {
    public static class ReadOnlySetupApi {
        private const uint DIGCF_PRESENT = 0x00000002;
        private const uint DIGCF_ALLCLASSES = 0x00000004;
        public const uint SPDRP_ALLOC_CONFIG = 0x00000003;
        private const int ERROR_NO_MORE_ITEMS = 259;
        private const int ERROR_INSUFFICIENT_BUFFER = 122;
        private static readonly IntPtr INVALID_HANDLE_VALUE = new IntPtr(-1);

        [StructLayout(LayoutKind.Sequential)]
        private struct SP_DEVINFO_DATA {
            public uint cbSize;
            public Guid ClassGuid;
            public uint DevInst;
            public IntPtr Reserved;
        }

        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr SetupDiGetClassDevsW(
            IntPtr ClassGuid, string Enumerator, IntPtr hwndParent, uint Flags);

        [DllImport("setupapi.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetupDiEnumDeviceInfo(
            IntPtr DeviceInfoSet, uint MemberIndex, ref SP_DEVINFO_DATA DeviceInfoData);

        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetupDiGetDeviceInstanceIdW(
            IntPtr DeviceInfoSet, ref SP_DEVINFO_DATA DeviceInfoData,
            StringBuilder DeviceInstanceId, uint DeviceInstanceIdSize, out uint RequiredSize);

        [DllImport("setupapi.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetupDiGetDeviceRegistryPropertyW(
            IntPtr DeviceInfoSet, ref SP_DEVINFO_DATA DeviceInfoData, uint Property,
            out uint PropertyRegDataType, byte[] PropertyBuffer, uint PropertyBufferSize,
            out uint RequiredSize);

        [DllImport("setupapi.dll", SetLastError = true)]
        [return: MarshalAs(UnmanagedType.Bool)]
        private static extern bool SetupDiDestroyDeviceInfoList(IntPtr DeviceInfoSet);

        // WDK defines CM_PARTIAL_RESOURCE_DESCRIPTOR under pshpack4.h.
        // Derive the ABI sizes/offsets through Pack=4 mirrors rather than
        // duplicating parser constants in PowerShell.
        [StructLayout(LayoutKind.Sequential, Pack = 4)]
        private struct CM_INTERRUPT_LAYOUT {
            public ushort Level;
            public ushort Group;
            public uint Vector;
            public UIntPtr Affinity;
        }

        [StructLayout(LayoutKind.Explicit, Pack = 4)]
        private struct CM_RESOURCE_UNION_LAYOUT {
            [FieldOffset(0)] public CM_INTERRUPT_LAYOUT Interrupt;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 4)]
        private struct CM_PARTIAL_RESOURCE_DESCRIPTOR_LAYOUT {
            public byte Type;
            public byte ShareDisposition;
            public ushort Flags;
            public CM_RESOURCE_UNION_LAYOUT Data;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 4)]
        private struct CM_PARTIAL_RESOURCE_LIST_HEADER_LAYOUT {
            public ushort Version;
            public ushort Revision;
            public uint Count;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 4)]
        private struct CM_FULL_RESOURCE_DESCRIPTOR_HEADER_LAYOUT {
            public int InterfaceType;
            public uint BusNumber;
            public CM_PARTIAL_RESOURCE_LIST_HEADER_LAYOUT Partial;
        }

        [StructLayout(LayoutKind.Sequential, Pack = 4)]
        private struct CM_RESOURCE_LIST_HEADER_LAYOUT {
            public uint Count;
            public CM_FULL_RESOURCE_DESCRIPTOR_HEADER_LAYOUT First;
        }

        public static int PartialDescriptorSize {
            get { return Marshal.SizeOf(typeof(CM_PARTIAL_RESOURCE_DESCRIPTOR_LAYOUT)); }
        }
        public static int PartialUnionOffset {
            get { return (int)Marshal.OffsetOf(typeof(CM_PARTIAL_RESOURCE_DESCRIPTOR_LAYOUT), "Data"); }
        }
        public static int ResourceListFirstFullOffset {
            get { return (int)Marshal.OffsetOf(typeof(CM_RESOURCE_LIST_HEADER_LAYOUT), "First"); }
        }
        public static int FullDescriptorHeaderSize {
            get { return Marshal.SizeOf(typeof(CM_FULL_RESOURCE_DESCRIPTOR_HEADER_LAYOUT)); }
        }

        public static byte[] ReadAllocatedConfig(string instanceId, out uint regType) {
            if (IntPtr.Size != 8) throw new InvalidOperationException("SETUPAPI_X64_PROCESS_REQUIRED");
            IntPtr set = SetupDiGetClassDevsW(IntPtr.Zero, null, IntPtr.Zero,
                                              DIGCF_PRESENT | DIGCF_ALLCLASSES);
            if (set == INVALID_HANDLE_VALUE)
                throw new Win32Exception(Marshal.GetLastWin32Error(), "SetupDiGetClassDevsW");

            try {
                for (uint index = 0; ; ++index) {
                    SP_DEVINFO_DATA data = new SP_DEVINFO_DATA();
                    data.cbSize = (uint)Marshal.SizeOf(typeof(SP_DEVINFO_DATA));
                    if (!SetupDiEnumDeviceInfo(set, index, ref data)) {
                        int error = Marshal.GetLastWin32Error();
                        if (error == ERROR_NO_MORE_ITEMS) break;
                        throw new Win32Exception(error, "SetupDiEnumDeviceInfo");
                    }

                    StringBuilder id = new StringBuilder(512);
                    uint needed;
                    if (!SetupDiGetDeviceInstanceIdW(set, ref data, id, (uint)id.Capacity, out needed))
                        throw new Win32Exception(Marshal.GetLastWin32Error(), "SetupDiGetDeviceInstanceIdW");

                    if (!String.Equals(id.ToString(), instanceId, StringComparison.OrdinalIgnoreCase))
                        continue;

                    uint required;
                    uint type;
                    bool first = SetupDiGetDeviceRegistryPropertyW(
                        set, ref data, SPDRP_ALLOC_CONFIG, out type, null, 0, out required);
                    int firstError = Marshal.GetLastWin32Error();
                    if (first || required == 0 || firstError != ERROR_INSUFFICIENT_BUFFER)
                        throw new Win32Exception(firstError, "SPDRP_ALLOC_CONFIG size query");

                    byte[] buffer = new byte[required];
                    if (!SetupDiGetDeviceRegistryPropertyW(
                            set, ref data, SPDRP_ALLOC_CONFIG, out type,
                            buffer, (uint)buffer.Length, out required))
                        throw new Win32Exception(Marshal.GetLastWin32Error(), "SPDRP_ALLOC_CONFIG read");

                    regType = type;
                    return buffer;
                }
                throw new InvalidOperationException("SETUPAPI_TARGET_DEV3198_NOT_FOUND");
            }
            finally {
                SetupDiDestroyDeviceInfoList(set);
            }
        }
    }
}
'@
    Add-Type -TypeDefinition $source -Language CSharp -ErrorAction Stop
}

function Assert-Range([byte[]]$Bytes,[int]$Offset,[int]$Length,[string]$Label) {
    if ($Offset -lt 0 -or $Length -lt 0 -or $Offset -gt $Bytes.Length -or
        $Length -gt ($Bytes.Length - $Offset)) {
        throw "CM_RESOURCE_LIST_TRUNCATED: $Label offset=$Offset length=$Length bytes=$($Bytes.Length)"
    }
}

function Read-U16([byte[]]$Bytes,[int]$Offset) {
    Assert-Range $Bytes $Offset 2 'u16'
    return [BitConverter]::ToUInt16($Bytes,$Offset)
}
function Read-U32([byte[]]$Bytes,[int]$Offset) {
    Assert-Range $Bytes $Offset 4 'u32'
    return [BitConverter]::ToUInt32($Bytes,$Offset)
}
function Read-U64([byte[]]$Bytes,[int]$Offset) {
    Assert-Range $Bytes $Offset 8 'u64'
    return [BitConverter]::ToUInt64($Bytes,$Offset)
}
function Get-HexSlice([byte[]]$Bytes,[int]$Offset,[int]$Length) {
    Assert-Range $Bytes $Offset $Length 'hex'
    return [BitConverter]::ToString($Bytes,$Offset,$Length).Replace('-','')
}

function Convert-CmResourceList([byte[]]$Bytes) {
    if ($null -eq $Bytes -or $Bytes.Length -lt 20) { throw 'CM_RESOURCE_LIST_TOO_SMALL' }
    if (-not [Environment]::Is64BitProcess) { throw 'CM_RESOURCE_LIST_X64_PROCESS_REQUIRED' }
    Initialize-SetupApiReader

    $descriptorBytes = [Phaser360.ReadOnlySetupApi]::PartialDescriptorSize
    $unionOffset = [Phaser360.ReadOnlySetupApi]::PartialUnionOffset
    $firstFullOffset = [Phaser360.ReadOnlySetupApi]::ResourceListFirstFullOffset
    $fullHeaderBytes = [Phaser360.ReadOnlySetupApi]::FullDescriptorHeaderSize
    if ($descriptorBytes -ne 20 -or $unionOffset -ne 4 -or
        $firstFullOffset -ne 4 -or $fullHeaderBytes -ne 16) {
        throw "CM_RESOURCE_ABI_UNEXPECTED: descriptor=$descriptorBytes union=$unionOffset firstFull=$firstFullOffset fullHeader=$fullHeaderBytes"
    }

    $fullCount = [int](Read-U32 $Bytes 0)
    if ($fullCount -lt 1 -or $fullCount -gt 8) {
        throw "CM_RESOURCE_LIST_FULL_COUNT_INVALID: $fullCount"
    }

    $offset = $firstFullOffset
    $all = @()
    $interrupts = @()
    for ($fullIndex=0; $fullIndex -lt $fullCount; $fullIndex++) {
        Assert-Range $Bytes $offset $fullHeaderBytes "full[$fullIndex]"
        $interfaceType = [int](Read-U32 $Bytes $offset)
        $busNumber = [uint32](Read-U32 $Bytes ($offset+4))
        $version = [uint16](Read-U16 $Bytes ($offset+8))
        $revision = [uint16](Read-U16 $Bytes ($offset+10))
        $partialCount = [int](Read-U32 $Bytes ($offset+12))
        if ($partialCount -lt 0 -or $partialCount -gt 64) {
            throw "CM_RESOURCE_LIST_PARTIAL_COUNT_INVALID: $partialCount"
        }

        $descriptorOffset = $offset + $fullHeaderBytes
        $lastDeviceSpecificBytes = 0
        for ($i=0; $i -lt $partialCount; $i++) {
            Assert-Range $Bytes $descriptorOffset $descriptorBytes "descriptor[$fullIndex][$i]"
            $type = [byte]$Bytes[$descriptorOffset]
            $share = [byte]$Bytes[$descriptorOffset+1]
            $flags = [uint16](Read-U16 $Bytes ($descriptorOffset+2))
            $entry = [ordered]@{
                FullIndex = $fullIndex
                Index = $i
                Type = [int]$type
                ShareDisposition = [int]$share
                Flags = ('0x{0:X4}' -f $flags)
                RawDescriptorHex = Get-HexSlice $Bytes $descriptorOffset $descriptorBytes
            }

            if ($type -eq 2) {
                $message = (($flags -band 0x0002) -ne 0) # CM_RESOURCE_INTERRUPT_MESSAGE
                $irq = [ordered]@{
                    FullIndex = $fullIndex
                    Index = $i
                    Kind = $(if ($message) { 'MESSAGE' } else { 'LINE' })
                    ShareDisposition = [int]$share
                    Flags = ('0x{0:X4}' -f $flags)
                    MessageFlagSet = $message
                    UnionHex = Get-HexSlice $Bytes ($descriptorOffset+$unionOffset) ($descriptorBytes-$unionOffset)
                }
                $interrupts += [pscustomobject]$irq
                $entry.InterruptKind = $irq.Kind
            }

            # If the final descriptor is DeviceSpecific, DataSize is the first
            # ULONG in the packed union and its payload follows the descriptor array.
            if ($type -eq 5) {
                $lastDeviceSpecificBytes = [int](Read-U32 $Bytes ($descriptorOffset+$unionOffset))
            }
            $all += [pscustomobject]$entry
            $descriptorOffset += $descriptorBytes
        }

        $all += [pscustomobject][ordered]@{
            FullIndex = $fullIndex
            InterfaceType = $interfaceType
            BusNumber = $busNumber
            Version = $version
            Revision = $revision
            PartialCount = $partialCount
            Header = $true
        }
        $offset = $descriptorOffset + $lastDeviceSpecificBytes
        if ($offset -gt $Bytes.Length) { throw 'CM_RESOURCE_LIST_DEVICE_SPECIFIC_OVERFLOW' }
    }

    [pscustomobject]@{
        Source = 'SPDRP_ALLOC_CONFIG'
        Architecture = 'x64'
        Packing = 4
        DescriptorBytes = $descriptorBytes
        UnionOffset = $unionOffset
        FirstFullOffset = $firstFullOffset
        FullHeaderBytes = $fullHeaderBytes
        FullDescriptorCount = $fullCount
        InterruptCount = @($interrupts).Count
        InterruptKinds = @($interrupts | ForEach-Object Kind)
        Interrupts = @($interrupts)
        Records = @($all)
        BufferBytes = $Bytes.Length
    }
}

function Get-Win10AllocatedResourceEvidence([string]$InstanceId,[string]$RunDir) {
    Initialize-SetupApiReader
    [uint32]$regType = 0
    $bytes = [Phaser360.ReadOnlySetupApi]::ReadAllocatedConfig($InstanceId,[ref]$regType)
    if ($null -eq $bytes -or $bytes.Length -eq 0) { throw 'SPDRP_ALLOC_CONFIG_EMPTY' }
    # REG_RESOURCE_LIST is 8. Reject another registry type rather than guessing.
    if ($regType -ne 8) { throw "SPDRP_ALLOC_CONFIG_REGTYPE_UNEXPECTED: $regType" }

    [IO.File]::WriteAllBytes((Join-Path $RunDir 'setupapi_alloc_config.bin'),$bytes)
    $parsed = Convert-CmResourceList $bytes
    $parsed | ConvertTo-Json -Depth 10 |
        Set-Content -LiteralPath (Join-Path $RunDir 'setupapi_alloc_config.json') -Encoding UTF8
    return $parsed
}

function Get-PhaserTargetState {
    $devices = @(Get-PnpDevice -PresentOnly -ErrorAction Stop |
        Where-Object { $_.InstanceId -like 'PCI\VEN_8086&DEV_3198*' })
    if ($devices.Count -ne 1) { throw "EXPECTED_ONE_PRESENT_DEV3198: count=$($devices.Count)" }

    $id = [string]$devices[0].InstanceId
    $properties = @(Get-PnpDeviceProperty -InstanceId $id -ErrorAction Stop)
    $map = @{}
    foreach ($p in $properties) { $map[[string]$p.KeyName] = $p.Data }

    if (-not $map.ContainsKey('DEVPKEY_Device_HardwareIds') -or
        -not $map.ContainsKey('DEVPKEY_Device_ProblemCode')) {
        throw 'REQUIRED_DEVICE_PROPERTY_MISSING'
    }

    [pscustomobject]@{
        InstanceId = $id
        Status = [string]$devices[0].Status
        Class = [string]$devices[0].Class
        ProblemCode = [int]$map['DEVPKEY_Device_ProblemCode']
        Service = [string]$map['DEVPKEY_Device_Service']
        DriverInfPath = [string]$map['DEVPKEY_Device_DriverInfPath']
        DriverVersion = [string]$map['DEVPKEY_Device_DriverVersion']
        DriverProvider = [string]$map['DEVPKEY_Device_DriverProvider']
        HardwareIds = @($map['DEVPKEY_Device_HardwareIds'])
    }
}

function Test-StableState($Before,$After) {
    foreach ($name in @('InstanceId','ProblemCode','Service','DriverInfPath',
                        'DriverVersion','DriverProvider')) {
        if ([string]$Before.$name -cne [string]$After.$name) { return $false }
    }
    return $true
}

function Test-IsAdministrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Write-Hashes([string]$Directory) {
    $sum = Join-Path $Directory 'SHA256SUMS.txt'
    Get-ChildItem -LiteralPath $Directory -File |
        Where-Object { $_.Name -ne 'SHA256SUMS.txt' } |
        Sort-Object Name |
        ForEach-Object {
            $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            "$hash  $($_.Name)"
        } | Set-Content -LiteralPath $sum -Encoding ASCII
}

if ($SelfTest) {
    Initialize-SetupApiReader
    if ([Phaser360.ReadOnlySetupApi]::SPDRP_ALLOC_CONFIG -ne 3) {
        throw 'SELFTEST_SPDRP_ALLOC_CONFIG_CONSTANT'
    }

    $fixture = 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06\FIXTURE'
    Assert-PnpReadOnlyArguments @('/enum-devices','/instanceid',$fixture,'/resources')
    Assert-PnpReadOnlyArguments @('/enum-devices','/instanceid',$fixture,
        '/deviceids','/services','/stack','/drivers','/properties','/resources')
    $rejected = 0
    foreach ($bad in @(
        @('/restart-device',$fixture),
        @('/enum-devices','/deviceid','PCI\VEN_8086&DEV_3198','/resources'),
        @('/enum-devices','/instanceid','PCI\VEN_1234&DEV_5678\X','/resources'),
        @('/enum-devices','/instanceid',$fixture,'/enable-device')
    )) {
        try { Assert-PnpReadOnlyArguments $bad }
        catch { $rejected++ }
    }
    if ($rejected -ne 4) { throw "SELFTEST_REJECTION_COUNT=$rejected" }

    $descriptorBytes = [Phaser360.ReadOnlySetupApi]::PartialDescriptorSize
    $unionOffset = [Phaser360.ReadOnlySetupApi]::PartialUnionOffset
    $firstFullOffset = [Phaser360.ReadOnlySetupApi]::ResourceListFirstFullOffset
    $fullHeaderBytes = [Phaser360.ReadOnlySetupApi]::FullDescriptorHeaderSize
    if ($descriptorBytes -ne 20 -or $unionOffset -ne 4 -or
        $firstFullOffset -ne 4 -or $fullHeaderBytes -ne 16) {
        throw "SELFTEST_CM_RESOURCE_ABI: descriptor=$descriptorBytes union=$unionOffset firstFull=$firstFullOffset fullHeader=$fullHeaderBytes"
    }

    # Synthetic packed-4 x64 CM_RESOURCE_LIST: Count(4), full header(16),
    # two 20-byte partial descriptors. No live enumeration in SelfTest.
    [byte[]]$cm = New-Object byte[] 60
    [BitConverter]::GetBytes([uint32]1).CopyTo($cm,0)
    [BitConverter]::GetBytes([uint32]5).CopyTo($cm,4)
    [BitConverter]::GetBytes([uint32]0).CopyTo($cm,8)
    [BitConverter]::GetBytes([uint16]1).CopyTo($cm,12)
    [BitConverter]::GetBytes([uint16]1).CopyTo($cm,14)
    [BitConverter]::GetBytes([uint32]2).CopyTo($cm,16)
    $cm[20]=2; $cm[21]=3
    [BitConverter]::GetBytes([uint16]0).CopyTo($cm,22)
    $cm[40]=2; $cm[41]=3
    [BitConverter]::GetBytes([uint16]2).CopyTo($cm,42)
    $parsed = Convert-CmResourceList $cm
    if ($parsed.DescriptorBytes -ne 20 -or $parsed.Packing -ne 4 -or
        $parsed.InterruptCount -ne 2 -or $parsed.InterruptKinds[0] -ne 'LINE' -or
        $parsed.InterruptKinds[1] -ne 'MESSAGE') {
        throw 'SELFTEST_CM_RESOURCE_LIST_CLASSIFICATION'
    }

    Write-Host 'IRQ_CAPTURE_SELFTEST=PASS; cm_pack4=PASS; descriptor20=PASS; readonly_setupapi=YES; readonly_pnputil=YES; line_and_message=PASS; mutation_commands=REJECTED'
    return
}

if (-not (Test-IsAdministrator)) { throw 'ADMINISTRATOR_REQUIRED_FOR_READONLY_ENUMERATION' }
if (-not [Environment]::Is64BitProcess) { throw 'WINDOWS_X64_PROCESS_REQUIRED' }
$build = [Environment]::OSVersion.Version.Build
if ($build -lt 19044) {
    throw "WINDOWS_10_21H2_OR_NEWER_REQUIRED: build=$build"
}

$before = Get-PhaserTargetState
$id = $before.InstanceId
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$suffix = [Guid]::NewGuid().ToString('N').Substring(0,8)
if ([string]::IsNullOrWhiteSpace($OutputRoot)) { $OutputRoot = $PSScriptRoot }
$runDir = Join-Path $OutputRoot ('IRQ_EVIDENCE_' + $stamp + '_' + $suffix)
New-Item -ItemType Directory -Path $runDir -Force | Out-Null

$before | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $runDir 'target_before.json') -Encoding UTF8

$allProperties = @(Get-PnpDeviceProperty -InstanceId $id -ErrorAction Stop |
    ForEach-Object {
        [pscustomobject]@{
            KeyName = [string]$_.KeyName
            Type = [string]$_.Type
            Data = $_.Data
        }
    })
$allProperties | ConvertTo-Json -Depth 10 |
    Set-Content -LiteralPath (Join-Path $runDir 'device_properties.json') -Encoding UTF8

$capturePath = ''
$captureComplete = $false
$irqCount = 0
$irqKinds = ''
$resourceExit = 'NOT_RUN'
$fullExit = 'NOT_RUN'

if ($build -ge 22621) {
    $capturePath = 'PNPUTIL_RESOURCES'
    $pnp = Join-Path $env:SystemRoot 'System32\pnputil.exe'
    if (-not (Test-Path -LiteralPath $pnp -PathType Leaf)) { throw 'PNPUTIL_NOT_FOUND' }

    $resourceArgs = @('/enum-devices','/instanceid',$id,'/resources')
    $fullArgs = @('/enum-devices','/instanceid',$id,
        '/deviceids','/services','/stack','/drivers','/properties','/resources')
    $resource = Invoke-PnpReadOnly $pnp $resourceArgs (Join-Path $runDir 'pnputil_resources.txt')
    $full = Invoke-PnpReadOnly $pnp $fullArgs (Join-Path $runDir 'pnputil_full.txt')
    $resourceExit = [string]$resource.ExitCode
    $fullExit = [string]$full.ExitCode
    $captureComplete = ($resource.ExitCode -eq 0 -and $full.ExitCode -eq 0 -and
        -not [string]::IsNullOrWhiteSpace($resource.Output) -and
        -not [string]::IsNullOrWhiteSpace($full.Output))
}
else {
    $capturePath = 'SETUPAPI_SPDRP_ALLOC_CONFIG'
    $allocated = Get-Win10AllocatedResourceEvidence $id $runDir
    $irqCount = [int]$allocated.InterruptCount
    $irqKinds = (@($allocated.InterruptKinds) -join ',')
    $captureComplete = ($allocated.BufferBytes -gt 0 -and $irqCount -gt 0)
}

$after = Get-PhaserTargetState
$after | ConvertTo-Json -Depth 6 |
    Set-Content -LiteralPath (Join-Path $runDir 'target_after.json') -Encoding UTF8

$stable = Test-StableState $before $after
$status = if ($captureComplete -and $stable) { 'CAPTURE_COMPLETE_STABLE' }
          elseif (-not $stable) { 'STATE_CHANGED_DURING_CAPTURE' }
          else { 'CAPTURE_PARTIAL' }

@(
    "STATUS=$status"
    "WINDOWS_BUILD=$build"
    "CAPTURE_PATH=$capturePath"
    "TARGET_INSTANCE=$id"
    "TARGET_PROBLEM_BEFORE=$($before.ProblemCode)"
    "TARGET_PROBLEM_AFTER=$($after.ProblemCode)"
    "TARGET_SERVICE_BEFORE=$($before.Service)"
    "TARGET_SERVICE_AFTER=$($after.Service)"
    "TARGET_INF_BEFORE=$($before.DriverInfPath)"
    "TARGET_INF_AFTER=$($after.DriverInfPath)"
    "PNPUTIL_RESOURCES_EXIT=$resourceExit"
    "PNPUTIL_FULL_EXIT=$fullExit"
    "ALLOC_CONFIG_INTERRUPT_COUNT=$irqCount"
    "ALLOC_CONFIG_INTERRUPT_KINDS=$irqKinds"
    "STATE_STABLE=$($stable.ToString().ToUpperInvariant())"
    'MODE=READ_ONLY_ENUMERATION'
    'DRIVER_INSTALL=NO'
    'DRIVER_BIND_UNBIND=NO'
    'DEVICE_RESTART=NO'
    'DEVICE_ENABLE_DISABLE=NO'
    'REGISTRY_WRITE=NO'
    'SETUPAPI_WRITE=NO'
    'MMIO=NO'
    'DSP_BOOT=NO'
    'WDF_INTERRUPT_CREATE=NO'
    'IRQ_SELECTION=DEFERRED'
    'AUDIO_PLAYBACK=NO'
) | Set-Content -LiteralPath (Join-Path $runDir 'RESULT.txt') -Encoding UTF8

Write-Hashes $runDir

$zip = Join-Path $OutputRoot ('RESULT_IRQ_READONLY_' + $stamp + '_' + $suffix + '.zip')
Compress-Archive -Path (Join-Path $runDir '*') -DestinationPath $zip -Force
Write-Host "STATUS=$status"
Write-Host "CAPTURE_PATH=$capturePath"
Write-Host "TARGET=$id"
if ($capturePath -eq 'SETUPAPI_SPDRP_ALLOC_CONFIG') {
    Write-Host "IRQ_COUNT=$irqCount"
    Write-Host "IRQ_KINDS=$irqKinds"
}
Write-Host "Trimite fisierul: $zip"
if ($status -ne 'CAPTURE_COMPLETE_STABLE') { exit 2 }
