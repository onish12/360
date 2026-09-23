#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$PackageRoot,[string]$OutputRoot='')
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$ExactHwid='PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06'
$Service='Phaser360H15l'
$CertSubject='CN=PHASER360 H15L R2 Ephemeral Test Signing'
$InterfaceGuid=[Guid]'8c1b3150-6d12-4f88-9d36-15f600319802'
$Ioctl=[Convert]::ToUInt32('833FE47C',16)
$RequiredFlags=[Convert]::ToUInt32('FFFFFFFF',16)
$ResultBytes=304
$ExpectedPg=[Convert]::ToUInt32('00000010',16)
$ExpectedCg=[Convert]::ToUInt32('807B0DFF',16)
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
  $pat='(?im)^\s*AddService\s*=\s*Phaser360H15l\s*,'
  foreach($f in @(Get-ChildItem (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' -File)){
    try{$t=Get-Content $f.FullName -Raw}catch{continue}
    if($t -match $pat -and
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
  if(-not ('Phaser360.H15lCi' -as [type])){
    Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;namespace Phaser360{public static class H15lCi{[StructLayout(LayoutKind.Sequential)]struct CI{public UInt32 Length;public UInt32 Options;}[DllImport("ntdll.dll")]static extern Int32 NtQuerySystemInformation(Int32 c,ref CI i,UInt32 l,IntPtr r);public static UInt32 Get(){CI i=new CI();i.Length=(UInt32)Marshal.SizeOf(typeof(CI));Int32 s=NtQuerySystemInformation(103,ref i,i.Length,IntPtr.Zero);if(s<0)throw new InvalidOperationException("CI=0x"+unchecked((UInt32)s).ToString("X8"));return i.Options;}}}' -Language CSharp
  }
  [uint32][Phaser360.H15lCi]::Get()
}
function Package([string]$root,[switch]$Trusted){
  $r=(Resolve-Path $root).Path
  $inf=Join-Path $r 'phaser360_h15l_core1_power_gate.inf'
  $sys=Join-Path $r 'phaser360_h15l_core1_power_gate.sys'
  $cat=Join-Path $r 'phaser360_h15l_core1_power_gate.cat'
  $cer=Join-Path $r 'phaser360_h15l_core1_power_gate.cer'
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
  if([string]$m.Purpose -cne 'H15L_R2_CORE1_SPA_CPA_HANDSHAKE_TRANSACTION_PACKAGE' -or
     [string]$m.CertificateSubject -cne $CertSubject -or
     [string]$m.ExactHardwareId -cne $ExactHwid -or
     [string]$m.ExpectedBaselineInf -cne $BaselineInf -or
     [string]$m.ExpectedBaselineVersion -cne $BaselineVersion -or
     [string]$m.ExpectedBaselineProvider -cne $BaselineProvider -or
     [string]$m.ExpectedHdaPhysical -cne '0x00000000CEEE0000' -or
     [string]$m.ExpectedDspPhysical -cne '0x00000000CEF00000' -or
     [string]$m.Mapping -cne 'HDA_PAGE_READONLY_DSP_PAGE_READWRITE' -or
     [string]$m.MmioWrite -cne 'ONLY_DSP_ADSPCS_CORE1_SPA_WITH_CPA_READONLY_ROLLBACK' -or
     [string]$m.Core0Write -cne 'NO' -or
     [string]$m.CpaWrite -cne 'NO' -or
     [string]$m.CstallWrite -cne 'NO' -or
     [string]$m.CrstWrite -cne 'NO' -or
     [string]$m.PciConfigWrite -cne 'NO' -or
     [string]$m.Dma -cne 'NO' -or
     [string]$m.IrqOwnership -cne 'NO' -or
     [string]$m.Firmware -cne 'NO' -or
     [string]$m.DspBoot -cne 'NO' -or
     [string]$m.Playback -cne 'NO' -or
     [string]$m.ExpectedPgctl -cne '0x00000010' -or
     [string]$m.ExpectedCgctl -cne '0x807B0DFF'){
    throw 'MANIFEST_H15L_CONTRACT_MISMATCH'
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
  if('Phaser360.H15lNative' -as [type]){return}
  $src=@'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
namespace Phaser360 {
  public static class H15lNative {
    const uint INSTALLFLAG_FORCE=0x00000001;
    const uint DIGCF_PRESENT=0x2,DIGCF_DEVICEINTERFACE=0x10;
    const uint GENERIC_READ=0x80000000,GENERIC_WRITE=0x40000000,FILE_SHARE_READ=1,FILE_SHARE_WRITE=2,OPEN_EXISTING=3;
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
    static extern bool DeviceIoControl(SafeFileHandle h,uint c,byte[] i,uint ib,byte[] o,uint ob,out uint r,IntPtr ov);

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
          throw new InvalidOperationException("EXPECTED_EXACTLY_ONE_H15L_INTERFACE");
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

    public static byte[] Transaction(Guid g,uint ioctl,byte[] input,int bytes) {
      string path=InterfacePath(g);
      using(var h=CreateFile(path,GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,IntPtr.Zero,OPEN_EXISTING,0,IntPtr.Zero)) {
        if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error(),"CreateFile H15L");
        var o=new byte[bytes]; uint got;
        Q(DeviceIoControl(h,ioctl,input,(uint)input.Length,o,(uint)o.Length,out got,IntPtr.Zero),"DeviceIoControl H15L");
        if(got!=bytes) throw new InvalidOperationException("H15L_RESULT_SIZE="+got);
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
function U16([byte[]]$b,[int]$o){[BitConverter]::ToUInt16($b,$o)}
function I32([byte[]]$b,[int]$o){[BitConverter]::ToInt32($b,$o)}
function Put32([byte[]]$b,[int]$o,[uint32]$v){[Array]::Copy([BitConverter]::GetBytes($v),0,$b,$o,4)}
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
if(@(Published).Count -ne 0){throw 'H15L_PACKAGE_ALREADY_PRESENT'}

$ci=CodeIntegrity
if(($ci -band 2) -eq 0){throw 'CODE_INTEGRITY_TESTSIGN_NOT_ALLOWED'}
$re=& (Join-Path $env:SystemRoot 'System32\reagentc.exe') /info 2>&1|Out-String
if($LASTEXITCODE -ne 0 -or $re -notmatch '(?im)Windows\s+RE.*(?:Enabled|Activat)'){
  throw 'WINRE_NOT_READY'
}

$pkg=Package $PackageRoot
$tb=Trust $pkg.Thumb
if($tb.Root -or $tb.TrustedPublisher){throw 'H15L_CERT_ALREADY_TRUSTED'}

if([string]::IsNullOrWhiteSpace($OutputRoot)){$OutputRoot=$PSScriptRoot}
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss'
$suffix=[Guid]::NewGuid().ToString('N').Substring(0,8)
$dir=Join-Path $OutputRoot ('H15L_TRANSACTION_'+$stamp+'_'+$suffix)
$backup=Join-Path $dir 'intel_baseline_export'
New-Item -ItemType Directory -Path $dir,$backup -Force|Out-Null
$before|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'target_before.json') -Encoding UTF8
Copy-Item (Join-Path $PSScriptRoot 'H15L_WINRE_ROLLBACK.txt') $dir

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
$writeAttempted=$false
$writeRestoreComplete=$false
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
  $x.Output|Set-Content (Join-Path $dir 'pnputil_publish_h15l.txt') -Encoding UTF8
  if($x.ExitCode -ne 0){throw 'H15L_DRIVERSTORE_PUBLISH_FAILED'}
  $p=@(Published)
  if($p.Count -ne 1){throw "EXPECTED_ONE_H15L_PUBLISHED_INF: count=$($p.Count)"}
  $publishedInf=$p[0]
  $published=$true
  $publishedInf|Set-Content (Join-Path $dir 'H15lPublishedInf.txt') -Encoding ASCII

  Native
  $handoffAttempted=$true
  $reboot=[Phaser360.H15lNative]::ForceUpdate($ExactHwid,$pkg.Inf)
  if($reboot){throw 'H15L_HANDOFF_REQUIRES_SYSTEM_REBOOT'}

  $with=WaitService $before.InstanceId $Service 20
  $with|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'target_with_h15l.json') -Encoding UTF8
  if($with.DriverInfPath -cne $publishedInf){
    throw "H15L_PUBLISHED_INF_NOT_ACTIVE: active=$($with.DriverInfPath) published=$publishedInf"
  }
  $handoffComplete=$true

  $req=[byte[]]::new(16)
  Put32 $req 0 2
  Put32 $req 4 16
  Put32 $req 8 $ExpectedPg
  Put32 $req 12 $ExpectedCg
  $writeAttempted=$true
  $r=[Phaser360.H15lNative]::Transaction($InterfaceGuid,$Ioctl,$req,$ResultBytes)

  $v=U32 $r 0
  $sz=U32 $r 4
  $nt=I32 $r 8
  $fl=U32 $r 12
  $gen=U32 $r 16
  $ven=U16 $r 20
  $dev=U16 $r 22
  $pg=U32 $r 28
  $cg=U32 $r 32
  $hda=U64 $r 40
  $dsp=U64 $r 48
  $hdaLen=U32 $r 56
  $dspLen=U32 $r 60

  function Obs([byte[]]$b,[int]$o){
    [ordered]@{
      HdaGcap=('0x{0:X4}' -f (U16 $b $o)); HdaVmin=('0x{0:X2}' -f [uint32]$b[$o+2]); HdaVmaj=('0x{0:X2}' -f [uint32]$b[$o+3])
      HdaGctl=('0x{0:X8}' -f (U32 $b ($o+4))); HdaCorbctl=('0x{0:X2}' -f [uint32]$b[$o+8]); HdaRirbctl=('0x{0:X2}' -f [uint32]$b[$o+9])
      TotalStreams=[uint32]$b[$o+10]; StreamRunMask=('0x{0:X8}' -f (U32 $b ($o+12))); HdaIntelEm2=('0x{0:X8}' -f (U32 $b ($o+16)))
      DspAdspcs=('0x{0:X8}' -f (U32 $b ($o+20))); DspAdspic=('0x{0:X8}' -f (U32 $b ($o+24))); DspAdspis=('0x{0:X8}' -f (U32 $b ($o+28)))
      DspHipci=('0x{0:X8}' -f (U32 $b ($o+32))); DspHipcie=('0x{0:X8}' -f (U32 $b ($o+36))); DspHipcctl=('0x{0:X8}' -f (U32 $b ($o+40)))
      DspRomStatus=('0x{0:X8}' -f (U32 $b ($o+44)))
    }
  }
  $beforeRegs=Obs $r 64
  $requestedRegs=Obs $r 112
  $poweredRegs=Obs $r 160
  $depoweredRegs=Obs $r 208
  $restoredRegs=Obs $r 256

  [ordered]@{
    Version=$v;Size=$sz;NtStatus=('0x{0:X8}' -f ([BitConverter]::ToUInt32([BitConverter]::GetBytes([int32]$nt),0)))
    Flags=('0x{0:X8}' -f $fl);Generation=$gen
    Vendor=('0x{0:X4}' -f $ven);Device=('0x{0:X4}' -f $dev)
    Pgctl=('0x{0:X8}' -f $pg);Cgctl=('0x{0:X8}' -f $cg)
    HdaPhysical=('0x{0:X16}' -f $hda);DspPhysical=('0x{0:X16}' -f $dsp)
    HdaLength=('0x{0:X8}' -f $hdaLen);DspLength=('0x{0:X8}' -f $dspLen)
    Before=$beforeRegs;Requested=$requestedRegs;Powered=$poweredRegs;Depowered=$depoweredRegs;Restored=$restoredRegs
  }|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'h15l_transaction.json') -Encoding UTF8

  $beforeGctl=U32 $r 68
  $beforeCorb=[uint32]$r[72]
  $beforeRirb=[uint32]$r[73]
  $beforeStreams=[uint32]$r[74]
  $beforeRunMask=U32 $r 76
  $beforeAdspcs=U32 $r 84
  $requestedAdspcs=U32 $r 132
  $poweredAdspcs=U32 $r 180
  $depoweredAdspcs=U32 $r 228
  $restoredAdspcs=U32 $r 276

  # Hardware-restore proof is derived before broad success validation.
  # Dynamic rollback proof bits: SPA clear written/observed, CPA clear observed,
  # depowered captured/exact, restored captured/exact.
  $writeAttempted=(($fl -band [Convert]::ToUInt32('00000010',16)) -ne 0)
  $restoreMask=[Convert]::ToUInt32('0001FC00',16)
  $writeRestoreComplete=(($fl -band $restoreMask) -eq $restoreMask -and
                         $restoredAdspcs -eq $beforeAdspcs -and
                         $restoredAdspcs -eq [Convert]::ToUInt32('001D003C',16))

  $spa1=[Convert]::ToUInt32('00020000',16)
  $cpa1=[Convert]::ToUInt32('02000000',16)
  $spaCpaMask=$spa1 -bor $cpa1
  $requestedBase=$requestedAdspcs -band (-bnot $spaCpaMask)

  if($v -ne 2 -or $sz -ne $ResultBytes -or $nt -lt 0 -or
     ($fl -band $RequiredFlags) -ne $RequiredFlags -or
     $ven -ne 0x8086 -or $dev -ne 0x3198 -or
     $pg -ne $ExpectedPg -or $cg -ne $ExpectedCg -or
     $hda -ne $ExpectedHda -or $dsp -ne $ExpectedDsp -or
     $hdaLen -ne $ExpectedHdaLength -or $dspLen -ne $ExpectedDspLength -or
     $beforeGctl -ne 0 -or
     ($beforeCorb -band 2) -ne 0 -or ($beforeRirb -band 2) -ne 0 -or
     $beforeStreams -ne 13 -or $beforeRunMask -ne 0 -or
     $beforeAdspcs -ne [Convert]::ToUInt32('001D003C',16) -or
     ($requestedAdspcs -band $spa1) -ne $spa1 -or
     $requestedBase -ne [Convert]::ToUInt32('001D003C',16) -or
     $poweredAdspcs -ne [Convert]::ToUInt32('021F003C',16) -or
     $depoweredAdspcs -ne [Convert]::ToUInt32('001D003C',16) -or
     $restoredAdspcs -ne $beforeAdspcs){
    throw 'H15L_R2_LIVE_TRANSACTION_VALIDATION_FAILED'
  }
  $snapshotComplete=$true

  $x=PnP @('/delete-driver',$publishedInf,'/uninstall','/force')
  $x.Output|Set-Content (Join-Path $dir 'pnputil_remove_h15l.txt') -Encoding UTF8
  if($x.ExitCode -ne 0){throw 'H15L_UNINSTALL_FAILED'}

  $after=WaitService $before.InstanceId 'IntcAudioBus' 20
  if($after.DriverInfPath -cne $BaselineInf -or
     $after.DriverVersion -cne $BaselineVersion -or
     $after.DriverProvider -cne $BaselineProvider){
    throw 'INTEL_BASELINE_NOT_RESELECTED'
  }
  if(@(Published).Count -ne 0){throw 'H15L_PACKAGE_REMAINS'}

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
    if($published -and ((-not $writeAttempted) -or $writeRestoreComplete)){
      foreach($p in @(Published)){
        $q=PnP @('/delete-driver',$p,'/uninstall','/force')
        $log+="DELETE $p EXIT=$($q.ExitCode)"
        $log+=$q.Output
      }
      try{
        $s=WaitService $before.InstanceId 'IntcAudioBus' 20
        $log+="INTEL_RETURN=$($s.Status);INF=$($s.DriverInfPath);VER=$($s.DriverVersion)"
      }catch{$log+="INTEL_RETURN_EXCEPTION=$($_.Exception.Message)"}
    } elseif($published -and $writeAttempted -and -not $writeRestoreComplete) {
      $log+='AUTOMATIC_UNINSTALL_BLOCKED_UNPROVEN_ADSPCS_RESTORE=TRUE'
      $log+='USE_H15L_WINRE_ROLLBACK_IF_WINDOWS_BECOMES_UNBOOTABLE=TRUE'
    }

    $safe=$false
    try{
      $s=Target
      $safe=(( -not $writeAttempted) -or $writeRestoreComplete) -and
        @(Published).Count -eq 0 -and
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
  'H15L_R2_CORE1_SPA_CPA_HANDSHAKE_AND_ROLLBACK_COMPLETE'
}else{'H15L_R2_CORE1_POWER_TRANSACTION_FAILED'}

[ordered]@{
  Status=$status;PublishedInf=$publishedInf
  HandoffAttempted=$handoffAttempted;HandoffCompleted=$handoffComplete
  WriteAttempted=$writeAttempted;WriteRestoreCompleted=$writeRestoreComplete
  SnapshotCompleted=$snapshotComplete;BaselineRestored=$baseline
  TrustRestored=(-not $ft.Root -and -not $ft.TrustedPublisher)
  TransactionError=$(if($err){$err.Message}else{$null})
  FunctionDriver='Phaser360H15l'
  Mapping='HDA_PAGE_READONLY_DSP_PAGE_READWRITE'
  MmioRead='HDA_IDLE_AND_DSP_STATUS';MmioWrite='ONLY_DSP_ADSPCS_CORE1_SPA_WITH_CPA_READONLY_ROLLBACK'
  HdaQuiescence='CORB_RIRB_ALL_STREAM_RUN_ZERO_BEFORE_ADSPCS_WRITE'
  DspStatusRead='ADSPCS_ADSPIC_ADSPIS_HIPCI_HIPCIE_HIPCCTL_ROM'
  HdaMmioWrite='NO';Core0Write='NO';CpaWrite='NO';CstallWrite='NO';CrstWrite='NO'
  PciConfigWrite='NO';Dma='NO'
  IrqOwnership='NO';Firmware='NO';DspBoot='NO';Playback='NO'
  SystemReboot='NO';BcdWrite='NO'
}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $dir 'transaction.json') -Encoding UTF8

Hashes $dir
$zip=Join-Path $OutputRoot ('RESULT_H15L_TRANSACTION_'+$stamp+'_'+$suffix+'.zip')
Compress-Archive -Path (Join-Path $dir '*') -DestinationPath $zip -Force

Write-Host "STATUS=$status"
Write-Host "HANDOFF_COMPLETED=$($handoffComplete.ToString().ToUpperInvariant())"
Write-Host "WRITE_RESTORE_COMPLETED=$($writeRestoreComplete.ToString().ToUpperInvariant())"
Write-Host "SNAPSHOT_COMPLETED=$($snapshotComplete.ToString().ToUpperInvariant())"
Write-Host "BASELINE_RESTORED=$($baseline.ToString().ToUpperInvariant())"
Write-Host "TRUST_RESTORED=$(((-not $ft.Root -and -not $ft.TrustedPublisher)).ToString().ToUpperInvariant())"
Write-Host "PUBLISHED_INF=$publishedInf"
Write-Host "Trimite fisierul: $zip"
if($err){throw $err}
if(-not $normal -or -not $baseline -or -not $handoffComplete -or -not $snapshotComplete){exit 3}
