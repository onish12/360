#requires -Version 5.1
$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$script:H15cExactHwid='PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06'
$script:H15cExtensionId='{53f678f1-2b3c-4b2e-a15c-360031980001}'
$script:H15cService='Phaser360H15cLive'
$script:H15cCertFile='phaser360_h15c_live_filter.cer'
$script:H15cManifestFile='package_manifest.json'
$script:H15cCertSubject='CN=PHASER360 H15C-LIVE R2 Ephemeral Test Signing'
$script:H15cCodeSigningOid='1.3.6.1.5.5.7.3.3'

function Test-H15cAdministrator {
    $id=[Security.Principal.WindowsIdentity]::GetCurrent()
    $p=[Security.Principal.WindowsPrincipal]::new($id)
    return $p.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

function Get-H15cTargetState {
    $devices=@(Get-PnpDevice -PresentOnly -ErrorAction Stop |
        Where-Object {$_.InstanceId -like 'PCI\VEN_8086&DEV_3198*'})
    if($devices.Count -ne 1){throw "EXPECTED_ONE_PRESENT_DEV3198: count=$($devices.Count)"}
    $id=[string]$devices[0].InstanceId
    $props=@(Get-PnpDeviceProperty -InstanceId $id -ErrorAction Stop)
    $map=@{};foreach($p in $props){$map[[string]$p.KeyName]=$p.Data}
    foreach($required in @(
        'DEVPKEY_Device_HardwareIds','DEVPKEY_Device_ProblemCode',
        'DEVPKEY_Device_Service','DEVPKEY_Device_DriverInfPath',
        'DEVPKEY_Device_DriverVersion','DEVPKEY_Device_DriverProvider')){
        if(-not $map.ContainsKey($required)){throw "TARGET_PROPERTY_MISSING: $required"}
    }
    $upper=@()
    $upperSource='ABSENT'
    $upperQueryError=$null
    try {
        $explicit=@(Get-PnpDeviceProperty -InstanceId $id -KeyName 'DEVPKEY_Device_CompoundUpperFilters' -ErrorAction Stop |
            Where-Object {$_.KeyName -ceq 'DEVPKEY_Device_CompoundUpperFilters'})
        if($explicit.Count -eq 1){
            $upper=@($explicit[0].Data)
            $upperSource='EXPLICIT_KEY_QUERY'
        }
    } catch {
        $upperQueryError=$_.Exception.Message
        if($map.ContainsKey('DEVPKEY_Device_CompoundUpperFilters')){
            $upper=@($map['DEVPKEY_Device_CompoundUpperFilters'])
            $upperSource='ENUMERATION_FALLBACK'
        }
    }
    [pscustomobject]@{
        InstanceId=$id
        Status=[string]$devices[0].Status
        ProblemCode=[int]$map['DEVPKEY_Device_ProblemCode']
        Service=[string]$map['DEVPKEY_Device_Service']
        DriverInfPath=[string]$map['DEVPKEY_Device_DriverInfPath']
        DriverVersion=[string]$map['DEVPKEY_Device_DriverVersion']
        DriverProvider=[string]$map['DEVPKEY_Device_DriverProvider']
        HardwareIds=@($map['DEVPKEY_Device_HardwareIds'])
        CompoundUpperFilters=$upper
        CompoundUpperFiltersSource=$upperSource
        CompoundUpperFiltersQueryError=$upperQueryError
    }
}

function Assert-H15cExactHealthyIntelTarget($State) {
    if([Environment]::OSVersion.Version.Build -ne 19044){throw 'EXACT_WINDOWS_BUILD_19044_REQUIRED'}
    if($State.ProblemCode -ne 0 -or $State.Status -cne 'OK'){throw 'TARGET_NOT_HEALTHY'}
    if(@($State.HardwareIds|Where-Object {$_ -ceq $script:H15cExactHwid}).Count -ne 1){
        throw 'EXACT_HARDWARE_ID_NOT_PRESENT'
    }
    if($State.Service -cne 'IntcAudioBus'){throw "INTC_AUDIO_BUS_NOT_BOUND: $($State.Service)"}
    if([string]::IsNullOrWhiteSpace($State.DriverInfPath)){throw 'BASE_DRIVER_INF_MISSING'}
}

function Initialize-H15cNative {
    if('Phaser360.H15cGateNative' -as [type]){return}
    $source=@'
using System;
using System.Runtime.InteropServices;
namespace Phaser360 {
  public static class H15cGateNative {
    [StructLayout(LayoutKind.Sequential)]
    struct CI { public UInt32 Length; public UInt32 Options; }
    [DllImport("ntdll.dll")]
    static extern Int32 NtQuerySystemInformation(
      Int32 informationClass,ref CI information,UInt32 length,IntPtr returned);
    public static UInt32 CodeIntegrityOptions() {
      CI ci=new CI(); ci.Length=(UInt32)Marshal.SizeOf(typeof(CI));
      Int32 status=NtQuerySystemInformation(103,ref ci,ci.Length,IntPtr.Zero);
      if(status<0) throw new InvalidOperationException(
        "NtQuerySystemInformation(SystemCodeIntegrityInformation)=0x"+
        unchecked((UInt32)status).ToString("X8"));
      return ci.Options;
    }
  }
}
'@
    Add-Type -TypeDefinition $source -Language CSharp -ErrorAction Stop
}

function Get-H15cCodeIntegrity {
    Initialize-H15cNative
    $o=[uint32][Phaser360.H15cGateNative]::CodeIntegrityOptions()
    [pscustomobject]@{
        Options=('0x{0:X8}' -f $o)
        KernelCiEnabled=(($o -band 0x1) -ne 0)
        TestSignAllowed=(($o -band 0x2) -ne 0)
        HvciKmciEnabled=(($o -band 0x400) -ne 0)
        HvciAudit=(($o -band 0x800) -ne 0)
        HvciStrict=(($o -band 0x1000) -ne 0)
    }
}

function Get-H15cSecureBoot {
    try {
        [pscustomobject]@{Known=$true;Enabled=[bool](Confirm-SecureBootUEFI -ErrorAction Stop)}
    } catch {
        [pscustomobject]@{Known=$false;Enabled=$null;Error=$_.Exception.Message}
    }
}

function Get-H15cWinRE {
    $exe=Join-Path $env:SystemRoot 'System32\reagentc.exe'
    if(-not (Test-Path -LiteralPath $exe -PathType Leaf)){throw 'REAGENTC_NOT_FOUND'}
    $old=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try{$text=(& $exe /info 2>&1|Out-String -Width 8192);$code=$LASTEXITCODE}
    finally{$ErrorActionPreference=$old}
    $status='UNKNOWN'
    if($text -match '(?im)Windows\s+RE.*(?:Enabled|Activat)'){$status='ENABLED'}
    elseif($text -match '(?im)Windows\s+RE.*(?:Disabled|Dezactivat)'){$status='DISABLED'}
    [pscustomobject]@{ExitCode=$code;Status=$status;Output=$text}
}

function Get-H15cPublishedInf {
    $matches=New-Object System.Collections.Generic.List[string]
    $infRoot=Join-Path $env:SystemRoot 'INF'
    foreach($file in @(Get-ChildItem -LiteralPath $infRoot -Filter 'oem*.inf' -File -ErrorAction Stop)){
        try{$text=Get-Content -LiteralPath $file.FullName -Raw -ErrorAction Stop}catch{continue}
        if($text.IndexOf($script:H15cExtensionId,[StringComparison]::OrdinalIgnoreCase) -ge 0 -and
           $text.IndexOf('Phaser360H15cLive',[StringComparison]::OrdinalIgnoreCase) -ge 0){
            $matches.Add($file.Name)
        }
    }
    return @($matches)
}

function Get-H15cCertificatePresence([string]$Thumbprint) {
    if([string]::IsNullOrWhiteSpace($Thumbprint)){throw 'CERT_THUMBPRINT_REQUIRED'}
    $thumb=$Thumbprint.Replace(' ','').ToUpperInvariant()
    [pscustomobject]@{
        Thumbprint=$thumb
        Root=(Test-Path -LiteralPath ("Cert:\LocalMachine\Root\"+$thumb))
        TrustedPublisher=(Test-Path -LiteralPath ("Cert:\LocalMachine\TrustedPublisher\"+$thumb))
    }
}

function Assert-H15cPackage([string]$PackageRoot,[switch]$RequireTrusted) {
    if([string]::IsNullOrWhiteSpace($PackageRoot)){throw 'PACKAGE_ROOT_REQUIRED'}
    $root=(Resolve-Path -LiteralPath $PackageRoot -ErrorAction Stop).Path
    $inf=Join-Path $root 'phaser360_h15c_live_filter.inf'
    $sys=Join-Path $root 'phaser360_h15c_live_filter.sys'
    $cat=Join-Path $root 'phaser360_h15c_live_filter.cat'
    $cer=Join-Path $root $script:H15cCertFile
    $manifestPath=Join-Path $root $script:H15cManifestFile
    foreach($p in @($inf,$sys,$cat,$cer,$manifestPath)){
        if(-not (Test-Path -LiteralPath $p -PathType Leaf)){throw "PACKAGE_FILE_MISSING: $p"}
    }

    $cert=Get-PfxCertificate -FilePath $cer
    if(-not $cert){throw 'CERTIFICATE_PARSE_FAILED'}
    if($cert.HasPrivateKey){throw 'PUBLIC_CER_HAS_PRIVATE_KEY'}
    if($cert.Subject -cne $script:H15cCertSubject -or $cert.Issuer -cne $script:H15cCertSubject){
        throw "CERTIFICATE_IDENTITY_INVALID: $($cert.Subject)"
    }
    $now=[DateTime]::UtcNow
    if($cert.NotBefore.ToUniversalTime() -gt $now -or $cert.NotAfter.ToUniversalTime() -le $now){
        throw 'CERTIFICATE_NOT_CURRENTLY_VALID'
    }
    $eku=New-Object System.Collections.Generic.List[string]
    foreach($extension in $cert.Extensions){
        if($extension.Oid.Value -eq '2.5.29.37'){
            $typed=[System.Security.Cryptography.X509Certificates.X509EnhancedKeyUsageExtension]$extension
            foreach($oid in $typed.EnhancedKeyUsages){$eku.Add([string]$oid.Value)}
        }
    }
    if(@($eku|Where-Object {$_ -ceq $script:H15cCodeSigningOid}).Count -ne 1){
        throw 'CODE_SIGNING_EKU_MISSING'
    }

    $manifest=Get-Content -LiteralPath $manifestPath -Raw|ConvertFrom-Json
    if([string]$manifest.CertificateThumbprint -cne [string]$cert.Thumbprint){
        throw 'MANIFEST_CERTIFICATE_THUMBPRINT_MISMATCH'
    }
    foreach($entry in @(
        @('INF',$inf,[string]$manifest.InfSha256),
        @('SYS',$sys,[string]$manifest.SysSha256),
        @('CAT',$cat,[string]$manifest.CatSha256),
        @('CER',$cer,[string]$manifest.CerSha256)
    )){
        $actual=(Get-FileHash -LiteralPath $entry[1] -Algorithm SHA256).Hash.ToLowerInvariant()
        if($actual -cne $entry[2].ToLowerInvariant()){throw "MANIFEST_HASH_MISMATCH: $($entry[0])"}
    }

    $sysSig=Get-AuthenticodeSignature -LiteralPath $sys
    $catSig=Get-AuthenticodeSignature -LiteralPath $cat
    foreach($pair in @(@('SYS',$sysSig),@('CAT',$catSig))){
        if(-not $pair[1].SignerCertificate){throw "$($pair[0])_SIGNER_MISSING"}
        if([string]$pair[1].SignerCertificate.Thumbprint -cne [string]$cert.Thumbprint){
            throw "$($pair[0])_SIGNER_CERT_MISMATCH"
        }
        if($pair[1].Status -eq 'HashMismatch' -or $pair[1].Status -eq 'NotSigned'){
            throw "$($pair[0])_SIGNATURE_DAMAGED: $($pair[1].Status)"
        }
    }
    if($RequireTrusted){
        if($sysSig.Status -ne 'Valid'){throw "SYS_SIGNATURE_NOT_VALID_AFTER_TRUST: $($sysSig.Status)"}
        if($catSig.Status -ne 'Valid'){throw "CAT_SIGNATURE_NOT_VALID_AFTER_TRUST: $($catSig.Status)"}
    }

    $infText=Get-Content -LiteralPath $inf -Raw
    if($infText.IndexOf($script:H15cExtensionId,[StringComparison]::OrdinalIgnoreCase) -lt 0 -or
       $infText.IndexOf($script:H15cExactHwid,[StringComparison]::OrdinalIgnoreCase) -lt 0){
        throw 'PACKAGE_INF_IDENTITY_MISMATCH'
    }

    [pscustomobject]@{
        Root=$root;Inf=$inf;Sys=$sys;Cat=$cat;Certificate=$cer;Manifest=$manifestPath
        SourceCommit=[string]$manifest.SourceCommit
        InfSha256=[string]$manifest.InfSha256
        SysSha256=[string]$manifest.SysSha256
        CatSha256=[string]$manifest.CatSha256
        CertificateSha256=[string]$manifest.CerSha256
        CertificateThumbprint=[string]$cert.Thumbprint
        CertificateSubject=[string]$cert.Subject
        CertificateNotAfter=$cert.NotAfter.ToUniversalTime().ToString('o')
        SysAuthenticode=[string]$sysSig.Status
        CatAuthenticode=[string]$catSig.Status
        TrustedRequired=[bool]$RequireTrusted
    }
}

function Invoke-H15cPnPUtil([string[]]$Arguments) {
    $exe=Join-Path $env:SystemRoot 'System32\pnputil.exe'
    if(-not (Test-Path -LiteralPath $exe -PathType Leaf)){throw 'PNPUTIL_NOT_FOUND'}
    $old=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try{$out=(& $exe @Arguments 2>&1|Out-String -Width 8192);$code=$LASTEXITCODE}
    finally{$ErrorActionPreference=$old}
    [pscustomobject]@{ExitCode=$code;Output=$out;Arguments=@($Arguments)}
}

function Invoke-H15cCertUtil([string[]]$Arguments) {
    $exe=Join-Path $env:SystemRoot 'System32\certutil.exe'
    if(-not (Test-Path -LiteralPath $exe -PathType Leaf)){throw 'CERTUTIL_NOT_FOUND'}
    $old=$ErrorActionPreference;$ErrorActionPreference='Continue'
    try{$out=(& $exe @Arguments 2>&1|Out-String -Width 8192);$code=$LASTEXITCODE}
    finally{$ErrorActionPreference=$old}
    [pscustomobject]@{ExitCode=$code;Output=$out;Arguments=@($Arguments)}
}

function Wait-H15cTargetHealthy([string]$InstanceId,[int]$Seconds=15) {
    $deadline=(Get-Date).AddSeconds($Seconds)
    do {
        Start-Sleep -Milliseconds 500
        try{$s=Get-H15cTargetState}catch{$s=$null}
        if($s -and $s.InstanceId -ceq $InstanceId -and $s.ProblemCode -eq 0 -and
           $s.Status -ceq 'OK' -and $s.Service -ceq 'IntcAudioBus'){return $s}
    } while((Get-Date) -lt $deadline)
    throw 'TARGET_DID_NOT_RETURN_HEALTHY'
}

function Write-H15cHashes([string]$Directory) {
    Get-ChildItem -LiteralPath $Directory -Recurse -File |
        Where-Object {$_.Name -ne 'SHA256SUMS.txt'} |
        Sort-Object FullName | ForEach-Object {
            (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()+
            '  '+$_.FullName.Substring($Directory.Length).TrimStart('\')
        } | Set-Content -LiteralPath (Join-Path $Directory 'SHA256SUMS.txt') -Encoding ASCII
}
