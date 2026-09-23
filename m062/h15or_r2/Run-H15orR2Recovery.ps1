#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$PackageRoot,[string]$OutputRoot='')
$ErrorActionPreference='Stop';Set-StrictMode -Version 2
$ExactHwid='PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06'
$ProbeService='Phaser360H15orR2';$H15oService='Phaser360H15o'
$ProbeSubject='CN=PHASER360 H15OR R2 Readonly Recovery Signing'
$H15oSubject='CN=PHASER360 H15O Ephemeral Test Signing'
$Ioctl=[Convert]::ToUInt32('8343E48C',16);$ResultBytes=120
$BaselineInf='oem14.inf';$BaselineVersion='9.22.0.4832';$BaselineProvider='Intel(R) Corporation'

function Admin{$id=[Security.Principal.WindowsIdentity]::GetCurrent();([Security.Principal.WindowsPrincipal]::new($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)}
function Target{
 $d=@(Get-PnpDevice -PresentOnly -ErrorAction Stop|Where-Object{$_.InstanceId -like 'PCI\VEN_8086&DEV_3198*'});if($d.Count-ne1){throw "EXPECTED_ONE_PRESENT_DEV3198: count=$($d.Count)"}
 $id=[string]$d[0].InstanceId;$p=@(Get-PnpDeviceProperty -InstanceId $id -ErrorAction Stop);$m=@{};foreach($x in $p){$m[[string]$x.KeyName]=$x.Data}
 [pscustomobject]@{InstanceId=$id;Status=[string]$d[0].Status;ProblemCode=[int]$m['DEVPKEY_Device_ProblemCode'];Service=[string]$m['DEVPKEY_Device_Service'];DriverInfPath=[string]$m['DEVPKEY_Device_DriverInfPath'];DriverVersion=[string]$m['DEVPKEY_Device_DriverVersion'];DriverProvider=[string]$m['DEVPKEY_Device_DriverProvider'];HardwareIds=@($m['DEVPKEY_Device_HardwareIds']);BusNumber=[int]$m['DEVPKEY_Device_BusNumber'];Address=[int]$m['DEVPKEY_Device_Address']}
}
function IsIntel($s){$s.Status -ceq 'OK' -and $s.ProblemCode-eq0 -and $s.Service -ceq 'IntcAudioBus' -and $s.DriverInfPath -cne '' -and $s.DriverVersion -ceq $BaselineVersion -and $s.DriverProvider -ceq $BaselineProvider}
function PnP([string[]]$a){$e=Join-Path $env:SystemRoot 'System32\pnputil.exe';$old=$ErrorActionPreference;$ErrorActionPreference='Continue';try{$o=(& $e @a 2>&1|Out-String -Width 8192);$c=$LASTEXITCODE}finally{$ErrorActionPreference=$old};[pscustomobject]@{ExitCode=$c;Output=$o}}
function InvokeScExe([string[]]$a){$e=Join-Path $env:SystemRoot 'System32\sc.exe';$old=$ErrorActionPreference;$ErrorActionPreference='Continue';try{$o=(& $e @a 2>&1|Out-String -Width 8192);$c=$LASTEXITCODE}finally{$ErrorActionPreference=$old};[pscustomobject]@{ExitCode=$c;Output=$o}}
function CertUtil([string[]]$a){$e=Join-Path $env:SystemRoot 'System32\certutil.exe';$old=$ErrorActionPreference;$ErrorActionPreference='Continue';try{$o=(& $e @a 2>&1|Out-String -Width 8192);$c=$LASTEXITCODE}finally{$ErrorActionPreference=$old};[pscustomobject]@{ExitCode=$c;Output=$o}}
function Trust([string]$thumb){$t=$thumb.Replace(' ','').ToUpperInvariant();[pscustomobject]@{Root=(Test-Path "Cert:\LocalMachine\Root\$t");TrustedPublisher=(Test-Path "Cert:\LocalMachine\TrustedPublisher\$t")}}
function PublishedH15o{$r=@();foreach($f in @(Get-ChildItem (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' -File)){try{$t=Get-Content $f.FullName -Raw}catch{continue};if($t.IndexOf('Phaser360H15o',[StringComparison]::OrdinalIgnoreCase)-ge0 -and $t.IndexOf($ExactHwid,[StringComparison]::OrdinalIgnoreCase)-ge0){$r+=$f.Name}};@($r)}
function WaitIntel([string]$instance,[int]$seconds=20){$end=(Get-Date).AddSeconds($seconds);do{Start-Sleep -Milliseconds 500;try{$s=Target}catch{$s=$null};if($s -and $s.InstanceId -ceq $instance -and (IsIntel $s)){return $s}}while((Get-Date)-lt$end);throw 'INTEL_BASELINE_TIMEOUT'}
function CodeIntegrity{if(-not('Phaser360.H15orCi'-as[type])){Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;namespace Phaser360{public static class H15orCi{[StructLayout(LayoutKind.Sequential)]struct CI{public UInt32 Length;public UInt32 Options;}[DllImport("ntdll.dll")]static extern Int32 NtQuerySystemInformation(Int32 c,ref CI i,UInt32 l,IntPtr r);public static UInt32 Get(){CI i=new CI();i.Length=(UInt32)Marshal.SizeOf(typeof(CI));Int32 s=NtQuerySystemInformation(103,ref i,i.Length,IntPtr.Zero);if(s<0)throw new Exception("CI query failed");return i.Options;}}}' -Language CSharp};[uint32][Phaser360.H15orCi]::Get()}
function Package([string]$root,[switch]$Trusted){
 $r=(Resolve-Path $root).Path;$sys=Join-Path $r 'phaser360_h15or_r2_readonly_recovery_probe.sys';$cer=Join-Path $r 'phaser360_h15or_r2_readonly_recovery_probe.cer';$man=Join-Path $r 'package_manifest.json'
 foreach($p in @($sys,$cer,$man)){if(-not(Test-Path $p -PathType Leaf)){throw "PACKAGE_FILE_MISSING: $p"}}
 $c=Get-PfxCertificate $cer;if(-not$c -or $c.HasPrivateKey -or $c.Subject -cne $ProbeSubject -or $c.Issuer -cne $ProbeSubject){throw 'RECOVERY_CERT_IDENTITY_INVALID'}
 $m=Get-Content $man -Raw|ConvertFrom-Json
 if([string]$m.Purpose -cne 'H15OR_R2_READONLY_RECOVERY_PACKAGE' -or [string]$m.SysSha256 -cne (Get-FileHash $sys -Algorithm SHA256).Hash.ToLowerInvariant()){throw 'RECOVERY_MANIFEST_INVALID'}
 if([string]$m.CertificateThumbprint -cne [string]$c.Thumbprint){throw 'RECOVERY_CERT_THUMBPRINT_MISMATCH'}
 $s=Get-AuthenticodeSignature $sys;if(-not$s.SignerCertificate -or [string]$s.SignerCertificate.Thumbprint -cne [string]$c.Thumbprint){throw 'RECOVERY_SIGNER_MISMATCH'}
 if($Trusted -and $s.Status-ne'Valid'){throw 'RECOVERY_SIGNATURE_NOT_VALID_AFTER_TRUST'}
 [pscustomobject]@{Root=$r;Sys=$sys;Cer=$cer;Thumb=[string]$c.Thumbprint;Manifest=$m}
}
function Native{
 if('Phaser360.H15orNative'-as[type]){return}
 Add-Type -TypeDefinition @'
using System;using System.ComponentModel;using System.Runtime.InteropServices;using Microsoft.Win32.SafeHandles;
namespace Phaser360{public static class H15orNative{
 const uint GENERIC_READ=0x80000000,GENERIC_WRITE=0x40000000,FILE_SHARE_READ=1,FILE_SHARE_WRITE=2,OPEN_EXISTING=3;
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern SafeFileHandle CreateFile(string n,uint a,uint sh,IntPtr sa,uint c,uint f,IntPtr t);
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool DeviceIoControl(SafeFileHandle h,uint c,byte[] i,uint ib,byte[] o,uint ob,out uint r,IntPtr ov);
 public static byte[] Snapshot(uint ioctl,byte[] input,int bytes){using(var h=CreateFile(@"\\.\Phaser360H15or",GENERIC_READ|GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_WRITE,IntPtr.Zero,OPEN_EXISTING,0,IntPtr.Zero)){if(h.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error(),"CreateFile H15OR");var o=new byte[bytes];uint got;if(!DeviceIoControl(h,ioctl,input,(uint)input.Length,o,(uint)o.Length,out got,IntPtr.Zero))throw new Win32Exception(Marshal.GetLastWin32Error(),"DeviceIoControl H15OR");if(got!=bytes)throw new Exception("H15OR_R2_RESULT_SIZE="+got);return o;}}
}}
'@ -Language CSharp -ErrorAction Stop
}
function U64([byte[]]$b,[int]$o){[BitConverter]::ToUInt64($b,$o)};function U32([byte[]]$b,[int]$o){[BitConverter]::ToUInt32($b,$o)};function U16([byte[]]$b,[int]$o){[BitConverter]::ToUInt16($b,$o)}
function Put32([byte[]]$b,[int]$o,[uint32]$v){[Array]::Copy([BitConverter]::GetBytes($v),0,$b,$o,4)}
function WriteUtf8([string]$path,[AllowNull()][object]$value){$text=if($null-eq$value){''}else{[string]$value};[IO.File]::WriteAllText($path,$text,[Text.UTF8Encoding]::new($false))}
function WriteAsciiLines([string]$path,[string[]]$lines){if($null-eq$lines){$lines=@()};[IO.File]::WriteAllLines($path,$lines,[Text.Encoding]::ASCII)}
function Hashes([string]$d){$lines=@(Get-ChildItem $d -Recurse -File|Where-Object{$_.Name-ne'SHA256SUMS.txt'}|Sort-Object FullName|ForEach-Object{(Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()+' '+$_.FullName.Substring($d.Length).TrimStart('\')});WriteAsciiLines (Join-Path $d 'SHA256SUMS.txt') $lines}
function RemoveSubject([string]$subject){
 foreach($store in @('Root','TrustedPublisher')){foreach($c in @(Get-ChildItem "Cert:\LocalMachine\$store"|Where-Object{$_.Subject -ceq $subject -and $_.Issuer -ceq $subject})){try{Remove-Item $c.PSPath -Force -ErrorAction Stop}catch{}}}
}

if(-not(Admin)){throw 'ADMINISTRATOR_REQUIRED'};if(-not[Environment]::Is64BitProcess){throw 'WINDOWS_X64_REQUIRED'}
Write-Host 'RUNNER=H15OR_R2_NONINTERACTIVE_RECOVERY'
Write-Host 'RUNNER_BUILD=h15or-r2.1-sc-alias-fix-20260923'
Write-Host ('RUNNER_PATH=' + $MyInvocation.MyCommand.Path)
if([Environment]::OSVersion.Version.Build -ne 19044){throw 'EXACT_WINDOWS_BUILD_19044_REQUIRED'}
if(((CodeIntegrity)-band 2)-eq0){throw 'CODE_INTEGRITY_TESTSIGN_NOT_ALLOWED'}
$before=Target;if(@($before.HardwareIds|Where-Object{$_ -ceq $ExactHwid}).Count-ne1){throw 'EXACT_HARDWARE_ID_NOT_PRESENT'}
if(IsIntel $before){Write-Host 'STATUS=H15OR_R2_ALREADY_INTEL_BASELINE';exit 0}
if($before.Service -cne $H15oService -or $before.DriverVersion -cne '0.6.15.270' -or $before.DriverProvider -cne 'PHASER360 Experimental'){throw 'H15OR_R2_EXPECTED_ACTIVE_H15O_NOT_FOUND'}
$h15oPublished=@(PublishedH15o);if($h15oPublished.Count-ne1 -or $h15oPublished[0] -cne $before.DriverInfPath){throw 'H15OR_R2_ACTIVE_H15O_INF_MISMATCH'}

# Clean only stale H15OR R2 probe state from an interrupted previous recovery attempt.
$stale=InvokeScExe @('query',$ProbeService);if($stale.ExitCode-eq0){$null=InvokeScExe @('stop',$ProbeService);$null=InvokeScExe @('delete',$ProbeService);Start-Sleep -Milliseconds 300}
RemoveSubject $ProbeSubject

$pkg=Package $PackageRoot
$selfSha=(Get-FileHash -LiteralPath $MyInvocation.MyCommand.Path -Algorithm SHA256).Hash.ToLowerInvariant()
if([string]$pkg.Manifest.RunnerSha256 -cne $selfSha){throw 'H15OR_R2_RUNNER_SELF_HASH_MISMATCH'}
Write-Host ('RUNNER_SHA256=' + $selfSha)
if([string]::IsNullOrWhiteSpace($OutputRoot)){$OutputRoot=$PSScriptRoot};$stamp=Get-Date -Format 'yyyyMMdd_HHmmss';$suffix=[Guid]::NewGuid().ToString('N').Substring(0,8)
$dir=Join-Path $OutputRoot ('H15OR_R2_RECOVERY_'+$stamp+'_'+$suffix);New-Item -ItemType Directory -Path $dir -Force|Out-Null
WriteUtf8 (Join-Path $dir 'target_before.json') ($before|ConvertTo-Json -Depth 8)

$probeRoot=$false;$probePub=$false;$probeCreated=$false;$probeStarted=$false;$snapshot=$false;$safe=$false;$intelRestored=$false;$err=$null
try{
 $x=CertUtil @('-f','-addstore','Root',$pkg.Cer);if($x.ExitCode-ne0){throw 'H15OR_R2_CERT_ROOT_ADD_FAILED'};$probeRoot=$true
 $x=CertUtil @('-f','-addstore','TrustedPublisher',$pkg.Cer);if($x.ExitCode-ne0){throw 'H15OR_R2_CERT_PUBLISHER_ADD_FAILED'};$probePub=$true
 $null=Package $PackageRoot -Trusted
 $q=InvokeScExe @('query',$ProbeService);if($q.ExitCode-eq0){$null=InvokeScExe @('stop',$ProbeService);$null=InvokeScExe @('delete',$ProbeService);Start-Sleep -Milliseconds 300}
 $x=InvokeScExe @('create',$ProbeService,'type=','kernel','start=','demand','binPath=',('"' + $pkg.Sys + '"'));WriteUtf8 (Join-Path $dir 'sc_create.txt') $x.Output;if($x.ExitCode-ne0){throw 'H15OR_R2_SERVICE_CREATE_FAILED'};$probeCreated=$true
 $x=InvokeScExe @('start',$ProbeService);WriteUtf8 (Join-Path $dir 'sc_start.txt') $x.Output;if($x.ExitCode-ne0){throw 'H15OR_R2_SERVICE_START_FAILED'};$probeStarted=$true
 Native
 $addr=[uint32][BitConverter]::ToUInt32([BitConverter]::GetBytes([int32]$before.Address),0);$devNum=($addr-shr16)-band0xFFFF;$func=$addr-band0xFFFF
 if($devNum-gt31 -or $func-gt7 -or $before.BusNumber-lt0 -or $before.BusNumber-gt255){throw "INVALID_PCI_LOCATION bus=$($before.BusNumber) address=0x$('{0:X8}'-f $addr)"}
 $slot=[uint32]($devNum -bor ($func-shl5));WriteUtf8 (Join-Path $dir 'pci_location.json') ([ordered]@{BusNumber=$before.BusNumber;Address=('0x{0:X8}'-f $addr);Device=$devNum;Function=$func;Slot=$slot}|ConvertTo-Json)
 $req=[byte[]]::new(16);Put32 $req 0 1;Put32 $req 4 16;Put32 $req 8 ([uint32]$before.BusNumber);Put32 $req 12 $slot
 $r=[Phaser360.H15orNative]::Snapshot($Ioctl,$req,$ResultBytes);[IO.File]::WriteAllBytes((Join-Path $dir 'h15or_r2_raw.bin'),$r)
 $v=U32 $r 0;$sz=U32 $r 4;$status=U32 $r 8;$flags=U32 $r 12;$bus=U32 $r 16;$slotOut=U32 $r 20;$pciBytes=U32 $r 24;$ven=U16 $r 28;$dev=U16 $r 30;$pg=U32 $r 32;$cg=U32 $r 36;$hda=U64 $r 40;$dsp=U64 $r 48;$hl=U32 $r 56;$dl=U32 $r 60
 $obs=[ordered]@{HdaGcap=('0x{0:X4}'-f(U16 $r 64));HdaVmin=('0x{0:X2}'-f[uint32]$r[66]);HdaVmaj=('0x{0:X2}'-f[uint32]$r[67]);HdaGctl=('0x{0:X8}'-f(U32 $r 68));HdaCorbctl=('0x{0:X2}'-f[uint32]$r[72]);HdaRirbctl=('0x{0:X2}'-f[uint32]$r[73]);TotalStreams=[uint32]$r[74];StreamRunMask=('0x{0:X8}'-f(U32 $r 76));HdaIntelEm2=('0x{0:X8}'-f(U32 $r 80));HdaPpctl=('0x{0:X8}'-f(U32 $r 84));HdaPpsts=('0x{0:X8}'-f(U32 $r 88));DspAdspcs=('0x{0:X8}'-f(U32 $r 92));DspAdspic=('0x{0:X8}'-f(U32 $r 96));DspAdspis=('0x{0:X8}'-f(U32 $r 100));DspHipci=('0x{0:X8}'-f(U32 $r 104));DspHipcie=('0x{0:X8}'-f(U32 $r 108));DspHipcctl=('0x{0:X8}'-f(U32 $r 112));DspRomStatus=('0x{0:X8}'-f(U32 $r 116))}
 WriteUtf8 (Join-Path $dir 'h15or_r2_snapshot.json') ([ordered]@{Version=$v;Size=$sz;Status=('0x{0:X8}'-f$status);Flags=('0x{0:X8}'-f$flags);BusNumber=$bus;Slot=$slotOut;PciBytesRead=$pciBytes;Vendor=('0x{0:X4}'-f$ven);Device=('0x{0:X4}'-f$dev);Pgctl=('0x{0:X8}'-f$pg);Cgctl=('0x{0:X8}'-f$cg);HdaPhysical=('0x{0:X16}'-f$hda);DspPhysical=('0x{0:X16}'-f$dsp);HdaLength=('0x{0:X8}'-f$hl);DspLength=('0x{0:X8}'-f$dl);Observation=$obs}|ConvertTo-Json -Depth 8)
 $snapshot=$true;$safe=($v-eq1 -and $sz-eq120 -and ($flags-band0x3FF)-eq0x3FF -and $ven-eq0x8086 -and $dev-eq0x3198 -and $pg-eq0x10 -and $cg-eq0x807B0DFF -and (U32 $r 68)-eq0 -and (U32 $r 80)-eq0x04007000 -and (U32 $r 84)-eq0 -and (U32 $r 92)-eq0x001D003C -and (U32 $r 108)-eq0x00420000 -and (U32 $r 116)-eq0x01006701)
} catch{$err=$_.Exception} finally{
 if($probeStarted){$x=InvokeScExe @('stop',$ProbeService);WriteUtf8 (Join-Path $dir 'sc_stop.txt') $x.Output;$probeStarted=$false}
 if($probeCreated){$x=InvokeScExe @('delete',$ProbeService);WriteUtf8 (Join-Path $dir 'sc_delete.txt') $x.Output;$probeCreated=$false}
 if($probePub){$null=CertUtil @('-delstore','TrustedPublisher',$pkg.Thumb);$probePub=$false}
 if($probeRoot){$null=CertUtil @('-delstore','Root',$pkg.Thumb);$probeRoot=$false}
}
if($safe -and -not$err){
 $x=PnP @('/delete-driver',$before.DriverInfPath,'/uninstall','/force');WriteUtf8 (Join-Path $dir 'pnputil_remove_h15o.txt') $x.Output
 if($x.ExitCode-eq0){
   try{$after=WaitIntel $before.InstanceId 20;WriteUtf8 (Join-Path $dir 'target_after.json') ($after|ConvertTo-Json -Depth 8);if(@(PublishedH15o).Count-eq0){RemoveSubject $H15oSubject;$intelRestored=$true}}catch{$err=$_.Exception}
 }
 else{$err=[Exception]::new('H15O_UNINSTALL_FAILED')}
}
$status=if($intelRestored){'H15OR_R2_SAFE_BASELINE_PROVED_AND_INTEL_RESTORED'}elseif($snapshot -and -not$safe){'H15OR_R2_DIRTY_OR_NONBASELINE_STATE_DETECTED_H15O_RETAINED'}else{'H15OR_R2_RECOVERY_PROBE_FAILED'}
WriteUtf8 (Join-Path $dir 'recovery.json') ([ordered]@{Status=$status;SnapshotCompleted=$snapshot;ExactSafeForHandoff=$safe;IntelRestored=$intelRestored;H15oRetained=(-not$intelRestored);TransactionError=$(if($err){$err.Message}else{$null});MmioWrite='NO';PciWrite='NO';Dma='NO';IrqOwnership='NO';Firmware='NO';Playback='NO';BcdWrite='NO';SystemReboot='NO'}|ConvertTo-Json -Depth 5)
Hashes $dir;$zip=Join-Path $OutputRoot ('RESULT_H15OR_R2_RECOVERY_'+$stamp+'_'+$suffix+'.zip');Compress-Archive -Path (Join-Path $dir '*') -DestinationPath $zip -Force
Write-Host "STATUS=$status";Write-Host "SNAPSHOT_COMPLETED=$($snapshot.ToString().ToUpperInvariant())";Write-Host "EXACT_SAFE_FOR_HANDOFF=$($safe.ToString().ToUpperInvariant())";Write-Host "INTEL_RESTORED=$($intelRestored.ToString().ToUpperInvariant())";Write-Host "Trimite fisierul: $zip"
if($err){Write-Host "ERROR=$($err.Message)"}
if(-not$intelRestored){exit 2}
