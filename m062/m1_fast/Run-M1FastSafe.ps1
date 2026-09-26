#requires -Version 5.1
param([Parameter(Mandatory=$true)][string]$PackageRoot,[string]$OutputRoot='')
$ErrorActionPreference='Stop';Set-StrictMode -Version 2

$Build='m1-fast-safe-20260926-r8-candidate-ssp-contract'
$ExactHwid='PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06'
$M1Service='Phaser360M1'
$M1Version='0.6.15.137'
$M1Provider='PHASER360 Experimental'
$BaselineService='IntcAudioBus'
$BaselineInf='oem14.inf'
$BaselineVersion='9.22.0.4832'
$BaselineProvider='Intel(R) Corporation'
$CertSubject='CN=PHASER360 M1 Fast Safe Signing'
$TelemetryGuid=[Guid]'6c50afa1-ec12-4b89-a150-3600615b0011'
$TelemetryIoctl=[Convert]::ToUInt32('00226000',16)
$StageProviderGuid=[Guid]'ebaed0db-f9db-42ea-a162-5f4111384051'
$FirmwareSha='40029b5a05665f19a492ef00b8c0a24c42e90d7c00fc57146e07947fd1407d5c'
$NHLTSha='4764aba0316e9a039a127285cc4ff9e97e22c75bfddb6d865d9f77b59dd2a6b9'

function Admin{$id=[Security.Principal.WindowsIdentity]::GetCurrent();([Security.Principal.WindowsPrincipal]::new($id)).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)}
function Target{
  $d=@(Get-PnpDevice -PresentOnly -ErrorAction Stop|Where-Object{$_.InstanceId -like 'PCI\VEN_8086&DEV_3198*'})
  if($d.Count-ne1){throw "EXPECTED_ONE_PRESENT_DEV3198: count=$($d.Count)"}
  $id=[string]$d[0].InstanceId;$p=@(Get-PnpDeviceProperty -InstanceId $id -ErrorAction Stop);$m=@{}
  foreach($x in $p){$m[[string]$x.KeyName]=$x.Data}
  [pscustomobject]@{
    InstanceId=$id;Status=[string]$d[0].Status;ProblemCode=[int]$m['DEVPKEY_Device_ProblemCode']
    ProblemStatusRaw=$m['DEVPKEY_Device_ProblemStatus']
    ProblemStatus=$(if($null-ne$m['DEVPKEY_Device_ProblemStatus']){'0x{0:X8}' -f ([uint32](([int64]$m['DEVPKEY_Device_ProblemStatus']) -band 0xffffffffL))}else{$null})
    Service=[string]$m['DEVPKEY_Device_Service'];DriverInfPath=[string]$m['DEVPKEY_Device_DriverInfPath']
    DriverVersion=[string]$m['DEVPKEY_Device_DriverVersion'];DriverProvider=[string]$m['DEVPKEY_Device_DriverProvider']
    HardwareIds=@($m['DEVPKEY_Device_HardwareIds'])
  }
}
function IsIntel($s){
  $s -and $s.Status -ceq 'OK' -and $s.ProblemCode-eq0 -and
  $s.Service -ceq $BaselineService -and $s.DriverInfPath -ceq $BaselineInf -and
  $s.DriverVersion -ceq $BaselineVersion -and $s.DriverProvider -ceq $BaselineProvider -and
  @($s.HardwareIds|Where-Object{$_ -ceq $ExactHwid}).Count-eq1
}
function IsM1Identity($s,[string]$inf){
  $s -and $s.Service -ceq $M1Service -and $s.DriverInfPath -ceq $inf -and
  $s.DriverVersion -ceq $M1Version -and $s.DriverProvider -ceq $M1Provider -and
  @($s.HardwareIds|Where-Object{$_ -ceq $ExactHwid}).Count-eq1
}
function IsM1($s,[string]$inf){
  (IsM1Identity $s $inf) -and $s.Status -ceq 'OK' -and $s.ProblemCode-eq0
}
function PnP([string[]]$a){$e=Join-Path $env:SystemRoot 'System32\pnputil.exe';$old=$ErrorActionPreference;$ErrorActionPreference='Continue';try{$o=(& $e @a 2>&1|Out-String -Width 8192);$c=$LASTEXITCODE}finally{$ErrorActionPreference=$old};[pscustomobject]@{ExitCode=$c;Output=$o}}
function CertUtil([string[]]$a){$e=Join-Path $env:SystemRoot 'System32\certutil.exe';$old=$ErrorActionPreference;$ErrorActionPreference='Continue';try{$o=(& $e @a 2>&1|Out-String -Width 8192);$c=$LASTEXITCODE}finally{$ErrorActionPreference=$old};[pscustomobject]@{ExitCode=$c;Output=$o}}
function Logman([string[]]$a){$e=Join-Path $env:SystemRoot 'System32\logman.exe';$old=$ErrorActionPreference;$ErrorActionPreference='Continue';try{$o=(& $e @a 2>&1|Out-String -Width 8192);$c=$LASTEXITCODE}finally{$ErrorActionPreference=$old};[pscustomobject]@{ExitCode=$c;Output=$o}}
function StartStageTrace([string]$dir,[string]$suffix){
  $name='PHASER360_M1_R8_CANDIDATE_'+$suffix
  $etl=Join-Path $dir 'M1_R8_CANDIDATE_STAGE_TRACE.etl'
  $provider='{'+$StageProviderGuid.ToString()+'}'
  $x=Logman @('start',$name,'-ets','-p',$provider,'0x1','0xFF','-o',$etl,'-bs','64','-nb','16','64')
  WriteUtf8 (Join-Path $dir 'stage_trace_start.txt') $x.Output
  if($x.ExitCode-ne0){throw "M1_R8_CANDIDATE_STAGE_TRACE_START_FAILED: exit=$($x.ExitCode)"}
  [pscustomobject]@{Name=$name;Etl=$etl;Started=$true;Stopped=$false;StopExitCode=$null;Decoded=$false;EventCount=0;DriverEntries=0;BootEntries=0;LossChecked=$false;EventsLost=$null;BuffersLost=$null}
}
function DecodeStageTrace([string]$xml,[string]$dir){
  [xml]$doc=Get-Content -LiteralPath $xml -Raw
  $ns=[Xml.XmlNamespaceManager]::new($doc.NameTable)
  $ns.AddNamespace('e','http://schemas.microsoft.com/win/2004/08/events/event')
  $rows=@(foreach($event in $doc.SelectNodes('//e:Event',$ns)){
    $data=$event.SelectSingleNode('e:Data',$ns)
    if(-not$data){continue}
    $message=$data.InnerText.Trim()
    if($message -notmatch '^PHASER360_R8_CANDIDATE (?<stage>\S+) status=(?<status>0x[0-9A-Fa-f]{8})(?: value=(?<value>0x[0-9A-Fa-f]{8}))?$'){continue}
    $stamp=$event.SelectSingleNode('e:System/e:TimeCreated',$ns)
    [pscustomobject]@{TimestampRaw=$(if($stamp){$stamp.GetAttribute('SystemTime')}else{$null});Stage=$Matches['stage'];Status=$Matches['status'];Value=$Matches['value'];Message=$message}
  })
  WriteUtf8 (Join-Path $dir 'stage_trace_decoded.json') (ConvertTo-Json -InputObject $rows -Depth 5)
  $header=$doc.SelectSingleNode('//e:Event[e:System/e:Provider/@Guid="{9e814aad-3204-11d2-9a82-006008a86939}" and e:System/e:Opcode="0"]/e:EventData',$ns)
  $eventsLost=$null;$buffersLost=$null
  if($header){
    $n=$header.SelectSingleNode('e:Data[@Name="EventsLost"]',$ns);if($n){$eventsLost=[uint64]$n.InnerText.Trim()}
    $n=$header.SelectSingleNode('e:Data[@Name="BuffersLost"]',$ns);if($n){$buffersLost=[uint64]$n.InnerText.Trim()}
  }
  [pscustomobject]@{EventCount=$rows.Count;DriverEntries=@($rows|Where-Object{$_.Stage -ceq 'A00_DRIVER_ENTRY'}).Count;BootEntries=@($rows|Where-Object{$_.Stage -ceq 'D30_FIRMWARE_ENTER'}).Count;LossChecked=($null-ne$eventsLost -and $null-ne$buffersLost);EventsLost=$eventsLost;BuffersLost=$buffersLost}
}
function StopStageTrace($trace,[string]$dir){
  if(-not$trace -or -not$trace.Started -or $trace.Stopped){return}
  $x=Logman @('stop',[string]$trace.Name,'-ets')
  WriteUtf8 (Join-Path $dir 'stage_trace_stop.txt') $x.Output
  $trace.StopExitCode=$x.ExitCode
  if($x.ExitCode-ne0){WriteUtf8 (Join-Path $dir 'stage_trace_decode_error.txt') "ETW_STOP_FAILED: exit=$($x.ExitCode)";return}
  $trace.Stopped=$true
  if(-not(Test-Path $trace.Etl -PathType Leaf)){WriteUtf8 (Join-Path $dir 'stage_trace_decode_error.txt') 'ETL_MISSING';return}
  try{
    $events=@(Get-WinEvent -Path $trace.Etl -Oldest -ErrorAction Stop|ForEach-Object{
      [ordered]@{
        TimeCreated=$(if($_.TimeCreated){$_.TimeCreated.ToString('o')}else{$null})
        Id=$_.Id;ProviderName=$_.ProviderName;LevelDisplayName=$_.LevelDisplayName;Message=$_.Message
        Properties=@($_.Properties|ForEach-Object{[string]$_.Value})
      }
    })
    WriteUtf8 (Join-Path $dir 'stage_trace_events.json') ($events|ConvertTo-Json -Depth 8)
  }catch{WriteUtf8 (Join-Path $dir 'stage_trace_getwinevent_error.txt') $_.Exception.ToString()}
  try{
    $tracerpt=Join-Path $env:SystemRoot 'System32\tracerpt.exe'
    $xml=Join-Path $dir 'M1_R8_CANDIDATE_STAGE_TRACE.xml'
    $old=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try{$o=(& $tracerpt $trace.Etl '-o' $xml '-of' 'XML' '-y' 2>&1|Out-String -Width 8192);$ec=$LASTEXITCODE}finally{$ErrorActionPreference=$old}
    WriteUtf8 (Join-Path $dir 'stage_trace_tracerpt.txt') ("EXIT=$ec"+[Environment]::NewLine+$o)
    if($ec-ne0){throw "TRACERPT_FAILED: exit=$ec"}
    $decoded=DecodeStageTrace $xml $dir
    $trace.EventCount=$decoded.EventCount;$trace.DriverEntries=$decoded.DriverEntries;$trace.BootEntries=$decoded.BootEntries
    $trace.LossChecked=$decoded.LossChecked;$trace.EventsLost=$decoded.EventsLost;$trace.BuffersLost=$decoded.BuffersLost
    $trace.Decoded=($decoded.EventCount-gt0)
  }catch{WriteUtf8 (Join-Path $dir 'stage_trace_tracerpt_error.txt') $_.Exception.ToString()}
}
function PublishedM1{
  $r=@();foreach($f in @(Get-ChildItem (Join-Path $env:SystemRoot 'INF') -Filter 'oem*.inf' -File)){
    try{$t=Get-Content $f.FullName -Raw}catch{continue}
    if($t.IndexOf('Phaser360M1',[StringComparison]::OrdinalIgnoreCase)-ge0 -and
       $t.IndexOf($ExactHwid,[StringComparison]::OrdinalIgnoreCase)-ge0){$r+=$f.Name}
  };@($r)
}
function WaitIntel([string]$instance,[int]$seconds=30){
  $end=(Get-Date).AddSeconds($seconds);do{Start-Sleep -Milliseconds 500;try{$s=Target}catch{$s=$null};if($s -and $s.InstanceId -ceq $instance -and (IsIntel $s)){return $s}}while((Get-Date)-lt$end);throw 'INTEL_BASELINE_TIMEOUT'
}
function WaitM1([string]$instance,[string]$inf,[int]$seconds=20){
  $end=(Get-Date).AddSeconds($seconds);do{
    Start-Sleep -Milliseconds 250
    try{$s=Target}catch{$s=$null}
    if($s -and $s.InstanceId -ceq $instance -and (IsM1Identity $s $inf)){
      if(IsM1 $s $inf){return $s}
      if($s.ProblemCode-eq10){
        throw ("M1_TARGET_FAILED_START_CODE10: problem_status="+$(if($s.ProblemStatus){$s.ProblemStatus}else{'UNKNOWN'}))
      }
    }
  }while((Get-Date)-lt$end)
  throw 'M1_TARGET_NOT_HEALTHY'
}
function Trust([string]$thumb){$t=$thumb.Replace(' ','').ToUpperInvariant();[pscustomobject]@{Root=(Test-Path "Cert:\LocalMachine\Root\$t");TrustedPublisher=(Test-Path "Cert:\LocalMachine\TrustedPublisher\$t")}}
function CodeIntegrity{
  if(-not('Phaser360.M1FastCi'-as[type])){Add-Type -TypeDefinition 'using System;using System.Runtime.InteropServices;namespace Phaser360{public static class M1FastCi{[StructLayout(LayoutKind.Sequential)]struct CI{public UInt32 Length;public UInt32 Options;}[DllImport("ntdll.dll")]static extern Int32 NtQuerySystemInformation(Int32 c,ref CI i,UInt32 l,IntPtr r);public static UInt32 Get(){CI i=new CI();i.Length=(UInt32)Marshal.SizeOf(typeof(CI));Int32 s=NtQuerySystemInformation(103,ref i,i.Length,IntPtr.Zero);if(s<0)throw new Exception("CI query failed");return i.Options;}}}' -Language CSharp};[uint32][Phaser360.M1FastCi]::Get()
}
function WinRE{
  $e=Join-Path $env:SystemRoot 'System32\reagentc.exe';$old=$ErrorActionPreference;$ErrorActionPreference='Continue';try{$o=(& $e /info 2>&1|Out-String -Width 8192);$c=$LASTEXITCODE}finally{$ErrorActionPreference=$old}
  $s=if($o -match '(?im)Windows\s+RE.*(?:Enabled|Activat)'){'ENABLED'}elseif($o -match '(?im)Windows\s+RE.*(?:Disabled|Dezactivat)'){'DISABLED'}else{'UNKNOWN'}
  [pscustomobject]@{ExitCode=$c;Status=$s;Output=$o}
}
function WriteUtf8([string]$p,[AllowNull()][object]$v){$t=if($null-eq$v){''}else{[string]$v};[IO.File]::WriteAllText($p,$t,[Text.UTF8Encoding]::new($false))}
function WriteAsciiLines([string]$p,[string[]]$v){if($null-eq$v){$v=@()};[IO.File]::WriteAllLines($p,$v,[Text.Encoding]::ASCII)}
function Hashes([string]$d){$lines=@(Get-ChildItem $d -Recurse -File|Where-Object{$_.Name-ne'SHA256SUMS.txt'}|Sort-Object FullName|ForEach-Object{(Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()+' '+$_.FullName.Substring($d.Length).TrimStart('\')});WriteAsciiLines (Join-Path $d 'SHA256SUMS.txt') $lines}
function Package([string]$root,[switch]$Trusted){
  $r=(Resolve-Path $root).Path
  $inf=Join-Path $r 'phaser360_m1_boot.inf';$sys=Join-Path $r 'phaser360_m1_boot.sys';$cat=Join-Path $r 'phaser360_m1_boot.cat'
  $cer=Join-Path $r 'phaser360_m1_fast_safe.cer';$man=Join-Path $r 'package_manifest.json'
  $ref=Join-Path $r 'SOF_REFERENCE_CHECK.json';$prov=Join-Path $r 'EMBEDDED_FIRMWARE_REPORT.json'
  foreach($p in @($inf,$sys,$cat,$cer,$man,$ref,$prov)){if(-not(Test-Path $p -PathType Leaf)){throw "PACKAGE_FILE_MISSING: $p"}}
  $c=Get-PfxCertificate $cer
  if(-not$c -or $c.HasPrivateKey -or $c.Subject -cne $CertSubject -or $c.Issuer -cne $CertSubject){throw 'M1_CERT_IDENTITY_INVALID'}
  if($c.NotBefore.ToUniversalTime()-gt[DateTime]::UtcNow -or $c.NotAfter.ToUniversalTime()-le[DateTime]::UtcNow){throw 'M1_CERT_NOT_CURRENTLY_VALID'}
  $m=Get-Content $man -Raw|ConvertFrom-Json
  if([string]$m.Purpose -cne 'M1_FAST_SAFE_R8_CANDIDATE_SSP_CONTRACT_DSP_BOOT' -or [string]$m.RunnerBuild -cne $Build -or
     [string]$m.ExactHardwareId -cne $ExactHwid -or [int]$m.WindowsBuildExact -ne19044 -or
     [string]$m.BaselineInf -cne $BaselineInf -or [string]$m.BaselineVersion -cne $BaselineVersion -or
     [string]$m.FirmwareSha256 -cne $FirmwareSha -or [string]$m.NhltSha256 -cne $NHLTSha -or
     [string]$m.StageTraceProvider -cne $StageProviderGuid.ToString() -or
     [string]$m.CertificateThumbprint -cne [string]$c.Thumbprint){throw 'M1_MANIFEST_CONTRACT_INVALID'}
  foreach($x in @(@($inf,[string]$m.InfSha256),@($sys,[string]$m.SysSha256),@($cat,[string]$m.CatSha256),@($cer,[string]$m.CerSha256))){
    if((Get-FileHash $x[0] -Algorithm SHA256).Hash.ToLowerInvariant() -cne $x[1].ToLowerInvariant()){throw 'M1_MANIFEST_HASH_MISMATCH'}
  }
  $rr=Get-Content $ref -Raw|ConvertFrom-Json;$pr=Get-Content $prov -Raw|ConvertFrom-Json
  if([string]$rr.reference_check -cne 'PASS' -or [string]$rr.fixture_sha256 -cne $FirmwareSha -or
     [string]$pr.input_sha256 -cne $FirmwareSha -or [int]$pr.input_bytes -ne287488){throw 'M1_FIRMWARE_EVIDENCE_INVALID'}
  $ss=Get-AuthenticodeSignature $sys;$cs=Get-AuthenticodeSignature $cat
  if(-not$ss.SignerCertificate -or -not$cs.SignerCertificate -or
     [string]$ss.SignerCertificate.Thumbprint -cne [string]$c.Thumbprint -or
     [string]$cs.SignerCertificate.Thumbprint -cne [string]$c.Thumbprint){throw 'M1_SIGNER_MISMATCH'}
  if($Trusted -and ($ss.Status-ne'Valid' -or $cs.Status-ne'Valid')){throw 'M1_SIGNATURE_NOT_VALID_AFTER_TRUST'}
  [pscustomobject]@{Root=$r;Inf=$inf;Sys=$sys;Cat=$cat;Cer=$cer;Thumb=[string]$c.Thumbprint;Manifest=$m}
}
function Native{
  if('Phaser360.M1FastNative'-as[type]){return}
  Add-Type -TypeDefinition @'
using System;using System.ComponentModel;using System.Runtime.InteropServices;using Microsoft.Win32.SafeHandles;
namespace Phaser360{public static class M1FastNative{
 const uint INSTALLFLAG_FORCE=1,DIGCF_PRESENT=2,DIGCF_DEVICEINTERFACE=0x10,GENERIC_READ=0x80000000,FILE_SHARE_READ=1,FILE_SHARE_WRITE=2,OPEN_EXISTING=3;
 const uint CR_SUCCESS=0,CM_GET_DEVICE_INTERFACE_LIST_PRESENT=0;
 const int ERROR_NO_MORE_ITEMS=259;static readonly IntPtr INVALID_HANDLE_VALUE=new IntPtr(-1);
 public static string PathSource{get;private set;}
 [DllImport("newdev.dll",CharSet=CharSet.Unicode,SetLastError=true,EntryPoint="UpdateDriverForPlugAndPlayDevicesW")]
 static extern bool UpdateDriverForPlugAndPlayDevices(IntPtr w,string h,string i,uint f,out bool r);
 [StructLayout(LayoutKind.Sequential)]struct IFACE{public int cbSize;public Guid InterfaceClassGuid;public int Flags;public UIntPtr Reserved;}
 [DllImport("setupapi.dll",SetLastError=true)]static extern IntPtr SetupDiGetClassDevs(ref Guid g,IntPtr e,IntPtr h,uint f);
 [DllImport("setupapi.dll",SetLastError=true)]static extern bool SetupDiEnumDeviceInterfaces(IntPtr s,IntPtr d,ref Guid g,uint i,ref IFACE x);
 [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern bool SetupDiGetDeviceInterfaceDetail(IntPtr s,ref IFACE x,IntPtr p,uint z,out uint n,IntPtr d);
 [DllImport("setupapi.dll",SetLastError=true)]static extern bool SetupDiDestroyDeviceInfoList(IntPtr s);
 [DllImport("cfgmgr32.dll",CharSet=CharSet.Unicode,EntryPoint="CM_Get_Device_Interface_List_SizeW")]
 static extern uint CM_Get_Device_Interface_List_Size(out uint len,ref Guid g,string deviceId,uint flags);
 [DllImport("cfgmgr32.dll",CharSet=CharSet.Unicode,EntryPoint="CM_Get_Device_Interface_ListW")]
 static extern uint CM_Get_Device_Interface_List(ref Guid g,string deviceId,IntPtr buffer,uint len,uint flags);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]static extern SafeFileHandle CreateFile(string n,uint a,uint sh,IntPtr sa,uint c,uint f,IntPtr t);
 [DllImport("kernel32.dll",SetLastError=true)]static extern bool DeviceIoControl(SafeFileHandle h,uint c,IntPtr i,uint ib,byte[] o,uint ob,out uint r,IntPtr ov);
 static void Q(bool ok,string op){if(!ok){int e=Marshal.GetLastWin32Error();throw new Win32Exception(e,op+"; WIN32_ERROR="+e);}}
 public static bool ForceUpdate(string hwid,string inf){bool reboot;Q(UpdateDriverForPlugAndPlayDevices(IntPtr.Zero,hwid,inf,INSTALLFLAG_FORCE,out reboot),"UpdateDriverForPlugAndPlayDevicesW");return reboot;}
 static string SetupPath(Guid g){
  IntPtr s=SetupDiGetClassDevs(ref g,IntPtr.Zero,IntPtr.Zero,DIGCF_PRESENT|DIGCF_DEVICEINTERFACE);if(s==INVALID_HANDLE_VALUE)throw new Win32Exception(Marshal.GetLastWin32Error(),"SetupDiGetClassDevs");
  try{var d=new IFACE();d.cbSize=Marshal.SizeOf(typeof(IFACE));Q(SetupDiEnumDeviceInterfaces(s,IntPtr.Zero,ref g,0,ref d),"EnumInterface[0]");
   var d2=new IFACE();d2.cbSize=Marshal.SizeOf(typeof(IFACE));bool second=SetupDiEnumDeviceInterfaces(s,IntPtr.Zero,ref g,1,ref d2);
   if(second||Marshal.GetLastWin32Error()!=ERROR_NO_MORE_ITEMS)throw new InvalidOperationException("EXPECTED_EXACTLY_ONE_M1_TELEMETRY_INTERFACE_SETUPAPI");
   uint n=0;SetupDiGetDeviceInterfaceDetail(s,ref d,IntPtr.Zero,0,out n,IntPtr.Zero);IntPtr p=Marshal.AllocHGlobal((int)n);
   try{for(int k=0;k<n;k++)Marshal.WriteByte(p,k,0);Marshal.WriteInt32(p,0,8);Q(SetupDiGetDeviceInterfaceDetail(s,ref d,p,n,out n,IntPtr.Zero),"InterfaceDetail");PathSource="SETUPAPI";return Marshal.PtrToStringUni(IntPtr.Add(p,4));}
   finally{Marshal.FreeHGlobal(p);}
  }finally{SetupDiDestroyDeviceInfoList(s);}
 }
 static string CmPath(Guid g){
  uint chars=0;uint cr=CM_Get_Device_Interface_List_Size(out chars,ref g,null,CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
  if(cr!=CR_SUCCESS||chars<2)throw new InvalidOperationException("CM_LIST_SIZE_CR="+cr+"; CHARS="+chars);
  IntPtr p=Marshal.AllocHGlobal(checked((int)chars*2));
  try{
   for(int k=0;k<checked((int)chars*2);++k)Marshal.WriteByte(p,k,0);
   cr=CM_Get_Device_Interface_List(ref g,null,p,chars,CM_GET_DEVICE_INTERFACE_LIST_PRESENT);
   if(cr!=CR_SUCCESS)throw new InvalidOperationException("CM_LIST_CR="+cr);
   string multi=Marshal.PtrToStringUni(p,checked((int)chars));
   string[] entries=multi.Split(new[]{'\0'},StringSplitOptions.RemoveEmptyEntries);
   if(entries.Length!=1)throw new InvalidOperationException("EXPECTED_EXACTLY_ONE_M1_TELEMETRY_INTERFACE_CFGMGR32: count="+entries.Length);
   PathSource="CFGMGR32";return entries[0];
  }finally{Marshal.FreeHGlobal(p);}
 }
 static string Path(Guid g){
  Exception setup=null;
  try{return SetupPath(g);}catch(Exception e){setup=e;}
  try{return CmPath(g);}catch(Exception cm){throw new InvalidOperationException("TELEMETRY_ENUM_FAILED; setup="+setup.Message+"; cfgmgr="+cm.Message,cm);}
 }
 public static byte[] Query(Guid g,uint ioctl,int bytes){string p=Path(g);using(var h=CreateFile(p,GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,IntPtr.Zero,OPEN_EXISTING,0,IntPtr.Zero)){
  if(h.IsInvalid)throw new Win32Exception(Marshal.GetLastWin32Error(),"CreateFile M1 telemetry via "+PathSource);var o=new byte[bytes];uint got;
  Q(DeviceIoControl(h,ioctl,IntPtr.Zero,0,o,(uint)o.Length,out got,IntPtr.Zero),"DeviceIoControl M1 telemetry via "+PathSource);if(got!=bytes)throw new InvalidOperationException("M1_TELEMETRY_SIZE="+got);return o;}}
}}
'@ -Language CSharp -ErrorAction Stop
}
function WaitTelemetry([string]$instance,[string]$inf,[int]$seconds=15){
  $end=(Get-Date).AddSeconds($seconds);$last=$null;$attempts=0;$lastState=$null
  do{
    ++$attempts
    try{
      $bytes=[Phaser360.M1FastNative]::Query($TelemetryGuid,$TelemetryIoctl,32)
      return [pscustomobject]@{Bytes=$bytes;Attempts=$attempts;Source=[string][Phaser360.M1FastNative]::PathSource}
    }catch{
      $last=$_.Exception
      try{$lastState=Target}catch{$lastState=$null}
      if($lastState -and ($lastState.InstanceId -cne $instance -or -not(IsM1 $lastState $inf))){
        throw ("M1_TARGET_LOST_DURING_TELEMETRY_WAIT: status=$($lastState.Status); problem=$($lastState.ProblemCode); service=$($lastState.Service); inf=$($lastState.DriverInfPath); last=$($last.Message)")
      }
      Start-Sleep -Milliseconds 250
    }
  }while((Get-Date)-lt$end)
  $stateText=if($lastState){"status=$($lastState.Status); problem=$($lastState.ProblemCode); service=$($lastState.Service); inf=$($lastState.DriverInfPath)"}else{'target=UNAVAILABLE'}
  throw ("M1_TELEMETRY_INTERFACE_TIMEOUT: attempts=$attempts; $stateText; last="+$(if($last){$last.Message}else{'NONE'}))
}
function U32([byte[]]$b,[int]$o){[BitConverter]::ToUInt32($b,$o)}
function I32([byte[]]$b,[int]$o){[BitConverter]::ToInt32($b,$o)}
function ParseTelemetry([byte[]]$b){
  [ordered]@{Version=U32 $b 0;Size=U32 $b 4;Flags=('0x{0:X8}'-f(U32 $b 8));FlagsValue=U32 $b 8;SessionGeneration=U32 $b 12;CompletedD0=U32 $b 16;FailedD0=U32 $b 20;LastD0Status=('0x{0:X8}'-f([uint32](I32 $b 24)));LastD0StatusValue=I32 $b 24}
}

if(-not(Admin)){throw 'ADMINISTRATOR_REQUIRED'}
if(-not[Environment]::Is64BitProcess){throw 'WINDOWS_X64_REQUIRED'}
Write-Host 'PHASER360 M1 R8 CANDIDATE SSP-CONTRACT - ONE-SHOT DSP BOOT / AUTOMATIC INTEL ROLLBACK'
Write-Host "RUNNER_BUILD=$Build"
Write-Host 'AUDIO_PLAYBACK=NO; CODEC_PROGRAMMING=NO; SPEAKER_ENABLE=NO; BCD_WRITE=NO; REBOOT=NO'
Write-Host ("STAGE_TRACE_PROVIDER={"+$StageProviderGuid.ToString()+"}")
if([Environment]::OSVersion.Version.Build-ne19044){throw 'EXACT_WINDOWS_BUILD_19044_REQUIRED'}
$before=Target;if(-not(IsIntel $before)){throw 'EXACT_INTEL_BASELINE_REQUIRED'}
if(@(PublishedM1).Count-ne0){throw 'STALE_M1_PACKAGE_PRESENT'}
$ci=CodeIntegrity;if(($ci-band2)-eq0){throw 'CODE_INTEGRITY_TESTSIGN_NOT_ALLOWED'}
$re=WinRE;if($re.ExitCode-ne0 -or $re.Status-cne'ENABLED'){throw 'WINRE_NOT_READY'}
$pkg=Package $PackageRoot
$selfSha=(Get-FileHash -LiteralPath $MyInvocation.MyCommand.Path -Algorithm SHA256).Hash.ToLowerInvariant()
if([string]$pkg.Manifest.RunnerSha256 -cne $selfSha){throw 'M1_RUNNER_SELF_HASH_MISMATCH'}
$tb=Trust $pkg.Thumb;if($tb.Root -or $tb.TrustedPublisher){throw 'M1_CERT_ALREADY_TRUSTED'}

if([string]::IsNullOrWhiteSpace($OutputRoot)){$OutputRoot=$PSScriptRoot}
$stamp=Get-Date -Format 'yyyyMMdd_HHmmss';$suffix=[Guid]::NewGuid().ToString('N').Substring(0,8)
$dir=Join-Path $OutputRoot ('M1_FAST_SAFE_'+$stamp+'_'+$suffix);$backup=Join-Path $dir 'intel_baseline_export'
New-Item -ItemType Directory -Path $dir,$backup -Force|Out-Null
WriteUtf8 (Join-Path $dir 'target_before.json') ($before|ConvertTo-Json -Depth 8)
WriteUtf8 (Join-Path $dir 'code_integrity.txt') ("0x{0:X8}"-f$ci)
WriteUtf8 (Join-Path $dir 'reagentc_info.txt') $re.Output
$ex=PnP @('/export-driver',$before.DriverInfPath,$backup);WriteUtf8 (Join-Path $dir 'pnputil_export_intel.txt') $ex.Output
if($ex.ExitCode-ne0 -or @(Get-ChildItem $backup -Recurse -File).Count-eq0){throw 'BASELINE_EXPORT_FAILED'}
$baselineExportInfs=@(Get-ChildItem $backup -Recurse -Filter '*.inf' -File)
if($baselineExportInfs.Count-ne1){throw "BASELINE_EXPORT_INF_COUNT_INVALID: count=$($baselineExportInfs.Count)"}
$baselineExportInf=$baselineExportInfs[0].FullName
WriteUtf8 (Join-Path $dir 'baseline_export_inf.txt') $baselineExportInf

$rootAdded=$false;$pubAdded=$false;$published=$false;$publishedInf=$null;$bindAttempted=$false;$bindRebootSignalled=$false;$m1Bound=$false;$bootProved=$false;$rollbackComplete=$false;$fallbackIntel=$false;$fallbackAttempted=$false;$fallbackRebootSignalled=$false;$err=$null;$stageTrace=$null
try{
  $x=CertUtil @('-f','-addstore','Root',$pkg.Cer);WriteUtf8 (Join-Path $dir 'cert_add_root.txt') $x.Output;if($x.ExitCode-ne0){throw 'CERT_ROOT_ADD_FAILED'};$rootAdded=$true
  $x=CertUtil @('-f','-addstore','TrustedPublisher',$pkg.Cer);WriteUtf8 (Join-Path $dir 'cert_add_publisher.txt') $x.Output;if($x.ExitCode-ne0){throw 'CERT_PUBLISHER_ADD_FAILED'};$pubAdded=$true
  $t=Trust $pkg.Thumb;if(-not$t.Root -or -not$t.TrustedPublisher){throw 'CERT_TRUST_NOT_PRESENT'}
  $null=Package $PackageRoot -Trusted

  $x=PnP @('/add-driver',$pkg.Inf);WriteUtf8 (Join-Path $dir 'pnputil_stage_m1.txt') $x.Output;if($x.ExitCode-ne0){throw 'M1_DRIVERSTORE_STAGE_FAILED'}
  $p=@(PublishedM1);if($p.Count-ne1){throw "EXPECTED_ONE_M1_PUBLISHED_INF: count=$($p.Count)"};$publishedInf=$p[0];$published=$true
  $pointer=@("PHASER360_M1_FAST_SAFE_RECOVERY=1","CUSTOM_INF=$publishedInf","BASELINE_INF=$BaselineInf","HWID=$ExactHwid","WINRE_REMOVE_COMMAND=dism /Image:<WINDOWS_VOLUME>:\ /Remove-Driver /Driver:$publishedInf","AUTOMATIC_REBOOT=NO") -join [Environment]::NewLine
  WriteUtf8 (Join-Path $dir 'M1_FAST_RECOVERY_POINTER.txt') $pointer
  WriteUtf8 (Join-Path $OutputRoot 'M1_FAST_RECOVERY_POINTER.txt') $pointer

  $stageTrace=StartStageTrace $dir $suffix
  Native;$bindAttempted=$true
  $bindRebootSignalled=[Phaser360.M1FastNative]::ForceUpdate($ExactHwid,$pkg.Inf)
  WriteUtf8 (Join-Path $dir 'bind_update_result.json') ([ordered]@{
    Api='UpdateDriverForPlugAndPlayDevicesW';Succeeded=$true;RebootRequired=[bool]$bindRebootSignalled;AutomaticReboot=$false
  }|ConvertTo-Json)
  try{
    $with=WaitM1 $before.InstanceId $publishedInf 20
    WriteUtf8 (Join-Path $dir 'target_with_m1.json') ($with|ConvertTo-Json -Depth 8)
    $m1Bound=$true
  }catch{
    try{
      $failed=Target
      if($failed.InstanceId -ceq $before.InstanceId -and (IsM1Identity $failed $publishedInf)){$m1Bound=$true}
      WriteUtf8 (Join-Path $dir 'target_failed_start.json') ($failed|ConvertTo-Json -Depth 8)
      $props=@(Get-PnpDeviceProperty -InstanceId $before.InstanceId -ErrorAction SilentlyContinue|Select-Object KeyName,Type,Data)
      WriteUtf8 (Join-Path $dir 'target_failed_all_properties.json') ($props|ConvertTo-Json -Depth 10)
    }catch{WriteUtf8 (Join-Path $dir 'target_failed_capture_error.txt') $_.Exception.ToString()}
    throw
  }

  $wq=WaitTelemetry $before.InstanceId $publishedInf 15
  WriteUtf8 (Join-Path $dir 'telemetry_interface_wait.json') ([ordered]@{Attempts=$wq.Attempts;MaxSeconds=15;Source=$wq.Source}|ConvertTo-Json)
  $t1=ParseTelemetry $wq.Bytes;WriteUtf8 (Join-Path $dir 'telemetry_1.json') ($t1|ConvertTo-Json -Depth 5)
  Start-Sleep -Milliseconds 500
  $with2=Target;if(-not(IsM1 $with2 $publishedInf)){throw 'M1_NOT_STABLE_AFTER_BOOT'}
  $q2=[Phaser360.M1FastNative]::Query($TelemetryGuid,$TelemetryIoctl,32);$t2=ParseTelemetry $q2;WriteUtf8 (Join-Path $dir 'telemetry_2.json') ($t2|ConvertTo-Json -Depth 5)
  foreach($tq in @($t1,$t2)){
    if($tq.Version-ne1 -or $tq.Size-ne32 -or (($tq.FlagsValue-band15)-ne7) -or $tq.SessionGeneration-lt1 -or
       $tq.CompletedD0-ne0 -or $tq.FailedD0-ne0 -or $tq.LastD0StatusValue-ne0){throw 'M1_BOOT_TELEMETRY_NOT_HEALTHY'}
  }
  if($t1.SessionGeneration-ne$t2.SessionGeneration){throw 'M1_SESSION_CHANGED_DURING_STABILITY_WINDOW'}
  StopStageTrace $stageTrace $dir
  if(-not$stageTrace.Stopped -or -not$stageTrace.Decoded){throw 'M1_STAGE_TRACE_NOT_VALIDATED'}
  if(-not$stageTrace.LossChecked -or $stageTrace.EventsLost-ne0 -or $stageTrace.BuffersLost-ne0){throw 'M1_STAGE_TRACE_LOSS_NOT_EXCLUDED'}
  if($stageTrace.DriverEntries-ne1 -or $stageTrace.BootEntries-ne1){throw "M1_ONE_SHOT_VIOLATED: driver_entries=$($stageTrace.DriverEntries); boot_entries=$($stageTrace.BootEntries)"}
  $bootProved=$true

  $x=PnP @('/delete-driver',$publishedInf,'/uninstall','/force');WriteUtf8 (Join-Path $dir 'pnputil_remove_m1.txt') $x.Output
  if($x.ExitCode-ne0){throw 'M1_UNINSTALL_FAILED'}
  Start-Sleep -Milliseconds 500
  if(@(PublishedM1).Count-ne0){throw 'M1_PACKAGE_REMAINS_AFTER_UNINSTALL'}
  try{$after=WaitIntel $before.InstanceId 20}catch{
    if(-not(Test-Path $baselineExportInf -PathType Leaf)){throw}
    $fallbackAttempted=$true
    $fallbackRebootSignalled=[Phaser360.M1FastNative]::ForceUpdate($ExactHwid,$baselineExportInf)
    WriteUtf8 (Join-Path $dir 'intel_fallback_update_result.json') ([ordered]@{
      Api='UpdateDriverForPlugAndPlayDevicesW';Succeeded=$true;RebootRequired=[bool]$fallbackRebootSignalled;AutomaticReboot=$false
    }|ConvertTo-Json)
    $fallbackIntel=$true;$after=WaitIntel $before.InstanceId 20
  }
  WriteUtf8 (Join-Path $dir 'target_after.json') ($after|ConvertTo-Json -Depth 8)
  $x=CertUtil @('-delstore','TrustedPublisher',$pkg.Thumb);if($x.ExitCode-ne0){throw 'CERT_PUBLISHER_REMOVE_FAILED'};$pubAdded=$false
  $x=CertUtil @('-delstore','Root',$pkg.Thumb);if($x.ExitCode-ne0){throw 'CERT_ROOT_REMOVE_FAILED'};$rootAdded=$false
  $ta=Trust $pkg.Thumb;if($ta.Root -or $ta.TrustedPublisher){throw 'CERT_TRUST_REMAINS'}
  $rollbackComplete=$true
}catch{
  $err=$_.Exception
  # Capture the failure before rollback, including failures after WaitM1.
  try{
    $failed=Target
    WriteUtf8 (Join-Path $dir 'target_transaction_failure.json') ($failed|ConvertTo-Json -Depth 8)
    $props=@(Get-PnpDeviceProperty -InstanceId $before.InstanceId -ErrorAction Stop|Select-Object KeyName,Type,Data)
    WriteUtf8 (Join-Path $dir 'target_transaction_failure_properties.json') ($props|ConvertTo-Json -Depth 10)
  }catch{}
  if($stageTrace){try{StopStageTrace $stageTrace $dir}catch{}}
}finally{
  # Diagnostic failure must never prevent the recovery path from running.
  if($stageTrace -and -not$stageTrace.Stopped){try{StopStageTrace $stageTrace $dir}catch{}}
  if(-not$rollbackComplete){
    $log=New-Object System.Collections.Generic.List[string]
    if($published){
      foreach($pi in @(PublishedM1)){
        $x=PnP @('/delete-driver',$pi,'/uninstall','/force');$log.Add("DELETE $pi EXIT=$($x.ExitCode)");$log.Add($x.Output)
      }
    }
    $safe=$false;$s=$null
    try{$s=WaitIntel $before.InstanceId 10;$safe=(IsIntel $s)-and(@(PublishedM1).Count-eq0)}catch{$log.Add("INTEL_RETURN_EXCEPTION=$($_.Exception.Message)")}
    # The old runner only had this fallback on the successful boot path.
    # Use the pre-bind export once; never bind M1 again or reboot here.
    if(-not$safe -and $bindAttempted -and -not$fallbackAttempted){
      try{
        if(@(PublishedM1).Count-ne0){throw 'M1_PACKAGE_REMAINS_BEFORE_INTEL_FALLBACK'}
        if(-not(Test-Path $baselineExportInf -PathType Leaf)){throw 'INTEL_EXPORT_MISSING_FOR_FALLBACK'}
        $fallbackAttempted=$true
        $fallbackRebootSignalled=[Phaser360.M1FastNative]::ForceUpdate($ExactHwid,$baselineExportInf)
        $fallbackIntel=$true
        WriteUtf8 (Join-Path $dir 'intel_fallback_update_result.json') ([ordered]@{
          Api='UpdateDriverForPlugAndPlayDevicesW';Succeeded=$true;RebootRequired=[bool]$fallbackRebootSignalled;AutomaticReboot=$false;Path='FAILURE_RECOVERY'
        }|ConvertTo-Json)
        $s=WaitIntel $before.InstanceId 20;$safe=(IsIntel $s)-and(@(PublishedM1).Count-eq0)
        $log.Add("INTEL_FALLBACK_SAFE=$safe")
      }catch{$log.Add("INTEL_FALLBACK_EXCEPTION=$($_.Exception.Message)")}
    }
    if($safe){
      if($pubAdded){$x=CertUtil @('-delstore','TrustedPublisher',$pkg.Thumb);$log.Add("CERT TrustedPublisher EXIT=$($x.ExitCode)");if($x.ExitCode-eq0){$pubAdded=$false}}
      if($rootAdded){$x=CertUtil @('-delstore','Root',$pkg.Thumb);$log.Add("CERT Root EXIT=$($x.ExitCode)");if($x.ExitCode-eq0){$rootAdded=$false}}
    }else{
      $log.Add('TRUST_RETAINED_FOR_SAFETY=TRUE')
      $log.Add('DO_NOT_REBOOT_UNTIL_TARGET_STATE_IS_REVIEWED=TRUE')
    }
    WriteUtf8 (Join-Path $dir 'emergency_rollback.txt') ($log -join [Environment]::NewLine)
  }
}

$final=$null;try{$final=Target}catch{}
$ft=Trust $pkg.Thumb
$baselineRestored=(IsIntel $final)-and(@(PublishedM1).Count-eq0)
$trustRestored=(-not$ft.Root -and -not$ft.TrustedPublisher)
$status=if($bootProved -and $rollbackComplete -and $baselineRestored -and $trustRestored -and -not$err){'M1_FAST_SAFE_DSP_BOOT_AND_INTEL_ROLLBACK_COMPLETE'}else{'M1_FAST_SAFE_TRANSACTION_FAILED'}
if($final){WriteUtf8 (Join-Path $dir 'target_final.json') ($final|ConvertTo-Json -Depth 8)}
WriteUtf8 (Join-Path $dir 'transaction.json') ([ordered]@{
 Status=$status;RunnerBuild=$Build;PublishedInf=$publishedInf;BindAttempted=$bindAttempted;BindRebootSignalled=$bindRebootSignalled;M1Bound=$m1Bound;BootProved=$bootProved
 BaselineRestored=$baselineRestored;TrustRestored=$trustRestored;IntelFallbackAttempted=$fallbackAttempted;IntelFallbackUsed=$fallbackIntel;IntelFallbackRebootSignalled=$fallbackRebootSignalled
 StageTraceProvider=$StageProviderGuid.ToString();StageTraceCaptured=[bool]($stageTrace -and $stageTrace.Stopped -and (Test-Path $stageTrace.Etl -PathType Leaf))
 StageTraceDecoded=[bool]($stageTrace -and $stageTrace.Decoded);StageTraceDriverEntries=$(if($stageTrace){$stageTrace.DriverEntries}else{0});StageTraceBootEntries=$(if($stageTrace){$stageTrace.BootEntries}else{0});StageTraceStopExitCode=$(if($stageTrace){$stageTrace.StopExitCode}else{$null})
 StageTraceLossChecked=[bool]($stageTrace -and $stageTrace.LossChecked);StageTraceEventsLost=$(if($stageTrace){$stageTrace.EventsLost}else{$null});StageTraceBuffersLost=$(if($stageTrace){$stageTrace.BuffersLost}else{$null})
 TransactionError=$(if($err){$err.Message}else{$null});FirmwareSha256=$FirmwareSha;NHLTSha256=$NHLTSha
 NHLTHashSource='DECLARED_REFERENCE_ONLY';NHLTObservedSha256=$null
 AudioPlayback='NO';CodecProgramming='NO';SpeakerEnable='NO';AutomaticReboot='NO';BcdWrite='NO'
}|ConvertTo-Json -Depth 6)
$pointerFinal=@("PHASER360_M1_FAST_SAFE_RECOVERY=1","STATUS=$status","CUSTOM_INF=$publishedInf","BASELINE_RESTORED=$baselineRestored","TRUST_RESTORED=$trustRestored","DO_NOT_REBOOT=$(if($baselineRestored){'FALSE'}else{'TRUE'})") -join [Environment]::NewLine
WriteUtf8 (Join-Path $OutputRoot 'M1_FAST_RECOVERY_POINTER.txt') $pointerFinal
Hashes $dir;$zip=Join-Path $OutputRoot ('RESULT_M1_FAST_SAFE_'+$stamp+'_'+$suffix+'.zip');Compress-Archive -Path (Join-Path $dir '*') -DestinationPath $zip -Force
Write-Host "STATUS=$status";Write-Host "BOOT_PROVED=$($bootProved.ToString().ToUpperInvariant())";Write-Host "BIND_REBOOT_SIGNALLED=$($bindRebootSignalled.ToString().ToUpperInvariant())";Write-Host "STAGE_TRACE_CAPTURED=$([bool]($stageTrace -and $stageTrace.Stopped -and (Test-Path $stageTrace.Etl -PathType Leaf)))";Write-Host "BASELINE_RESTORED=$($baselineRestored.ToString().ToUpperInvariant())";Write-Host "TRUST_RESTORED=$($trustRestored.ToString().ToUpperInvariant())";Write-Host "PUBLISHED_INF=$publishedInf";Write-Host "Trimite fisierul: $zip"
if($err){Write-Host "ERROR=$($err.Message)"}
if($status-ne'M1_FAST_SAFE_DSP_BOOT_AND_INTEL_ROLLBACK_COMPLETE'){exit 3}
