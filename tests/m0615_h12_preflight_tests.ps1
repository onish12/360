$ErrorActionPreference='Stop'
Set-StrictMode -Version 2

$root=Join-Path $PSScriptRoot '..'
$preflight=Get-Content -LiteralPath (Join-Path $root 'm062\m1\Collect-M1Preflight.ps1') -Raw
$contract=Get-Content -LiteralPath (Join-Path $root 'm062\m1\target_contract.json') -Raw | ConvertFrom-Json
$workflow=Get-Content -LiteralPath (Join-Path $root '.github\workflows\build-m062-dma.yml') -Raw
$doc=Get-Content -LiteralPath (Join-Path $root 'docs\M0615_M1_PREFLIGHT_RECOVERY.md') -Raw

if([int]$contract.windows_build_exact -ne 19044){throw 'H12_BUILD_NOT_EXACT_19044'}
if([string]$contract.controller_hardware_id -cne 'PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06'){throw 'H12_TARGET_HWID_INVALID'}
if([int]$contract.hda_bar_bytes -ne 0x4000 -or [int]$contract.dsp_bar_bytes -ne 0x100000){throw 'H12_BAR_CONTRACT_INVALID'}
if([int]$contract.pci_interrupt_support -ne 3 -or [int]$contract.pci_interrupt_message_maximum -ne 1){throw 'H12_IRQ_CONTRACT_INVALID'}
if([string]$contract.nhlt_sha256 -cne '4764aba0316e9a039a127285cc4ff9e97e22c75bfddb6d865d9f77b59dd2a6b9'){throw 'H12_NHLT_PIN_INVALID'}
if([string]$contract.firmware_sha256 -cne '40029b5a05665f19a492ef00b8c0a24c42e90d7c00fc57146e07947fd1407d5c'){throw 'H12_FIRMWARE_PIN_INVALID'}
foreach($flag in @('speaker_amplifier_enable_authorized','codec_programming_authorized','audio_playback_authorized','automatic_reboot_authorized')){
    if([bool]$contract.$flag){throw "H12_SAFETY_FLAG_TRUE: $flag"}
}

foreach($required in @(
    'Read-TargetContract','Get-PnpDevice -PresentOnly','Get-PnpDeviceProperty',
    'DEVPKEY_PciDevice_InterruptSupport','DEVPKEY_PciDevice_InterruptMessageMaximum',
    'reagentc.exe','& $exe /info','if(-not $ready){exit 2}',
    'MODE=READ_ONLY_PREFLIGHT','DRIVER_INSTALL=NO','DRIVER_BIND_UNBIND=NO',
    'DEVICE_RESTART=NO','REGISTRY_WRITE=NO','BCD_WRITE=NO','MMIO=NO','DSP_BOOT=NO','AUDIO_PLAYBACK=NO'
)){
    if($preflight.IndexOf($required,[StringComparison]::Ordinal) -lt 0){throw "H12_PREFLIGHT_REQUIRED_MISSING: $required"}
}

foreach($forbidden in @(
    'pnputil.exe','/add-driver','/delete-driver','/export-driver','/restart-device',
    '/enable-device','/disable-device','devcon','Enable-PnpDevice','Disable-PnpDevice',
    'Set-PnpDevice','Set-ItemProperty','New-ItemProperty','Remove-ItemProperty',
    'Set-Service','Start-Service','Stop-Service','sc.exe','reg.exe','bcdedit',
    'dism.exe','Remove-WindowsDriver','Add-WindowsDriver','Restart-Computer',
    'Stop-Computer','shutdown.exe','READ_REGISTER_','WRITE_REGISTER_','MmMapIoSpace'
)){
    if($preflight.IndexOf($forbidden,[StringComparison]::OrdinalIgnoreCase) -ge 0){throw "H12_PREFLIGHT_MUTATION_FORBIDDEN: $forbidden"}
}

foreach($required in @(
    'phaser360_m1_boot.inf',
    'dism /Image:<WINDOWS_VOLUME>:\ /Get-Drivers /Format:Table',
    'dism /Image:<WINDOWS_VOLUME>:\ /Remove-Driver /Driver:oemNN.inf',
    'first physical M1 remains strictly'
)){
    if($doc.IndexOf($required,[StringComparison]::Ordinal) -lt 0){throw "H12_RECOVERY_DOC_MISSING: $required"}
}

foreach($required in @(
    'H12_M1_PREFLIGHT_SELFTEST=PASS','H12_M1_PREFLIGHT_STATIC_TESTS=PASS',
    'M1_PREFLIGHT=READ_ONLY_EXACT_TARGET_WIN10_19044_WINRE_REQUIRED',
    'RECOVERY=WINRE_OFFLINE_DISM_DOCUMENTED_NOT_EXECUTED',
    'INSTALLABLE=FALSE','SYS_UPLOADED=FALSE','AUDIO_PLAYBACK=NOT_IMPLEMENTED'
)){
    if($workflow.IndexOf($required,[StringComparison]::Ordinal) -lt 0){throw "H12_WORKFLOW_GUARD_MISSING: $required"}
}

if($workflow -match '(?im)Copy-Item[^\r\n]*\.(sys|inf|cat|cer|pfx|p12)\b'){throw 'H12_INSTALLABLE_OR_KEY_MATERIAL_COPY_FORBIDDEN'}

Write-Host 'H12_M1_PREFLIGHT_STATIC_TESTS=PASS; target=EXACT_DEV3198_REV06; windows_build=19044; winre=REQUIRED; preflight_mutations=NONE; recovery=DOCUMENTED_NOT_EXECUTED; installable=NO; playback=NO'
