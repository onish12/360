#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$PackageRoot,[string]$OutputRoot='')
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$ExactHwid='PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06'
$ExtensionId='{53f678f1-2b3e-4b2e-a15e-360031980001}'
$Service='Phaser360H15eLive'
$CertSubject='CN=PHASER360 H15E-LIVE R0 Ephemeral Test Signing'
$InterfaceGuid=[Guid]'8c1b3150-6d0e-4c88-9d36-15e000319801'
$Ioctl=[Convert]::ToUInt32('8339645C',16)
$SnapshotBytes=104
$RequiredFlags=[Convert]::ToUInt32('000007FF',16)
$ExpectedPg=[Convert]::ToUInt32('00000010',16)
$ExpectedCg=[Convert]::ToUInt32('807B0DFF',16)

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
    $m=@{};foreach($x in $p){$m[[string]$x.KeyName]=$x.Data}
    $upper=@()
    try{
        $u=@(Get-PnpDeviceProperty -InstanceId $id -KeyName 'DEVPKEY_Device_CompoundUpperFilters' -ErrorAction Stop)
        if($u.Count -eq 1){$upper=@($u[0].Data)}
    }catch{}
    [pscustomobject]@{
        InstanceId=$id;Status=[string]$d[0].Status
        ProblemCode=[int]$m['DEVPKEY_Device_ProblemCode']
        Service=[string]$m['DEVPKEY_Device_Service']
        DriverInfPath=[string]$m['DEVPKEY_Device_DriverInfPath']
        DriverVersion=[string]$m['DEVPKEY_Device_DriverVersion']
        DriverProvider=[string]$m['DEVPKEY_Device_DriverProvider']
        HardwareIds=@($m['DEVPKEY_Device_HardwareIds'])
        CompoundUpperFilters=$upper
    }
}
function AssertTarget($s){
    if([Environment]::OSVersion.Version.Build -ne 19044){throw 'EXACT_WINDOWS_BUILD_19044_REQUIRED'}
    if($s.Status -cne 'OK' -or $s.ProblemCode -ne 0 -or $s.Service -cne 'IntcAudioBus'){
        throw 'TARGET_NOT_HEALTHY_ON_INTC_AUDIO_BUS'
    }
    if(@($s.HardwareIds|Where-Object {$_ -ceq $ExactHwid}).Count -ne 1){
        throw 'EXACT_HARDWARE_ID_NOT_PRESENT'
    }
}
function PnP([string[]]$a){
    $e=Join-Path $env:SystemRoot 'System32\pnputil.exe'
    $old=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try{$o=(& $e @a 2>&1|Out-String -Width 8192);$c=$LASTEXITCODE}
    finally{$ErrorActionPreference=$old}
    [pscustomobject]@{ExitCode=$c;Output=$o}
}
function CertUtil([string[]]$a){
    $e=Join-Path $env:SystemRoot 'System32\certutil.exe'
    $old=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try{$o=(& $e @a 2>&1|Out-String -Width 8192);$c=$LASTEXITCODE}
    finally{$ErrorActionPreference=$old}
    [pscustomobject]@{ExitCode=$c;Output=$o}
}
function Published {
    $r=@()
    foreach($f in @(Get-ChildItem (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' -File)){
        try{$t=Get-Content $f.FullName -Raw}catch{continue}
        if($t.IndexOf($ExtensionId,[StringComparison]::OrdinalIgnoreCase) -ge 0 -and
           $t.IndexOf($Service,[StringComparison]::OrdinalIgnoreCase) -ge 0){$r+=$f.Name}
    }
    @($r)
}
function WaitHealthy([string]$id){
    $end=(Get-Date).AddSeconds(15)
    do{
        Start-Sleep -Milliseconds 500
        try{$s=Target}catch{$s=$null}
        if($s -and $s.InstanceId -ceq $id -and $s.Status -ceq 'OK' -and
           $s.ProblemCode -eq 0 -and $s.Service -ceq 'IntcAudioBus'){return $s}
    }while((Get-Date) -lt $end)
    throw 'TARGET_DID_NOT_RETURN_HEALTHY'
}
function CodeIntegrity {
    if(-not ('Phaser360.H15eCi' -as [type])){
        Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;namespace Phaser360{public static class H15eCi{[StructLayout(LayoutKind.Sequential)]struct CI{public UInt32 Length;public UInt32 Options;}[DllImport("ntdll.dll")]static extern Int32 NtQuerySystemInformation(Int32 c,ref CI i,UInt32 l,IntPtr r);public static UInt32 Get(){CI i=new CI();i.Length=(UInt32)Marshal.SizeOf(typeof(CI));Int32 s=NtQuerySystemInformation(103,ref i,i.Length,IntPtr.Zero);if(s<0)throw new InvalidOperationException("CI=0x"+unchecked((UInt32)s).ToString("X8"));return i.Options;}}}' -Language CSharp
    }
    [uint32][Phaser360.H15eCi]::Get()
}
function Trust([string]$thumb){
    $t=$thumb.Replace(' ','').ToUpperInvariant()
    [pscustomobject]@{
        Root=(Test-Path "Cert:\LocalMachine\Root\$t")
        TrustedPublisher=(Test-Path "Cert:\LocalMachine\TrustedPublisher\$t")
    }
}
function Hashes([string]$d){
    Get-ChildItem $d -Recurse -File |
        Where-Object {$_.Name -ne 'SHA256SUMS.txt'} |
        Sort-Object FullName | ForEach-Object {
            (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()+' '+
            $_.FullName.Substring($d.Length).TrimStart('\')
        } | Set-Content (Join-Path $d 'SHA256SUMS.txt') -Encoding ASCII
}
function Package([string]$root,[switch]$Trusted){
    $r=(Resolve-Path $root).Path
    $inf=Join-Path $r 'phaser360_h15e_live_filter.inf'
    $sys=Join-Path $r 'phaser360_h15e_live_filter.sys'
    $cat=Join-Path $r 'phaser360_h15e_live_filter.cat'
    $cer=Join-Path $r 'phaser360_h15e_live_filter.cer'
    $man=Join-Path $r 'package_manifest.json'
    foreach($p in @($inf,$sys,$cat,$cer,$man)){
        if(-not (Test-Path $p -PathType Leaf)){throw "PACKAGE_FILE_MISSING: $p"}
    }
    $c=Get-PfxCertificate $cer
    if(-not $c -or $c.HasPrivateKey -or $c.Subject -cne $CertSubject -or $c.Issuer -cne $CertSubject){
        throw 'CERTIFICATE_IDENTITY_INVALID'
    }
    if($c.NotBefore.ToUniversalTime() -gt [DateTime]::UtcNow -or
       $c.NotAfter.ToUniversalTime() -le [DateTime]::UtcNow){
        throw 'CERTIFICATE_NOT_CURRENTLY_VALID'
    }
    $eku=@()
    foreach($e in $c.Extensions){
        if($e.Oid.Value -eq '2.5.29.37'){
            $te=[System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]$e
            foreach($o in $te.EnhancedKeyUsages){$eku+=[string]$o.Value}
        }
    }
    if(@($eku|Where-Object {$_ -ceq '1.3.6.1.5.5.7.3.3'}).Count -ne 1){
        throw 'CODE_SIGNING_EKU_MISSING'
    }

    $m=Get-Content $man -Raw|ConvertFrom-Json
    if([string]$m.CertificateThumbprint -cne [string]$c.Thumbprint){
        throw 'MANIFEST_CERTIFICATE_THUMBPRINT_MISMATCH'
    }
    if([string]$m.Purpose -cne 'H15E_LIVE_R0_TRANSIENT_READONLY_MMIO_PACKAGE' -or
       [int]$m.SnapshotBytes -ne 104 -or
       [string]$m.ExpectedPgctl -cne '0x00000010' -or
       [string]$m.ExpectedCgctl -cne '0x807B0DFF' -or
       [string]$m.Mapping -cne 'PAGE_READONLY_NOCACHE_TRANSIENT' -or
       [string]$m.MmioWrite -cne 'NO' -or
       [string]$m.PciWrite -cne 'NO'){
        throw 'MANIFEST_CAPTURE_CONTRACT_MISMATCH'
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
    $it=Get-Content $inf -Raw
    if($it.IndexOf($ExtensionId,[StringComparison]::OrdinalIgnoreCase) -lt 0 -or
       $it.IndexOf($ExactHwid,[StringComparison]::OrdinalIgnoreCase) -lt 0){
        throw 'PACKAGE_INF_IDENTITY_MISMATCH'
    }
    [pscustomobject]@{
        Root=$r;Inf=$inf;Sys=$sys;Cat=$cat;Cer=$cer
        Thumb=[string]$c.Thumbprint;Manifest=$m
    }
}
function Native {
    if('Phaser360.H15eNative' -as [type]){return}
    $src=@'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
namespace Phaser360 {
  public static class H15eNative {
    const uint P=2,I=16,R=0x80000000,SR=1,SW=2,O=3;
    const int N=259;
    static readonly IntPtr X=new IntPtr(-1);
    [StructLayout(LayoutKind.Sequential)]
    struct D { public int cbSize; public Guid g; public int f; public UIntPtr r; }
    [DllImport("setupapi.dll",SetLastError=true)]
    static extern IntPtr SetupDiGetClassDevs(ref Guid g,IntPtr e,IntPtr h,uint f);
    [DllImport("setupapi.dll",SetLastError=true)]
    static extern bool SetupDiEnumDeviceInterfaces(IntPtr s,IntPtr d,ref Guid g,uint i,ref D x);
    [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr s,ref D x,IntPtr p,uint z,out uint n,IntPtr d);
    [DllImport("setupapi.dll",SetLastError=true)]
    static extern bool SetupDiDestroyDeviceInfoList(IntPtr s);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern SafeFileHandle CreateFile(string n,uint a,uint sh,IntPtr sa,uint c,uint f,IntPtr t);
    [DllImport("kernel32.dll",SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle h,uint c,IntPtr i,uint ib,byte[] o,uint ob,out uint r,IntPtr ov);
    static void Q(bool ok,string op) {
      if(!ok){int e=Marshal.GetLastWin32Error();throw new Win32Exception(e,op+"; WIN32_ERROR="+e);}
    }
    static string Path(Guid g) {
      IntPtr s=SetupDiGetClassDevs(ref g,IntPtr.Zero,IntPtr.Zero,P|I);
      if(s==X) throw new Win32Exception(Marshal.GetLastWin32Error());
      try {
        var d=new D(); d.cbSize=Marshal.SizeOf(typeof(D));
        Q(SetupDiEnumDeviceInterfaces(s,IntPtr.Zero,ref g,0,ref d),"Enum[0]");
        var d2=new D(); d2.cbSize=Marshal.SizeOf(typeof(D));
        bool b=SetupDiEnumDeviceInterfaces(s,IntPtr.Zero,ref g,1,ref d2);
        if(b||Marshal.GetLastWin32Error()!=N)
          throw new InvalidOperationException("EXPECTED_EXACTLY_ONE_H15E_INTERFACE");
        uint n=0;
        SetupDiGetDeviceInterfaceDetail(s,ref d,IntPtr.Zero,0,out n,IntPtr.Zero);
        IntPtr p=Marshal.AllocHGlobal((int)n);
        try {
          for(int k=0;k<n;k++) Marshal.WriteByte(p,k,0);
          Marshal.WriteInt32(p,0,8);
          Q(SetupDiGetDeviceInterfaceDetail(s,ref d,p,n,out n,IntPtr.Zero),"Detail");
          return Marshal.PtrToStringUni(IntPtr.Add(p,4));
        } finally { Marshal.FreeHGlobal(p); }
      } finally { SetupDiDestroyDeviceInfoList(s); }
    }
    public static byte[] Query(Guid g,uint io,int n) {
      string p=Path(g);
      using(var h=CreateFile(p,R,SR|SW,IntPtr.Zero,O,0,IntPtr.Zero)) {
        if(h.IsInvalid)
          throw new Win32Exception(Marshal.GetLastWin32Error(),"CreateFile H15E GENERIC_READ");
        var o=new byte[n]; uint z;
        Q(DeviceIoControl(h,io,IntPtr.Zero,0,o,(uint)o.Length,out z,IntPtr.Zero),
          "DeviceIoControl H15E snapshot");
        if(z!=n) throw new InvalidOperationException("RESULT_SIZE="+z);
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
function U16([byte[]]$b,[int]$o){[BitConverter]::ToUInt16($b,$o)}

if(-not (Admin)){throw 'ADMINISTRATOR_REQUIRED'}
if(-not [Environment]::Is64BitProcess){throw 'WINDOWS_X64_PROCESS_REQUIRED'}

$before=Target
AssertTarget $before
if(@($before.CompoundUpperFilters|Where-Object {
    $_ -ceq 'Phaser360H15cLive' -or $_ -ceq 'Phaser360H15dLive' -or $_ -ceq $Service
}).Count -ne 0){throw 'PHASER_DIAGNOSTIC_FILTER_ALREADY_ATTACHED'}
if(@(Published).Count -ne 0){throw 'H15E_LIVE_FILTER_ALREADY_PRESENT'}

$ci=CodeIntegrity
if(($ci -band 2) -eq 0){throw 'CODE_INTEGRITY_TESTSIGN_NOT_ALLOWED'}
$re=& (Join-Path $env:SystemRoot 'System32\reagentc.exe') /info 2>&1|Out-String
if($LASTEXITCODE -ne 0 -or $re -notmatch '(?im)Windows\s+RE.*(?:Enabled|Activat)'){
    throw 'WINRE_NOT_READY'
}

$pkg=Package $PackageRoot
$tb=Trust $pkg.Thumb
if($tb.Root -or $tb.TrustedPublisher){throw 'H15E_LIVE_PACKAGE_CERT_ALREADY_TRUSTED'}

if([string]::IsNullOrWhiteSpace($OutputRoot)){$OutputRoot=$PSScriptRoot}
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss'
$suffix=[Guid]::NewGuid().ToString('N').Substring(0,8)
$dir=Join-Path $OutputRoot ('H15E_LIVE_R0_TRANSACTION_'+$stamp+'_'+$suffix)
$backup=Join-Path $dir 'intel_baseline_export'
New-Item -ItemType Directory -Path $dir,$backup -Force|Out-Null
$before|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'target_before.json') -Encoding UTF8
Copy-Item (Join-Path $PSScriptRoot 'H15E_LIVE_WINRE_ROLLBACK.txt') $dir

$ex=PnP @('/export-driver',$before.DriverInfPath,$backup)
$ex.Output|Set-Content (Join-Path $dir 'pnputil_export_intel.txt') -Encoding UTF8
if($ex.ExitCode -ne 0 -or @(Get-ChildItem $backup -Recurse -File).Count -eq 0){
    throw 'BASELINE_EXPORT_FAILED'
}

$rootAdded=$false
$pubAdded=$false
$installed=$false
$publishedInf=$null
$captureAttempted=$false
$captureComplete=$false
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

    $installed=$true
    $x=PnP @('/add-driver',$pkg.Inf,'/install')
    $x.Output|Set-Content (Join-Path $dir 'pnputil_add_install.txt') -Encoding UTF8
    if($x.ExitCode -ne 0){throw 'FILTER_INSTALL_FAILED'}

    $p=@(Published)
    if($p.Count -ne 1){throw "EXPECTED_ONE_H15E_PUBLISHED_INF: count=$($p.Count)"}
    $publishedInf=$p[0]
    $publishedInf|Set-Content (Join-Path $dir 'H15ePublishedInf.txt') -Encoding ASCII

    $x=PnP @('/restart-device',$before.InstanceId)
    if($x.ExitCode -ne 0){throw 'FILTER_DEVICE_RESTART_FAILED'}
    $with=WaitHealthy $before.InstanceId
    $with|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'target_with_filter.json') -Encoding UTF8
    if(@($with.CompoundUpperFilters|Where-Object {$_ -ceq $Service}).Count -ne 1){
        throw 'H15E_FILTER_NOT_OBSERVED_IN_COMPOUND_UPPER_FILTERS'
    }

    Native
    $captureAttempted=$true
    $r=[Phaser360.H15eNative]::Query($InterfaceGuid,$Ioctl,$SnapshotBytes)

    $v=U32 $r 0
    $sz=U32 $r 4
    $nt=I32 $r 8
    $fl=U32 $r 12
    $gen=U32 $r 16
    $ven=U16 $r 20
    $dev=U16 $r 22
    $hdr=[uint32]$r[24]
    $fc=[uint32]$r[25]
    $cc=[uint32]$r[26]
    $pg=U32 $r 28
    $cg=U32 $r 32
    $hda=U64 $r 40
    $dsp=U64 $r 48
    $hdaLen=U32 $r 56
    $dspLen=U32 $r 60
    $gcap=U16 $r 64
    $vmin=[uint32]$r[66]
    $vmaj=[uint32]$r[67]
    $gctl=U32 $r 68
    $em2=U32 $r 72
    $adspcs=U32 $r 76
    $adspis=U32 $r 80
    $hipci=U32 $r 84
    $hipcie=U32 $r 88
    $rom=U32 $r 92

    [ordered]@{
        Version=$v;Size=$sz;NtStatus=('0x{0:X8}' -f ([uint32]$nt))
        Flags=('0x{0:X8}' -f $fl);Generation=$gen
        Vendor=('0x{0:X4}' -f $ven);Device=('0x{0:X4}' -f $dev)
        HeaderType=('0x{0:X2}' -f $hdr)
        FirstCapability=('0x{0:X2}' -f $fc);CapabilityCount=$cc
        Pgctl=('0x{0:X8}' -f $pg);Cgctl=('0x{0:X8}' -f $cg)
        HdaPhysical=('0x{0:X16}' -f $hda);DspPhysical=('0x{0:X16}' -f $dsp)
        HdaLength=('0x{0:X8}' -f $hdaLen);DspLength=('0x{0:X8}' -f $dspLen)
        HdaGcap=('0x{0:X4}' -f $gcap);HdaVmin=('0x{0:X2}' -f $vmin)
        HdaVmaj=('0x{0:X2}' -f $vmaj);HdaGctl=('0x{0:X8}' -f $gctl)
        HdaIntelEm2=('0x{0:X8}' -f $em2)
        DspAdspcs=('0x{0:X8}' -f $adspcs);DspAdspis=('0x{0:X8}' -f $adspis)
        DspHipci=('0x{0:X8}' -f $hipci);DspHipcie=('0x{0:X8}' -f $hipcie)
        DspRomStatus=('0x{0:X8}' -f $rom)
    }|ConvertTo-Json -Depth 6|Set-Content (Join-Path $dir 'mmio_snapshot.json') -Encoding UTF8

    if($v -ne 1 -or $sz -ne 104 -or $nt -lt 0 -or
       ($fl -band $RequiredFlags) -ne $RequiredFlags -or
       $ven -ne 0x8086 -or $dev -ne 0x3198 -or
       $pg -ne $ExpectedPg -or $cg -ne $ExpectedCg -or
       $hda -eq 0 -or $dsp -eq 0 -or $hda -eq $dsp -or
       $hdaLen -ne 0x4000 -or $dspLen -ne 0x100000 -or
       $gcap -eq 0xffff -or $gctl -eq 0xffffffff -or $em2 -eq 0xffffffff -or
       $adspcs -eq 0xffffffff -or $adspis -eq 0xffffffff){
        throw 'H15E_LIVE_SNAPSHOT_VALIDATION_FAILED'
    }
    $captureComplete=$true

    $x=PnP @('/delete-driver',$publishedInf,'/uninstall','/force')
    if($x.ExitCode -ne 0){throw 'FILTER_UNINSTALL_FAILED'}

    $x=PnP @('/restart-device',$before.InstanceId)
    if($x.ExitCode -ne 0){throw 'BASELINE_DEVICE_RESTART_FAILED'}
    $after=WaitHealthy $before.InstanceId

    if(@(Published).Count -ne 0){throw 'H15E_FILTER_PACKAGE_REMAINS'}
    if(@($after.CompoundUpperFilters|Where-Object {$_ -ceq $Service}).Count -ne 0){
        throw 'H15E_FILTER_REMAINS_IN_COMPOUND_UPPER_FILTERS'
    }
    foreach($n in @('InstanceId','Service','DriverInfPath','DriverVersion','DriverProvider')){
        if([string]$before.$n -cne [string]$after.$n){
            throw "BASELINE_IDENTITY_CHANGED: $n"
        }
    }

    $x=CertUtil @('-delstore','TrustedPublisher',$pkg.Thumb)
    if($x.ExitCode -ne 0){throw 'CERT_PUBLISHER_REMOVE_FAILED'}
    $ta=Trust $pkg.Thumb
    if($ta.TrustedPublisher){throw 'CERT_PUBLISHER_REMAINS'}
    $pubAdded=$false

    $x=CertUtil @('-delstore','Root',$pkg.Thumb)
    if($x.ExitCode -ne 0){throw 'CERT_ROOT_REMOVE_FAILED'}
    $ta=Trust $pkg.Thumb
    if($ta.Root){throw 'CERT_ROOT_REMAINS'}
    $rootAdded=$false

    if($ta.Root -or $ta.TrustedPublisher){throw 'CERT_TRUST_REMAINS'}
    $after|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'target_after.json') -Encoding UTF8
    $normal=$true
} catch {
    $err=$_.Exception
} finally {
    if(-not $normal){
        $log=@()
        if($installed){
            foreach($p in @(Published)){
                $q=PnP @('/delete-driver',$p,'/uninstall','/force')
                $log+="DELETE $p EXIT=$($q.ExitCode)"
                $log+=$q.Output
            }
            try{
                $q=PnP @('/restart-device',$before.InstanceId)
                $log+="RESTART EXIT=$($q.ExitCode)"
            }catch{$log+="RESTART_EXCEPTION=$($_.Exception.Message)"}
        }

        $safe=$false
        try{
            $s=Target
            $safe=@(Published).Count -eq 0 -and
                $s.Status -ceq 'OK' -and $s.ProblemCode -eq 0 -and
                $s.InstanceId -ceq $before.InstanceId -and
                $s.Service -ceq $before.Service -and
                $s.DriverInfPath -ceq $before.DriverInfPath -and
                $s.DriverVersion -ceq $before.DriverVersion -and
                $s.DriverProvider -ceq $before.DriverProvider -and
                @($s.HardwareIds|Where-Object {$_ -ceq $ExactHwid}).Count -eq 1 -and
                @($s.CompoundUpperFilters|Where-Object {$_ -ceq $Service}).Count -eq 0
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
$rem=@(Published)
$ft=Trust $pkg.Thumb
$baseline=$false
if($final){
    $baseline=$final.Status -ceq 'OK' -and $final.ProblemCode -eq 0 -and
        $final.InstanceId -ceq $before.InstanceId -and
        $final.Service -ceq $before.Service -and
        $final.DriverInfPath -ceq $before.DriverInfPath -and
        $final.DriverVersion -ceq $before.DriverVersion -and
        $final.DriverProvider -ceq $before.DriverProvider -and
        @($final.HardwareIds|Where-Object {$_ -ceq $ExactHwid}).Count -eq 1 -and
        @($final.CompoundUpperFilters|Where-Object {$_ -ceq $Service}).Count -eq 0 -and
        $rem.Count -eq 0 -and -not $ft.Root -and -not $ft.TrustedPublisher
}

$status=if($normal -and $baseline -and $captureComplete -and -not $err){
    'H15E_LIVE_R0_CAPTURE_AND_ROLLBACK_COMPLETE'
}else{'H15E_LIVE_R0_TRANSACTION_FAILED'}

[ordered]@{
    Status=$status;PublishedInf=$publishedInf
    CaptureAttempted=$captureAttempted;CaptureCompleted=$captureComplete
    BaselineRestored=$baseline
    TrustRestored=(-not $ft.Root -and -not $ft.TrustedPublisher)
    TransactionError=$(if($err){$err.Message}else{$null})
    PciConfigRead='YES_ATTESTATION'
    PciConfigWrite='NO'
    MmioRead='REVIEWED_REGISTERS_ONLY'
    MmioWrite='NO'
    MmioMapping='PAGE_READONLY_NOCACHE_TRANSIENT'
    Dma='NO';Irq='NO';DspBoot='NO';AudioPlayback='NO'
    SystemReboot='NO';BcdWrite='NO';DeviceRestarts='TARGET_DEV3198_ONLY'
}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $dir 'transaction.json') -Encoding UTF8

Hashes $dir
$zip=Join-Path $OutputRoot ('RESULT_H15E_LIVE_R0_TRANSACTION_'+$stamp+'_'+$suffix+'.zip')
Compress-Archive -Path (Join-Path $dir '*') -DestinationPath $zip -Force

Write-Host "STATUS=$status"
Write-Host "CAPTURE_COMPLETED=$($captureComplete.ToString().ToUpperInvariant())"
Write-Host "BASELINE_RESTORED=$($baseline.ToString().ToUpperInvariant())"
Write-Host "TRUST_RESTORED=$(((-not $ft.Root -and -not $ft.TrustedPublisher)).ToString().ToUpperInvariant())"
Write-Host "PUBLISHED_INF=$publishedInf"
Write-Host "Trimite fisierul: $zip"
if($err){throw $err}
if(-not $normal -or -not $baseline -or -not $captureComplete){exit 3}
