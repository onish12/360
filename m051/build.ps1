$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 2
$repo = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repo
function Find-WdkTool([string]$Name) {
    $files = @(Get-ChildItem (Join-Path $PSScriptRoot 'packages') -Recurse -File -Filter $Name)
    $x64 = @($files | Where-Object { $_.FullName -match '\\x64\\' } | Sort-Object FullName -Descending)
    if ($x64.Count) { return $x64[0].FullName }
    if ($files.Count) { return $files[0].FullName }
    throw "WDK_TOOL_MISSING: $Name"
}
nuget restore '.\m051\packages.config' -PackagesDirectory '.\m051\packages' -NonInteractive
if ($LASTEXITCODE -ne 0) { throw 'NUGET_RESTORE_FAILED' }
msbuild '.\m051\driver\phaser360_m051_mmio_ro.vcxproj' /m /t:Rebuild `
    /p:Configuration=Release /p:Platform=x64 /p:KMDF_VERSION_MAJOR=1 /p:KMDF_VERSION_MINOR=33 `
    /p:TargetVersion=Windows10 /p:SignMode=Off /verbosity:minimal
if ($LASTEXITCODE -ne 0) { throw 'WDK_BUILD_FAILED' }

$stage = Join-Path $repo '_artifact_m051'
New-Item -ItemType Directory -Path (Join-Path $stage 'driver') -Force | Out-Null
Copy-Item '.\m051\driver\out\phaser360_m051_mmio_ro.sys' (Join-Path $stage 'driver')
Copy-Item '.\m051\driver\obj\phaser360_m051_mmio_ro.inf' (Join-Path $stage 'driver')
Copy-Item '.\m051\runtime' $stage -Recurse
foreach ($name in @('RUN_M051.cmd','RECOVER_WINRE.cmd','START_AICI.txt')) {
    Copy-Item (Join-Path $PSScriptRoot $name) $stage
}
New-Item -ItemType Directory -Path (Join-Path $stage 'source') -Force | Out-Null
foreach ($name in @('phaser360_m051_mmio_ro.cpp','resource_contract.h','phaser360_m051_mmio_ro.vcxproj')) {
    Copy-Item (Join-Path $PSScriptRoot "driver\$name") (Join-Path $stage 'source')
}
Copy-Item '.\docs\M051_FRESH_WINDOWS.md' $stage
$sys = Join-Path $stage 'driver\phaser360_m051_mmio_ro.sys'
$inf = Join-Path $stage 'driver\phaser360_m051_mmio_ro.inf'
$cat = Join-Path $stage 'driver\phaser360_m051_mmio_ro.cat'
$infverif = Find-WdkTool 'infverif.exe'
$signtool = Find-WdkTool 'signtool.exe'
$inf2cat = Find-WdkTool 'inf2cat.exe'
& $infverif /w $inf
if ($LASTEXITCODE -ne 0) { throw 'INFVERIF_FAILED' }

$cert = New-SelfSignedCertificate -Type CodeSigningCert -Subject 'CN=PHASER360 M0.5.1 Test Signing' `
    -CertStoreLocation 'Cert:\CurrentUser\My' -HashAlgorithm SHA256 -KeyAlgorithm RSA `
    -KeyLength 3072 -KeyExportPolicy NonExportable -NotAfter (Get-Date).AddYears(2)
Export-Certificate -Cert $cert -FilePath (Join-Path $stage 'PHASER360_M051_TEST_SIGNING.cer') -Type CERT | Out-Null
& $signtool sign /v /fd SHA256 /sha1 $cert.Thumbprint /s My $sys
if ($LASTEXITCODE -ne 0) { throw 'EMBEDDED_SYS_SIGN_FAILED' }
# Regenerate the catalog AFTER the embedded SYS signature changes the file.
& $inf2cat "/driver:$(Join-Path $stage 'driver')" /os:10_CO_X64,10_NI_X64,10_GE_X64,10_25H2_X64 /uselocaltime
if ($LASTEXITCODE -ne 0) { throw 'INF2CAT_FAILED' }
$sysHash = (Get-FileHash $sys -Algorithm SHA256).Hash
$infHash = (Get-FileHash $inf -Algorithm SHA256).Hash
& $signtool sign /v /fd SHA256 /sha1 $cert.Thumbprint /s My $cat
if ($LASTEXITCODE -ne 0) { throw 'CAT_SIGN_FAILED' }
if ((Get-FileHash $sys -Algorithm SHA256).Hash -ne $sysHash -or
    (Get-FileHash $inf -Algorithm SHA256).Hash -ne $infHash) { throw 'CATALOG_MEMBERS_CHANGED' }

# Verify CMS cryptography and the expected signer without CI trust-store writes.
Add-Type -AssemblyName System.Security.Cryptography.Pkcs
function Check-Cms([byte[]]$Bytes) {
    $cms = [Security.Cryptography.Pkcs.SignedCms]::new()
    $cms.Decode($Bytes); $cms.CheckSignature($true)
    if ($cms.SignerInfos.Count -ne 1 -or $cms.SignerInfos[0].Certificate.Thumbprint -ne $cert.Thumbprint) {
        throw 'CMS_SIGNER_MISMATCH'
    }
}
Check-Cms ([IO.File]::ReadAllBytes($cat))
$pe = [IO.File]::ReadAllBytes($sys)
$peOffset = [BitConverter]::ToInt32($pe,0x3c)
if ([BitConverter]::ToUInt32($pe,$peOffset) -ne 0x4550 -or
    [BitConverter]::ToUInt16($pe,$peOffset+24) -ne 0x20b) { throw 'EXPECTED_PE32_PLUS' }
$securityDirectory = $peOffset + 24 + 112 + 4*8
$securityOffset = [BitConverter]::ToUInt32($pe,$securityDirectory)
$securitySize = [BitConverter]::ToUInt32($pe,$securityDirectory+4)
if ($securityOffset -eq 0 -or $securitySize -lt 8 -or
    [uint64]$securityOffset + $securitySize -gt $pe.Length) { throw 'MISSING_SYS_WIN_CERTIFICATE' }
$certLength = [BitConverter]::ToUInt32($pe,$securityOffset)
if ($certLength -lt 8 -or $certLength -gt $securitySize -or
    [BitConverter]::ToUInt16($pe,$securityOffset+6) -ne 2) { throw 'INVALID_SYS_WIN_CERTIFICATE' }
$cmsBytes = [byte[]]::new($certLength-8)
[Array]::Copy($pe,$securityOffset+8,$cmsBytes,0,$cmsBytes.Length)
Check-Cms $cmsBytes
Write-Host 'SYS_AND_CAT_CMS_SIGNATURES=PASS; trust-chain validation not claimed'

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vsRoot = (& $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath | Select-Object -First 1)
$dumpbin = Get-ChildItem (Join-Path $vsRoot 'VC\Tools\MSVC') -Recurse -File -Filter dumpbin.exe |
    Where-Object { $_.FullName -match '\\bin\\Hostx64\\x64\\dumpbin\.exe$' } |
    Sort-Object FullName -Descending | Select-Object -First 1
if (-not $dumpbin) { throw 'DUMPBIN_MISSING' }
$imports = (& $dumpbin.FullName /imports $sys | Out-String)
if ($LASTEXITCODE -ne 0) { throw 'DUMPBIN_FAILED' }
$imports | Set-Content (Join-Path $stage 'BINARY_IMPORTS.txt') -Encoding ascii
foreach ($required in @('MmMapIoSpaceEx','MmUnmapIoSpace')) {
    if (-not $imports.Contains($required)) { throw "MISSING_IMPORT: $required" }
}
foreach ($forbidden in @('WRITE_REGISTER_', 'MmAllocateContiguousMemory','MmAllocatePagesForMdl')) {
    if ($imports.Contains($forbidden)) { throw "FORBIDDEN_IMPORT: $forbidden" }
}
@("COMMIT=$env:GITHUB_SHA", "SYS_SHA256=$sysHash", "INF_SHA256=$infHash",
    "PUBLIC_CERT_THUMBPRINT=$($cert.Thumbprint)", 'SYS_EMBEDDED_CMS=PASS', 'CAT_CMS=PASS',
    'CAT_GENERATED_AFTER_SYS_SIGN=TRUE','CI_TRUST_STORES_CHANGED=FALSE',
    'HARDWARE_EXECUTION=NOT_TESTED','AUDIO_PLAYBACK=NOT_IMPLEMENTED',
    'DEVICE_MMIO_WRITES=NONE','DMA_IRQ_FIRMWARE_IPC=NONE','RUNTIME_BASELINE=UNBOUND_CODE28') |
    Set-Content (Join-Path $stage 'BUILD_AUDIT.txt') -Encoding ascii
$manifest = Join-Path $stage 'SHA256SUMS.txt'
$lines = @(Get-ChildItem $stage -Recurse -File | Where-Object { $_.Name -ne 'SHA256SUMS.txt' } |
    Sort-Object FullName | ForEach-Object {
        $rel = $_.FullName.Substring($stage.Length+1).Replace('\','/')
        (Get-FileHash $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant() + '  ' + $rel
    })
$lines | Set-Content $manifest -Encoding ascii
& (Join-Path $stage 'runtime\VERIFY_PACKAGE.ps1') -Root $stage
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath (Join-Path $repo 'PHASER360_M051_FRESH_WINDOWS.zip') -Force
Write-Host 'M051_PACKAGE=PASS'
exit 0
