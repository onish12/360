#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$PackageRoot,[string]$OutputRoot='')
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$ExactHwid='PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06'
$ExtensionId='{53f678f1-2b3c-4b2e-a15d-360031980001}'
$Service='Phaser360H15dLive'
$CertSubject='CN=PHASER360 H15D-LIVE R1 Ephemeral Test Signing'
$InterfaceGuid=[Guid]'8c1b3150-6d0c-4c88-9d36-15d000319801'
$Ioctl=[Convert]::ToUInt32('8338E458',16)
$ExpectedPg=[Convert]::ToUInt32('00000010',16)
$ExpectedCg=[Convert]::ToUInt32('807B0DFF',16)
$AppliedPg=[Convert]::ToUInt32('00000014',16)
$AppliedCg=[Convert]::ToUInt32('807B0DFD',16)
$RequiredFlags=[Convert]::ToUInt32('000007FF',16)

function Admin {
 $id=[Security.Principal.WindowsIdentity]::GetCurrent()
 ([Security.Principal.WindowsPrincipal]::new($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}
function Target {
 $d=@(Get-PnpDevice -PresentOnly -ErrorAction Stop|Where-Object {$_.InstanceId -like 'PCI\VEN_8086&DEV_3198*'})
 if($d.Count-ne1){throw "EXPECTED_ONE_PRESENT_DEV3198: count=$($d.Count)"}
 $id=[string]$d[0].InstanceId;$p=@(Get-PnpDeviceProperty -InstanceId $id -ErrorAction Stop);$m=@{};foreach($x in $p){$m[[string]$x.KeyName]=$x.Data}
 $upper=@();try{$u=@(Get-PnpDeviceProperty -InstanceId $id -KeyName 'DEVPKEY_Device_CompoundUpperFilters' -ErrorAction Stop);if($u.Count-eq1){$upper=@($u[0].Data)}}catch{}
 [pscustomobject]@{InstanceId=$id;Status=[string]$d[0].Status;ProblemCode=[int]$m['DEVPKEY_Device_ProblemCode'];Service=[string]$m['DEVPKEY_Device_Service'];DriverInfPath=[string]$m['DEVPKEY_Device_DriverInfPath'];DriverVersion=[string]$m['DEVPKEY_Device_DriverVersion'];DriverProvider=[string]$m['DEVPKEY_Device_DriverProvider'];HardwareIds=@($m['DEVPKEY_Device_HardwareIds']);CompoundUpperFilters=$upper}
}
function AssertTarget($s){
 if([Environment]::OSVersion.Version.Build-ne19044){throw 'EXACT_WINDOWS_BUILD_19044_REQUIRED'}
 if($s.Status-cne'OK'-or$s.ProblemCode-ne0-or$s.Service-cne'IntcAudioBus'){throw 'TARGET_NOT_HEALTHY_ON_INTC_AUDIO_BUS'}
 if(@($s.HardwareIds|Where-Object {$_-ceq$ExactHwid}).Count-ne1){throw 'EXACT_HARDWARE_ID_NOT_PRESENT'}
}
function PnP([string[]]$a){$e=Join-Path $env:SystemRoot 'System32\pnputil.exe';$old=$ErrorActionPreference;$ErrorActionPreference='Continue';try{$o=(& $e @a 2>&1|Out-String -Width 8192);$c=$LASTEXITCODE}finally{$ErrorActionPreference=$old};[pscustomobject]@{ExitCode=$c;Output=$o}}
function CertUtil([string[]]$a){$e=Join-Path $env:SystemRoot 'System32\certutil.exe';$old=$ErrorActionPreference;$ErrorActionPreference='Continue';try{$o=(& $e @a 2>&1|Out-String -Width 8192);$c=$LASTEXITCODE}finally{$ErrorActionPreference=$old};[pscustomobject]@{ExitCode=$c;Output=$o}}
function Published {
 $r=@();foreach($f in @(Get-ChildItem (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' -File)){try{$t=Get-Content $f.FullName -Raw}catch{continue};if($t.IndexOf($ExtensionId,[StringComparison]::OrdinalIgnoreCase)-ge0-and$t.IndexOf($Service,[StringComparison]::OrdinalIgnoreCase)-ge0){$r+=$f.Name}};@($r)
}
function WaitHealthy([string]$id){
 $end=(Get-Date).AddSeconds(15);do{Start-Sleep -Milliseconds 500;try{$s=Target}catch{$s=$null};if($s-and$s.InstanceId-ceq$id-and$s.Status-ceq'OK'-and$s.ProblemCode-eq0-and$s.Service-ceq'IntcAudioBus'){return $s}}while((Get-Date)-lt$end);throw 'TARGET_DID_NOT_RETURN_HEALTHY'
}
function CodeIntegrity {
 if(-not('Phaser360.H15dCi'-as[type])){Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;namespace Phaser360{public static class H15dCi{[StructLayout(LayoutKind.Sequential)]struct CI{public UInt32 Length;public UInt32 Options;}[DllImport("ntdll.dll")]static extern Int32 NtQuerySystemInformation(Int32 c,ref CI i,UInt32 l,IntPtr r);public static UInt32 Get(){CI i=new CI();i.Length=(UInt32)Marshal.SizeOf(typeof(CI));Int32 s=NtQuerySystemInformation(103,ref i,i.Length,IntPtr.Zero);if(s<0)throw new InvalidOperationException("CI=0x"+unchecked((UInt32)s).ToString("X8"));return i.Options;}}}' -Language CSharp}
 [uint32][Phaser360.H15dCi]::Get()
}
function Trust([string]$thumb){$t=$thumb.Replace(' ','').ToUpperInvariant();[pscustomobject]@{Root=(Test-Path "Cert:\LocalMachine\Root\$t");TrustedPublisher=(Test-Path "Cert:\LocalMachine\TrustedPublisher\$t")}}
function Hashes([string]$d){Get-ChildItem $d -Recurse -File|Where-Object {$_.Name-ne'SHA256SUMS.txt'}|Sort-Object FullName|ForEach-Object {(Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()+'  '+$_.FullName.Substring($d.Length).TrimStart('\')}|Set-Content (Join-Path $d 'SHA256SUMS.txt') -Encoding ASCII}
function Package([string]$root,[switch]$Trusted){
 $r=(Resolve-Path $root).Path;$inf=Join-Path $r 'phaser360_h15d_live_filter.inf';$sys=Join-Path $r 'phaser360_h15d_live_filter.sys';$cat=Join-Path $r 'phaser360_h15d_live_filter.cat';$cer=Join-Path $r 'phaser360_h15d_live_filter.cer';$man=Join-Path $r 'package_manifest.json'
 foreach($p in @($inf,$sys,$cat,$cer,$man)){if(-not(Test-Path $p -PathType Leaf)){throw "PACKAGE_FILE_MISSING: $p"}}
 $c=Get-PfxCertificate $cer;if(-not$c-or$c.HasPrivateKey-or$c.Subject-cne$CertSubject-or$c.Issuer-cne$CertSubject){throw 'CERTIFICATE_IDENTITY_INVALID'}
 if($c.NotBefore.ToUniversalTime()-gt[DateTime]::UtcNow-or$c.NotAfter.ToUniversalTime()-le[DateTime]::UtcNow){throw 'CERTIFICATE_NOT_CURRENTLY_VALID'}
 $eku=@();foreach($e in $c.Extensions){if($e.Oid.Value-eq'2.5.29.37'){$te=[System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]$e;foreach($o in $te.EnhancedKeyUsages){$eku+=[string]$o.Value}}};if(@($eku|Where-Object {$_-ceq'1.3.6.1.5.5.7.3.3'}).Count-ne1){throw 'CODE_SIGNING_EKU_MISSING'}
 $m=Get-Content $man -Raw|ConvertFrom-Json;if([string]$m.CertificateThumbprint-cne[string]$c.Thumbprint){throw 'MANIFEST_CERTIFICATE_THUMBPRINT_MISMATCH'}
 if([string]$m.Purpose-cne'H15D_LIVE_R1_BOUNDED_PCI_TRANSACTION_PACKAGE'-or[string]$m.ExpectedPgctl-cne'0x00000010'-or[string]$m.ExpectedCgctl-cne'0x807B0DFF'-or[string]$m.AppliedPgctl-cne'0x00000014'-or[string]$m.AppliedCgctl-cne'0x807B0DFD'-or[string]$m.PciConfigWrite-cne'ONLY_0x44_BIT2_AND_0x48_BIT1_WITH_EXACT_RESTORE'){throw 'MANIFEST_TRANSACTION_CONTRACT_MISMATCH'}
 foreach($x in @(@($inf,[string]$m.InfSha256),@($sys,[string]$m.SysSha256),@($cat,[string]$m.CatSha256),@($cer,[string]$m.CerSha256))){if((Get-FileHash $x[0] -Algorithm SHA256).Hash.ToLowerInvariant()-cne$x[1].ToLowerInvariant()){throw 'MANIFEST_HASH_MISMATCH'}}
 $ss=Get-AuthenticodeSignature $sys;$cs=Get-AuthenticodeSignature $cat;if(-not$ss.SignerCertificate-or-not$cs.SignerCertificate){throw 'SIGNER_MISSING'};if([string]$ss.SignerCertificate.Thumbprint-cne[string]$c.Thumbprint-or[string]$cs.SignerCertificate.Thumbprint-cne[string]$c.Thumbprint){throw 'SIGNER_CERT_MISMATCH'}
 if($Trusted-and($ss.Status-ne'Valid'-or$cs.Status-ne'Valid')){throw 'SIGNATURE_NOT_VALID_AFTER_TRUST'}
 $it=Get-Content $inf -Raw;if($it.IndexOf($ExtensionId,[StringComparison]::OrdinalIgnoreCase)-lt0-or$it.IndexOf($ExactHwid,[StringComparison]::OrdinalIgnoreCase)-lt0){throw 'PACKAGE_INF_IDENTITY_MISMATCH'}
 [pscustomobject]@{Root=$r;Inf=$inf;Sys=$sys;Cat=$cat;Cer=$cer;Thumb=[string]$c.Thumbprint;Manifest=$m}
}
function Native {
 if('Phaser360.H15dNative'-as[type]){return}
 $src=@'
using System;using System.ComponentModel;using System.Runtime.InteropServices;using Microsoft.Win32.SafeHandles;
namespace Phaser360{public static class H15dNative{
 const uint P=2,I=16,R=0x80000000,W=0x40000000,SR=1,SW=2,O=3;const int N=259;static readonly IntPtr X=new IntPtr(-1);
 [StructLayout(LayoutKind.Sequential)]struct D{public int cbSize;public Guid g;public int f;public UIntPtr r;}
 [DllImport("setupapi.dll",SetLastError=true)]static extern IntPtr SetupDiGetClassDevs(ref Guid g,IntPtr e,IntPtr h,uint f);
 [DllImport("setupapi.dll",SetLastError=true)]static extern bool SetupDiEnumDeviceInterfaces(IntPtr s,IntPtr d,ref Guid g,uint i,ref D x);
 [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr s,ref D x,IntPtr p,uint z,out uint n,IntPtr d);
 [DllImport("setupapi.dll",SetLastError=true)]static extern bool SetupDiDestroyDeviceInfoList(IntPtr s);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern SafeFileHandle CreateFile(string n,uint a,uint sh,IntPtr sa,uint c,uint f,IntPtr t);
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool DeviceIoControl(SafeFileHandle h,uint c,byte[] i,uint ib,byte[] o,uint ob,out uint r,IntPtr ov);
 static void Q(bool ok,string op){if(!ok){int e=Marshal.GetLastWin32Error();throw new Win32Exception(e,op+"; WIN32_ERROR="+e);}}
 static string Path(Guid g){IntPtr s=SetupDiGetClassDevs(ref g,IntPtr.Zero,IntPtr.Zero,P|I);if(s==X)throw new Win32Exception(Marshal.GetLastWin32Error());try{var d=new D();d.cbSize=Marshal.SizeOf(typeof(D));Q(SetupDiEnumDeviceInterfaces(s,IntPtr.Zero,ref g,0,ref d),"Enum[0]");var d2=new D();d2.cbSize=Marshal.SizeOf(typeof(D));bool b=SetupDiEnumDeviceInterfaces(s,IntPtr.Zero,ref g,1,ref d2);if(b||Marshal.GetLastWin32Error()!=N)throw new InvalidOperationException("EXPECTED_EXACTLY_ONE_H15D_INTERFACE");uint n=0;SetupDiGetDeviceInterfaceDetail(s,ref d,IntPtr.Zero,0,out n,IntPtr.Zero);IntPtr p=Marshal.AllocHGlobal((int)n);try{for(int k=0;k<n;k++)Marshal.WriteByte(p,k,0);Marshal.WriteInt32(p,0,8);Q(SetupDiGetDeviceInterfaceDetail(s,ref d,p,n,out n,IntPtr.Zero),"Detail");return Marshal.PtrToStringUni(IntPtr.Add(p,4));}finally{Marshal.FreeHGlobal(p);}}finally{SetupDiDestroyDeviceInfoList(s);}}
 public static byte[] Go(Guid g,uint io,byte[] input,int n){string p=Path(g);using(var h=CreateFile(p,R|W,SR|SW,IntPtr.Zero,O,0,IntPtr.Zero)){if(h.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error(),"CreateFile H15D GENERIC_READ|GENERIC_WRITE");var o=new byte[n];uint z;Q(DeviceIoControl(h,io,input,(uint)input.Length,o,(uint)o.Length,out z,IntPtr.Zero),"DeviceIoControl H15D transaction");if(z!=n)throw new InvalidOperationException("RESULT_SIZE="+z);return o;}}
}}
'@
 Add-Type -TypeDefinition $src -Language CSharp -ErrorAction Stop
}
function U32([byte[]]$b,[int]$o){[BitConverter]::ToUInt32($b,$o)} function I32([byte[]]$b,[int]$o){[BitConverter]::ToInt32($b,$o)} function U16([byte[]]$b,[int]$o){[BitConverter]::ToUInt16($b,$o)}
function Put32([byte[]]$b,[int]$o,[uint32]$v){[Array]::Copy([BitConverter]::GetBytes($v),0,$b,$o,4)}

if(-not(Admin)){throw 'ADMINISTRATOR_REQUIRED'};if(-not[Environment]::Is64BitProcess){throw 'WINDOWS_X64_PROCESS_REQUIRED'}
$before=Target;AssertTarget $before;if(@(Published).Count-ne0){throw 'H15D_LIVE_FILTER_ALREADY_PRESENT'}
$ci=CodeIntegrity;if(($ci-band2)-eq0){throw 'CODE_INTEGRITY_TESTSIGN_NOT_ALLOWED'}
$re=& (Join-Path $env:SystemRoot 'System32\reagentc.exe') /info 2>&1|Out-String;if($LASTEXITCODE-ne0-or$re-notmatch'(?im)Windows\s+RE.*(?:Enabled|Activat)'){throw 'WINRE_NOT_READY'}
$pkg=Package $PackageRoot;$tb=Trust $pkg.Thumb;if($tb.Root-or$tb.TrustedPublisher){throw 'H15D_LIVE_PACKAGE_CERT_ALREADY_TRUSTED'}
if([string]::IsNullOrWhiteSpace($OutputRoot)){$OutputRoot=$PSScriptRoot};$stamp=Get-Date -Format 'yyyyMMdd_HHmmss';$suffix=[Guid]::NewGuid().ToString('N').Substring(0,8);$dir=Join-Path $OutputRoot ('H15D_LIVE_R1_TRANSACTION_'+$stamp+'_'+$suffix);$backup=Join-Path $dir 'intel_baseline_export';New-Item -ItemType Directory -Path $dir,$backup -Force|Out-Null
$before|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'target_before.json') -Encoding UTF8
Copy-Item (Join-Path $PSScriptRoot 'H15D_LIVE_WINRE_ROLLBACK.txt') $dir
$ex=PnP @('/export-driver',$before.DriverInfPath,$backup);$ex.Output|Set-Content (Join-Path $dir 'pnputil_export_intel.txt');if($ex.ExitCode-ne0-or@(Get-ChildItem $backup -Recurse -File).Count-eq0){throw 'BASELINE_EXPORT_FAILED'}

$rootAdded=$false;$pubAdded=$false;$installed=$false;$publishedInf=$null;$ioComplete=$false;$normal=$false;$err=$null
try{
 $x=CertUtil @('-f','-addstore','Root',$pkg.Cer);if($x.ExitCode-ne0){throw 'CERT_ROOT_ADD_FAILED'};$rootAdded=$true
 $x=CertUtil @('-f','-addstore','TrustedPublisher',$pkg.Cer);if($x.ExitCode-ne0){throw 'CERT_PUBLISHER_ADD_FAILED'};$pubAdded=$true
 $tn=Trust $pkg.Thumb;if(-not$tn.Root-or-not$tn.TrustedPublisher){throw 'CERT_TRUST_NOT_PRESENT'};$null=Package $PackageRoot -Trusted
 $installed=$true;$x=PnP @('/add-driver',$pkg.Inf,'/install');$x.Output|Set-Content (Join-Path $dir 'pnputil_add_install.txt');if($x.ExitCode-ne0){throw 'FILTER_INSTALL_FAILED'}
 $p=@(Published);if($p.Count-ne1){throw "EXPECTED_ONE_H15D_PUBLISHED_INF: count=$($p.Count)"};$publishedInf=$p[0];$publishedInf|Set-Content (Join-Path $dir 'H15dPublishedInf.txt')
 $x=PnP @('/restart-device',$before.InstanceId);if($x.ExitCode-ne0){throw 'FILTER_DEVICE_RESTART_FAILED'};$with=WaitHealthy $before.InstanceId;$with|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'target_with_filter.json')
 Native;$req=[byte[]]::new(16);Put32 $req 0 1;Put32 $req 4 16;Put32 $req 8 $ExpectedPg;Put32 $req 12 $ExpectedCg;$r=[Phaser360.H15dNative]::Go($InterfaceGuid,$Ioctl,$req,52)
 $v=U32 $r 0;$sz=U32 $r 4;$nt=I32 $r 8;$fl=U32 $r 12;$gen=U32 $r 16;$ven=U16 $r 20;$dev=U16 $r 22;$pg0=U32 $r 28;$cg0=U32 $r 32;$pg1=U32 $r 36;$cg1=U32 $r 40;$pg2=U32 $r 44;$cg2=U32 $r 48
 [ordered]@{Version=$v;Size=$sz;NtStatus=('0x{0:X8}'-f([uint32]$nt));Flags=('0x{0:X8}'-f$fl);Generation=$gen;Vendor=('0x{0:X4}'-f$ven);Device=('0x{0:X4}'-f$dev);PgBefore=('0x{0:X8}'-f$pg0);CgBefore=('0x{0:X8}'-f$cg0);PgApplied=('0x{0:X8}'-f$pg1);CgApplied=('0x{0:X8}'-f$cg1);PgRestored=('0x{0:X8}'-f$pg2);CgRestored=('0x{0:X8}'-f$cg2)}|ConvertTo-Json|Set-Content (Join-Path $dir 'pci_transaction.json') -Encoding UTF8
 if($v-ne1-or$sz-ne52-or$nt-lt0-or($fl-band$RequiredFlags)-ne$RequiredFlags-or$ven-ne0x8086-or$dev-ne0x3198-or$pg0-ne$ExpectedPg-or$cg0-ne$ExpectedCg-or$pg1-ne$AppliedPg-or$cg1-ne$AppliedCg-or$pg2-ne$ExpectedPg-or$cg2-ne$ExpectedCg){throw 'H15D_LIVE_TRANSACTION_VALIDATION_FAILED'}
 $ioComplete=$true
 $x=PnP @('/delete-driver',$publishedInf,'/uninstall','/force');if($x.ExitCode-ne0){throw 'FILTER_UNINSTALL_FAILED'}
 $x=PnP @('/restart-device',$before.InstanceId);if($x.ExitCode-ne0){throw 'BASELINE_DEVICE_RESTART_FAILED'};$after=WaitHealthy $before.InstanceId
 if(@(Published).Count-ne0){throw 'H15D_FILTER_PACKAGE_REMAINS'}
 foreach($n in @('InstanceId','Service','DriverInfPath','DriverVersion','DriverProvider')){if([string]$before.$n-cne[string]$after.$n){throw "BASELINE_IDENTITY_CHANGED: $n"}}
 $x=CertUtil @('-delstore','TrustedPublisher',$pkg.Thumb);if($x.ExitCode-ne0){throw 'CERT_PUBLISHER_REMOVE_FAILED'};$pubAdded=$false
 $x=CertUtil @('-delstore','Root',$pkg.Thumb);if($x.ExitCode-ne0){throw 'CERT_ROOT_REMOVE_FAILED'};$rootAdded=$false
 $ta=Trust $pkg.Thumb;if($ta.Root-or$ta.TrustedPublisher){throw 'CERT_TRUST_REMAINS'};$after|ConvertTo-Json -Depth 8|Set-Content (Join-Path $dir 'target_after.json') -Encoding UTF8;$normal=$true
}catch{$err=$_.Exception}finally{
 if(-not$normal){$log=@();if($installed){foreach($p in @(Published)){$q=PnP @('/delete-driver',$p,'/uninstall','/force');$log+="DELETE $p EXIT=$($q.ExitCode)";$log+=$q.Output};try{$q=PnP @('/restart-device',$before.InstanceId);$log+="RESTART EXIT=$($q.ExitCode)"}catch{$log+="RESTART_EXCEPTION=$($_.Exception.Message)"}}
  $safe=$false;try{$s=Target;$safe=@(Published).Count-eq0-and$s.Status-ceq'OK'-and$s.ProblemCode-eq0-and$s.InstanceId-ceq$before.InstanceId-and$s.Service-ceq$before.Service-and$s.DriverInfPath-ceq$before.DriverInfPath}catch{}
  if($safe){if($pubAdded){$q=CertUtil @('-delstore','TrustedPublisher',$pkg.Thumb);if($q.ExitCode-eq0){$pubAdded=$false}};if($rootAdded){$q=CertUtil @('-delstore','Root',$pkg.Thumb);if($q.ExitCode-eq0){$rootAdded=$false}}}else{$log+='TRUST_RETAINED_FOR_SAFETY=TRUE'}
  $log|Set-Content (Join-Path $dir 'emergency_rollback.txt') -Encoding UTF8
 }
}
$final=$null;try{$final=Target}catch{};$rem=@(Published);$ft=Trust $pkg.Thumb;$baseline=$false;if($final){$baseline=$final.Status-ceq'OK'-and$final.ProblemCode-eq0-and$final.InstanceId-ceq$before.InstanceId-and$final.Service-ceq$before.Service-and$final.DriverInfPath-ceq$before.DriverInfPath-and$rem.Count-eq0-and-not$ft.Root-and-not$ft.TrustedPublisher}
[ordered]@{Status=$(if($normal-and$baseline-and$ioComplete-and-not$err){'H15D_LIVE_R1_WRITE_RESTORE_AND_ROLLBACK_COMPLETE'}else{'H15D_LIVE_R1_TRANSACTION_FAILED'});PublishedInf=$publishedInf;WriteRestoreCompleted=$ioComplete;BaselineRestored=$baseline;TrustRestored=(-not$ft.Root-and-not$ft.TrustedPublisher);TransactionError=$(if($err){$err.Message}else{$null});PciConfigWrite='ONLY_0x44_BIT2_AND_0x48_BIT1_WITH_EXACT_RESTORE';Mmio='NO';Dma='NO';DspBoot='NO';AudioPlayback='NO';SystemReboot='NO';BcdWrite='NO';DeviceRestarts='TARGET_DEV3198_ONLY'}|ConvertTo-Json -Depth 6|Set-Content (Join-Path $dir 'transaction.json') -Encoding UTF8
Hashes $dir;$zip=Join-Path $OutputRoot ('RESULT_H15D_LIVE_R1_TRANSACTION_'+$stamp+'_'+$suffix+'.zip');Compress-Archive -Path (Join-Path $dir '*') -DestinationPath $zip -Force
Write-Host $(if($normal-and$baseline-and$ioComplete-and-not$err){'STATUS=H15D_LIVE_R1_WRITE_RESTORE_AND_ROLLBACK_COMPLETE'}else{'STATUS=H15D_LIVE_R1_TRANSACTION_FAILED'})
Write-Host "WRITE_RESTORE_COMPLETED=$($ioComplete.ToString().ToUpperInvariant())";Write-Host "BASELINE_RESTORED=$($baseline.ToString().ToUpperInvariant())";Write-Host "TRUST_RESTORED=$(((-not$ft.Root-and-not$ft.TrustedPublisher)).ToString().ToUpperInvariant())";Write-Host "PUBLISHED_INF=$publishedInf";Write-Host "Trimite fisierul: $zip"
if($err){throw $err};if(-not$normal-or-not$baseline-or-not$ioComplete){exit 3}
