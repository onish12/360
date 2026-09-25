#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$PackageRoot,[string]$OutputRoot='')
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$ExactHwid='PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06'
$Service='Phaser360H15f'
$CertSubject='CN=PHASER360 H15F Ephemeral Test Signing'
$InterfaceGuid=[Guid]'8c1b3150-6d0f-4f88-9d36-15f000319801'
$Ioctl=[Convert]::ToUInt32('833A6460',16)
$RequiredFlags=[Convert]::ToUInt32('000003FF',16)
$SnapshotBytes=96
$ExpectedHda=[Convert]::ToUInt64('00000000CEEE0000',16)
$ExpectedDsp=[Convert]::ToUInt64('00000000CEF00000',16)
$ExpectedHdaLength=[Convert]::ToUInt32('00004000',16)
$ExpectedDspLength=[Convert]::ToUInt32('00100000',16)
$BaselineInf='oem14.inf'
$BaselineVersion='9.22.0.4832'
$BaselineProvider='Intel(R) Corporation'

function Admin {
  $id=[Security.Principal.WindowsIdentity]::GetCurrent()
  ([Security.Principal.WindowsPrincipal]::new($id)).IsInRole(
    [Security.Principal.WindowsBuiltInRole]::Administrator)
}
function Target {
  $d=@(Get-PnpDevice -PresentOnly -ErrorAction Stop |
      Where-Object {$_.InstanceId -like 'PCI\VEN_8086&DEV_3198*'})
  if($d.Count -ne 1){throw "EXPECTED_ONE_PRESENT_DEV3198: count=$($d.Count)"}
  $id=[string]$d[0].InstanceId
  $p=@(Get-PnpDeviceProperty -InstanceId $id -ErrorAction Stop)
  $m=@{}; foreach($x in $p){$m[[string]$x.KeyName]=$x.Data}
  [pscustomobject]@{
    InstanceId=$id
    Status=[string]$d[0].Status
    ProblemCode=[int]$m['DEVPKEY_Device_ProblemCode']
    Service=[string]$m['DEVPKEY_Device_Service']
    DriverInfPath=[string]$m['DEVPKEY_Device_DriverInfPath']
    DriverVersion=[string]$m['DEVPKEY_Device_DriverVersion']
    DriverProvider=[string]$m['DEVPKEY_Device_DriverProvider']
    HardwareIds=@($m['DEVPKEY_Device_HardwareIds'])
  }
}
function AssertIntelBaseline($s){
  if([Environment]::OSVersion.Version.Build -ne 19044){
    throw 'EXACT_WINDOWS_BUILD_19044_REQUIRED'
  }
  if($s.Status -cne 'OK' -or $s.ProblemCode -ne 0 -or
     $s.Service -cne 'IntcAudioBus' -or
     $s.DriverInfPath -cne $BaselineInf -or
     $s.DriverVersion -cne $BaselineVersion -or
     $s.DriverProvider -cne $BaselineProvider){
    throw 'EXACT_INTEL_BASELINE_REQUIRED'
  }
  if(@($s.HardwareIds|Where-Object {$_ -ceq $ExactHwid}).Count -ne 1){
    throw 'EXACT_HARDWARE_ID_NOT_PRESENT'
  }
}
function PnP([string[]]$a){
  $e=Join-Path $env:SystemRoot 'System32\pnputil.exe'
  $old=$ErrorActionPreference
  $ErrorActionPreference='Continue'
  try{$o=(& $e @a 2>&1|Out-String -Width 8192);$c=$LASTEXITCODE}
  finally{$ErrorActionPreference=$old}
  [pscustomobject]@{ExitCode=$c;Output=$o}
}
function CertUtil([string[]]$a){
  $e=Join-Path $env:SystemRoot 'System32\certutil.exe'
  $old=$ErrorActionPreference
  $ErrorActionPreference='Continue'
  try{$o=(& $e @a 2>&1|Out-String -Width 8192);$c=$LASTEXITCODE}
  finally{$ErrorActionPreference=$old}
  [pscustomobject]@{ExitCode=$c;Output=$o}
}
function Trust([string]$thumb){
  $t=$thumb.Replace(' ','').ToUpperInvariant()
  [pscustomobject]@{
    Root=(Test-Path "Cert:\LocalMachine\Root\$t")
    TrustedPublisher=(Test-Path "Cert:\LocalMachine\TrustedPublisher\$t")
  }
}
function Published {
  $r=@()
  foreach($f in @(Get-ChildItem (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' -File)){
    try{$t=Get-Content $f.FullName -Raw}catch{continue}
    if($t.IndexOf('Phaser360H15f',[StringComparison]::OrdinalIgnoreCase) -ge 0 -and
       $t.IndexOf($ExactHwid,[StringComparison]::OrdinalIgnoreCase) -ge 0){$r+=$f.Name}
  }
  @($r)
}
function WaitService([string]$instance,[string]$service,[int]$seconds=20){
  $end=(Get-Date).AddSeconds($seconds)
  do{
    Start-Sleep -Milliseconds 500
    try{$s=Target}catch{$s=$null}
    if($s -and $s.InstanceId -ceq $instance -and $s.Status -ceq 'OK' -and
       $s.ProblemCode -eq 0 -and $s.Service -ceq $service){return $s}
  }while((Get-Date) -lt $end)
  throw "TARGET_SERVICE_TIMEOUT: expected=$service"
}
function CodeIntegrity {
  if(-not ('Phaser360.H15fCi' -as [type])){
    Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;namespace Phaser360{public static class H15fCi{[StructLayout(LayoutKind.Sequential)]struct CI{public UInt32 Length;public UInt32 Options;}[DllImport("ntdll.dll")]static extern Int32 NtQuerySystemInformation(Int32 c,ref CI i,UInt32 l,IntPtr r);public static UInt32 Get(){CI i=new CI();i.Length=(UInt32)Marshal.SizeOf(typeof(CI));Int32 s=NtQuerySystemInformation(103,ref i,i.Length,IntPtr.Zero);if(s<0)throw new InvalidOperationException("CI=0x"+unchecked((UInt32)s).ToString("X8"));return i.Options;}}}' -Language CSharp
  }
  [uint32][Phaser360.H15fCi]::Get()
}
function Package([string]$root,[switch]$Trusted){
  $r=(Resolve-Path $root).Path
  $inf=Join-Path $r 'phaser360_h15f_function_driver.inf'
  $sys=Join-Path $r 'phaser360_h15f_function_driver.sys'
  $cat=Join-Path $r 'phaser360_h15f_function_driver.cat'
  $cer=Join-Path $r 'phaser360_h15f_function_driver.cer'
  $man=Join-Path $r 'package_manifest.json'
  foreach($p in @($inf,$sys,$cat,$cer,$man)){
    if(-not (Test-Path $p -PathType Leaf)){throw "PACKAGE_FILE_MISSING: $p"}
  }
  $c=Get-PfxCertificate $cer
  if(-not $c -or $c.HasPrivateKey -or $c.Subject -cne $CertSubject -or
     $c.Issuer -cne $CertSubject){throw 'CERTIFICATE_IDENTITY_INVALID'}
  if($c.NotBefore.ToUniversalTime() -gt [DateTime]::UtcNow -or
     $c.NotAfter.ToUniversalTime() -le [DateTime]::UtcNow){
    throw 'CERTIFICATE_NOT_CURRENTLY_VALID'
  }

  $m=Get-Content $man -Raw|ConvertFrom-Json
  if([string]$m.CertificateThumbprint -cne [string]$c.Thumbprint){
    throw 'MANIFEST_CERTIFICATE_THUMBPRINT_MISMATCH'
  }
  if([string]$m.Purpose -cne 'H15F_FUNCTION_DRIVER_OWNERSHIP_HANDOFF_PACKAGE' -or
     [string]$m.ExactHardwareId -cne $ExactHwid -or
     [string]$m.ExpectedBaselineInf -cne $BaselineInf -or
     [string]$m.ExpectedBaselineVersion -cne $BaselineVersion -or
     [string]$m.ExpectedBaselineProvider -cne $BaselineProvider -or
     [string]$m.ExpectedHdaPhysical -cne '0x00000000CEEE0000' -or
     [string]$m.ExpectedDspPhysical -cne '0x00000000CEF00000' -or
     [string]$m.HardwareAccess -cne 'NONE'){
    throw 'MANIFEST_HANDOFF_CONTRACT_MISMATCH'
  }
  foreach($x in @(
    @($inf,[string]$m.InfSha256),@($sys,[string]$m.SysSha256),
    @($cat,[string]$m.CatSha256),@($cer,[string]$m.CerSha256)
  )){
    if((Get-FileHash $x[0] -Algorithm SHA256).Hash.ToLowerInvariant() -cne
       $x[1].ToLowerInvariant()){throw 'MANIFEST_HASH_MISMATCH'}
  }

  $ss=Get-AuthenticodeSignature $sys
  $cs=Get-AuthenticodeSignature $cat
  if(-not $ss.SignerCertificate -or -not $cs.SignerCertificate){throw 'SIGNER_MISSING'}
  if([string]$ss.SignerCertificate.Thumbprint -cne [string]$c.Thumbprint -or
     [string]$cs.SignerCertificate.Thumbprint -cne [string]$c.Thumbprint){
    throw 'SIGNER_CERT_MISMATCH'
  }
  if($Trusted -and ($ss.Status -ne 'Valid' -or $cs.Status -ne 'Valid')){
    throw 'SIGNATURE_NOT_VALID_AFTER_TRUST'
  }
  [pscustomobject]@{Root=$r;Inf=$inf;Sys=$sys;Cat=$cat;Cer=$cer;Thumb=[string]$c.Thumbprint;Manifest=$m}
}
function Native {
  if('Phaser360.H15fNative' -as [type]){return}
  $src=@'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
namespace Phaser360 {
  public static class H15fNative {
    const uint INSTALLFLAG_FORCE=0x00000001;
    const uint DIGCF_PRESENT=0x2,DIGCF_DEVICEINTERFACE=0x10;
    const uint GENERIC_READ=0x80000000,FILE_SHARE_READ=1,FILE_SHARE_WRITE=2,OPEN_EXISTING=3;
    const int ERROR_NO_MORE_ITEMS=259;
    static readonly IntPtr INVALID_HANDLE_VALUE=new IntPtr(-1);

    [DllImport("newdev.dll",CharSet=CharSet.Unicode,SetLastError=true,EntryPoint="UpdateDriverForPlugAndPlayDevicesW")]
    static extern bool UpdateDriverForPlugAndPlayDevices(IntPtr hwndParent,string hardwareId,string fullInfPath,uint installFlags,out bool rebootRequired);

    [StructLayout(LayoutKind.Sequential)]
    struct SP_DEVICE_INTERFACE_DATA { public int cbSize; public Guid InterfaceClassGuid; public int Flags; public UIntPtr Reserved; }

    [DllImport("setupapi.dll",SetLastError=true)]
    static extern IntPtr SetupDiGetClassDevs(ref Guid g,IntPtr e,IntPtr h,uint f);
    [DllImport("setupapi.dll",SetLastError=true)]
    static extern bool SetupDiEnumDeviceInterfaces(IntPtr s,IntPtr d,ref Guid g,uint i,ref SP_DEVICE_INTERFACE_DATA x);
    [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr s,ref SP_DEVICE_INTERFACE_DATA x,IntPtr p,uint z,out uint n,IntPtr d);
    [DllImport("setupapi.dll",SetLastError=true)]
    static extern bool SetupDiDestroyDeviceInfoList(IntPtr s);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern SafeFileHandle CreateFile(string n,uint a,uint sh,IntPtr sa,uint c,uint f,IntPtr t);
    [DllImport("kernel32.dll",SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle h,uint c,IntPtr i,uint ib,byte[] o,uint ob,out uint r,IntPtr ov);

    static void Q(bool ok,string op) {
      if(!ok){int e=Marshal.GetLastWin32Error();throw new Win32Exception(e,op+"; WIN32_ERROR="+e);}
    }

    public static bool ForceUpdate(string hardwareId,string inf) {
      bool reboot;
      Q(UpdateDriverForPlugAndPlayDevices(IntPtr.Zero,hardwareId,inf,INSTALLFLAG_FORCE,out reboot),"UpdateDriverForPlugAndPlayDevicesW");
      return reboot;
    }

    static string InterfacePath(Guid g) {
      IntPtr s=SetupDiGetClassDevs(ref g,IntPtr.Zero,IntPtr.Zero,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);
      if(s==INVALID_HANDLE_VALUE) throw new Win32Exception(Marshal.GetLastWin32Error());
      try {
        var d=new SP_DEVICE_INTERFACE_DATA(); d.cbSize=Marshal.SizeOf(typeof(SP_DEVICE_INTERFACE_DATA));
        Q(SetupDiEnumDeviceInterfaces(s,IntPtr.Zero,ref g,0,ref d),"EnumInterface[0]");
        var d2=new SP_DEVICE_INTERFACE_DATA(); d2.cbSize=Marshal.SizeOf(typeof(SP_DEVICE_INTERFACE_DATA));
        bool second=SetupDiEnumDeviceInterfaces(s,IntPtr.Zero,ref g,1,ref d2);
        if(second||Marshal.GetLastWin32Error()!=ERROR_NO_MORE_ITEMS)
          throw new InvalidOperationException("EXPECTED_EXACTLY_ONE_H15F_INTERFACE");
        uint n=0;
        SetupDiGetDeviceInterfaceDetail(s,ref d,IntPtr.Zero,0,out n,IntPtr.Zero);
        IntPtr p=Marshal.AllocHGlobal((int)n);
        try {
          for(int k=0;k<n;k++) Marshal.WriteByte(p,k,0);
          Marshal.WriteInt32(p,0,8);
          Q(SetupDiGetDeviceInterfaceDetail(s,ref d,p,n,out n,IntPtr.Zero),"InterfaceDetail");
          return Marshal.PtrToStringUni(IntPtr.Add(p,4));
        } finally { Marshal.FreeHGlobal(p); }
      } finally { SetupDiDestroyDeviceInfoList(s); }
    }

    public static byte[] Snapshot(Guid g,uint ioctl,int bytes) {
      string path=InterfacePath(g);
      using(var h=CreateFile(path,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,IntPtr.Zero,OPEN_EXISTING,0,IntPtr.Zero)) {
        if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error(),"CreateFile H15F");
        var o=new byte[bytes]; uint got;
        Q(DeviceIoControl(h,ioctl,IntPtr.Zero,0,o,(uint)o.Length,out got,IntPtr.Zero),"DeviceIoControl H15F");
        if(got!=bytes) throw new InvalidOperationException("H15F_SNAPSHOT_SIZE="+got);
        return o;
      }
    }
  }
}
'@
  Add-Type -TypeDefinition $src -Language CSharp -ErrorAction Stop
}
function U64([byte[]]$b,[int]$o){[BitConverter]::ToUInt64($b,$o)}
function U32([byte[]]$b,[int]$o){[BitConverter]::ToUInt32($b,$o)}
function I32([byte[]]$b,[int]$o){[BitConverter]::ToInt32($b,$o)}
function Hashes([string]$d){
  Get-ChildItem $d -Recurse -File |
    Where-Object {$_.Name -ne 'SHA256SUMS.txt'} |
    Sort-Object FullName |
    ForEach-Object {
      (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()+' '+
      $_.FullName.Substring($d.Length).TrimStart('\')
    } | Set-Content (Join-Path $d 'SHA256SUMS.txt') -Encoding ASCII
}

if(-not (Admin)){throw 'ADMINISTRATOR_REQUIRED'}
if(-not [Environment]::Is64BitProcess){throw 'WINDOWS_X64_PROCESS_REQUIRED'}

$before=Target
AssertIntelBaseline $before
if(@(Published).Count -ne 0){throw 'H15F_PACKAGE_ALREADY_PRESENT'}

$ci=CodeIntegrity
if(($ci -band 2) -eq 0){throw 'CODE_INTEGRITY_TESTSIGN_NOT_ALLOWED'}
$re=& (Join-Path $env:SystemRoot 'System32\reagentc.exe') /info 2>&1|Out-String
if($LASTEXITCODE -ne 0 -or $re -notmatch '(?im)Windows\s+RE.*(?:Enabled|Activat)'){
  throw 'WINRE_NOT_READY'
}

$pkg=Package $PackageRoot
$tb=Trust $pkg.Thumb
if($tb.Root -or $tb.TrustedPublisher){throw 'H15F_CERT_ALREADY_TRUSTED'}

if([string]::IsNullOrWhiteSpace($OutputRoot)){$OutputRoot=$PSScriptRoot}
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss'
$suffix=[Guid]::NewGuid().ToString('N').Substring(0,8)
$dir=Join-Path $OutputRoot ('H15F_HANDOFF_'+$stamp+'_'+$suffix)
$backup=Join-Path $dir 'intel_baseline_export'
New-Item -ItemType Directory -Path $dir,$backup -Force|Out-Null
$before|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'target_before.json') -Encoding UTF8
Copy-Item (Join-Path $PSScriptRoot 'H15F_WINRE_ROLLBACK.txt') $dir

$ex=PnP @('/export-driver',$before.DriverInfPath,$backup)
$ex.Output|Set-Content (Join-Path $dir 'pnputil_export_intel.txt') -Encoding UTF8
if($ex.ExitCode -ne 0 -or @(Get-ChildItem $backup -Recurse -File).Count -eq 0){
  throw 'BASELINE_EXPORT_FAILED'
}

$rootAdded=$false
$pubAdded=$false
$publishedInf=$null
$published=$false
$handoffAttempted=$false
$handoffComplete=$false
$snapshotComplete=$false
$normal=$false
$err=$null

try {
  $x=CertUtil @('-f','-addstore','Root',$pkg.Cer)
  if($x.ExitCode -ne 0){throw 'CERT_ROOT_ADD_FAILED'}
  $rootAdded=$true

  $x=CertUtil @('-f','-addstore','TrustedPublisher',$pkg.Cer)
  if($x.ExitCode -ne 0){throw 'CERT_PUBLISHER_ADD_FAILED'}
  $pubAdded=$true

  $tn=Trust $pkg.Thumb
  if(-not $tn.Root -or -not $tn.TrustedPublisher){throw 'CERT_TRUST_NOT_PRESENT'}
  $null=Package $PackageRoot -Trusted

  $x=PnP @('/add-driver',$pkg.Inf)
  $x.Output|Set-Content (Join-Path $dir 'pnputil_publish_h15f.txt') -Encoding UTF8
  if($x.ExitCode -ne 0){throw 'H15F_DRIVERSTORE_PUBLISH_FAILED'}
  $p=@(Published)
  if($p.Count -ne 1){throw "EXPECTED_ONE_H15F_PUBLISHED_INF: count=$($p.Count)"}
  $publishedInf=$p[0]
  $published=$true
  $publishedInf|Set-Content (Join-Path $dir 'H15fPublishedInf.txt') -Encoding ASCII

  Native
  $handoffAttempted=$true
  $reboot=[Phaser360.H15fNative]::ForceUpdate($ExactHwid,$pkg.Inf)
  if($reboot){throw 'H15F_HANDOFF_REQUIRES_SYSTEM_REBOOT'}

  $with=WaitService $before.InstanceId $Service 20
  $with|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'target_with_h15f.json') -Encoding UTF8
  if($with.DriverInfPath -cne $publishedInf){
    throw "H15F_PUBLISHED_INF_NOT_ACTIVE: active=$($with.DriverInfPath) published=$publishedInf"
  }
  $handoffComplete=$true

  $r=[Phaser360.H15fNative]::Snapshot($InterfaceGuid,$Ioctl,$SnapshotBytes)
  $v=U32 $r 0
  $sz=U32 $r 4
  $fl=U32 $r 8
  $nt=I32 $r 12
  $gen=U32 $r 16
  $prep=U32 $r 20
  $d0in=U32 $r 24
  $d0out=U32 $r 28
  $rel=U32 $r 32
  $raw=U32 $r 36
  $tr=U32 $r 40
  $mem=U32 $r 44
  $irq=U32 $r 48
  $hda=U64 $r 56
  $dsp=U64 $r 64
  $hdaLen=U32 $r 72
  $dspLen=U32 $r 76
  $irqFlags=U32 $r 80

  [ordered]@{
    Version=$v;Size=$sz;Flags=('0x{0:X8}' -f $fl)
    LastStatus=('0x{0:X8}' -f ([uint32]$nt))
    Generation=$gen;PrepareCount=$prep;D0EntryCount=$d0in
    D0ExitCount=$d0out;ReleaseCount=$rel
    RawResourceCount=$raw;TranslatedResourceCount=$tr
    MemoryCount=$mem;InterruptCount=$irq
    HdaPhysical=('0x{0:X16}' -f $hda)
    DspPhysical=('0x{0:X16}' -f $dsp)
    HdaLength=('0x{0:X8}' -f $hdaLen)
    DspLength=('0x{0:X8}' -f $dspLen)
    InterruptFlags=('0x{0:X8}' -f $irqFlags)
  }|ConvertTo-Json -Depth 6|Set-Content (Join-Path $dir 'h15f_snapshot.json') -Encoding UTF8

  if($v -ne 1 -or $sz -ne 96 -or $nt -lt 0 -or
     ($fl -band $RequiredFlags) -ne $RequiredFlags -or
     $prep -lt 1 -or $d0in -lt 1 -or
     $mem -ne 2 -or $irq -ne 1 -or
     $hda -ne $ExpectedHda -or $dsp -ne $ExpectedDsp -or
     $hdaLen -ne $ExpectedHdaLength -or $dspLen -ne $ExpectedDspLength){
    throw 'H15F_LIVE_SNAPSHOT_VALIDATION_FAILED'
  }
  $snapshotComplete=$true

  $x=PnP @('/delete-driver',$publishedInf,'/uninstall','/force')
  $x.Output|Set-Content (Join-Path $dir 'pnputil_remove_h15f.txt') -Encoding UTF8
  if($x.ExitCode -ne 0){throw 'H15F_UNINSTALL_FAILED'}

  $after=WaitService $before.InstanceId 'IntcAudioBus' 20
  if($after.DriverInfPath -cne $BaselineInf -or
     $after.DriverVersion -cne $BaselineVersion -or
     $after.DriverProvider -cne $BaselineProvider){
    throw 'INTEL_BASELINE_NOT_RESELECTED'
  }
  if(@(Published).Count -ne 0){throw 'H15F_PACKAGE_REMAINS'}

  $x=CertUtil @('-delstore','TrustedPublisher',$pkg.Thumb)
  if($x.ExitCode -ne 0){throw 'CERT_PUBLISHER_REMOVE_FAILED'}
  $pubAdded=$false

  $x=CertUtil @('-delstore','Root',$pkg.Thumb)
  if($x.ExitCode -ne 0){throw 'CERT_ROOT_REMOVE_FAILED'}
  $rootAdded=$false

  $ta=Trust $pkg.Thumb
  if($ta.Root -or $ta.TrustedPublisher){throw 'CERT_TRUST_REMAINS'}

  $after|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'target_after.json') -Encoding UTF8
  $normal=$true
} catch {
  $err=$_.Exception
} finally {
  if(-not $normal){
    $log=@()
    if($published){
      foreach($p in @(Published)){
        $q=PnP @('/delete-driver',$p,'/uninstall','/force')
        $log+="DELETE $p EXIT=$($q.ExitCode)"
        $log+=$q.Output
      }
      try{
        $s=WaitService $before.InstanceId 'IntcAudioBus' 20
        $log+="INTEL_RETURN=$($s.Status);INF=$($s.DriverInfPath);VER=$($s.DriverVersion)"
      }catch{$log+="INTEL_RETURN_EXCEPTION=$($_.Exception.Message)"}
    }

    $safe=$false
    try{
      $s=Target
      $safe=@(Published).Count -eq 0 -and
        $s.Status -ceq 'OK' -and $s.ProblemCode -eq 0 -and
        $s.InstanceId -ceq $before.InstanceId -and
        $s.Service -ceq 'IntcAudioBus' -and
        $s.DriverInfPath -ceq $BaselineInf -and
        $s.DriverVersion -ceq $BaselineVersion -and
        $s.DriverProvider -ceq $BaselineProvider
    }catch{}

    if($safe){
      if($pubAdded){
        $q=CertUtil @('-delstore','TrustedPublisher',$pkg.Thumb)
        $qp=Trust $pkg.Thumb
        if($q.ExitCode -eq 0 -and -not $qp.TrustedPublisher){$pubAdded=$false}
      }
      if($rootAdded){
        $q=CertUtil @('-delstore','Root',$pkg.Thumb)
        $qr=Trust $pkg.Thumb
        if($q.ExitCode -eq 0 -and -not $qr.Root){$rootAdded=$false}
      }
    }else{
      $log+='TRUST_RETAINED_FOR_SAFETY=TRUE'
    }
    $log|Set-Content (Join-Path $dir 'emergency_rollback.txt') -Encoding UTF8
  }
}

$final=$null
try{$final=Target}catch{}
$ft=Trust $pkg.Thumb
$baseline=$false
if($final){
  $baseline=$final.Status -ceq 'OK' -and $final.ProblemCode -eq 0 -and
    $final.InstanceId -ceq $before.InstanceId -and
    $final.Service -ceq 'IntcAudioBus' -and
    $final.DriverInfPath -ceq $BaselineInf -and
    $final.DriverVersion -ceq $BaselineVersion -and
    $final.DriverProvider -ceq $BaselineProvider -and
    @(Published).Count -eq 0 -and
    -not $ft.Root -and -not $ft.TrustedPublisher
}
$status=if($normal -and $baseline -and $handoffComplete -and $snapshotComplete -and -not $err){
  'H15F_FUNCTION_DRIVER_HANDOFF_AND_ROLLBACK_COMPLETE'
}else{'H15F_FUNCTION_DRIVER_HANDOFF_FAILED'}

[ordered]@{
  Status=$status;PublishedInf=$publishedInf
  HandoffAttempted=$handoffAttempted;HandoffCompleted=$handoffComplete
  SnapshotCompleted=$snapshotComplete;BaselineRestored=$baseline
  TrustRestored=(-not $ft.Root -and -not $ft.TrustedPublisher)
  TransactionError=$(if($err){$err.Message}else{$null})
  FunctionDriver='Phaser360H15f'
  HardwareAccess='NONE';Mmio='NO';PciWrite='NO';Dma='NO'
  IrqOwnership='NO';Firmware='NO';DspBoot='NO';Playback='NO'
  SystemReboot='NO';BcdWrite='NO'
}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $dir 'transaction.json') -Encoding UTF8

Hashes $dir
$zip=Join-Path $OutputRoot ('RESULT_H15F_HANDOFF_'+$stamp+'_'+$suffix+'.zip')
Compress-Archive -Path (Join-Path $dir '*') -DestinationPath $zip -Force

Write-Host "STATUS=$status"
Write-Host "HANDOFF_COMPLETED=$($handoffComplete.ToString().ToUpperInvariant())"
Write-Host "SNAPSHOT_COMPLETED=$($snapshotComplete.ToString().ToUpperInvariant())"
Write-Host "BASELINE_RESTORED=$($baseline.ToString().ToUpperInvariant())"
Write-Host "TRUST_RESTORED=$(((-not $ft.Root -and -not $ft.TrustedPublisher)).ToString().ToUpperInvariant())"
Write-Host "PUBLISHED_INF=$publishedInf"
Write-Host "Trimite fisierul: $zip"
if($err){throw $err}
if(-not $normal -or -not $baseline -or -not $handoffComplete -or -not $snapshotComplete){exit 3}
