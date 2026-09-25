$ErrorActionPreference='Stop'
Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$hdr=Get-Content (Join-Path $root 'm062\h15d_live\h15d_live_filter.h') -Raw
$src=Get-Content (Join-Path $root 'm062\h15d_live\h15d_live_filter.cpp') -Raw
$proj=Get-Content (Join-Path $root 'm062\h15d_live\phaser360_h15d_live_filter.vcxproj') -Raw
$inf=Get-Content (Join-Path $root 'm062\h15d_live\phaser360_h15d_live_filter.inf') -Raw
$run=Get-Content (Join-Path $root 'm062\h15d_live\Run-H15dLiveR1.ps1') -Raw
$wf=Get-Content (Join-Path $root '.github\workflows\h15d-live-r1-package.yml') -Raw
$runnerPath=Join-Path $root 'm062\h15d_live\Run-H15dLiveR1.ps1'
$ps51=Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
if(-not (Test-Path -LiteralPath $ps51 -PathType Leaf)){throw 'H15D_PS51_NOT_FOUND'}
$parseHarness=Join-Path $env:RUNNER_TEMP 'h15d_ps51_parse.ps1'
@'
param([Parameter(Mandatory=$true)][string]$Path)
$tokens=$null
$errors=$null
[System.Management.Automation.Language.Parser]::ParseFile(
    $Path,[ref]$tokens,[ref]$errors) | Out-Null
if(@($errors).Count -ne 0){
    $errors | ForEach-Object { Write-Error $_.Message }
    exit 1
}
Write-Host 'H15D_PS51_PARSE=PASS'
'@ | Set-Content -LiteralPath $parseHarness -Encoding ASCII
& $ps51 -NoLogo -NoProfile -ExecutionPolicy Bypass -File $parseHarness -Path $runnerPath
if($LASTEXITCODE -ne 0){throw 'H15D_PS51_RUNNER_PARSE_FAILED'}

foreach($x in @('0x8338e458u','0x00000010u','0x807b0dffu','0x00000014u','0x807b0dfdu','kH15dRequiredSuccessFlags=0x7ffu','H15dFullConfigRestoredExact','sizeof(H15dLiveResultV1)==52u')){if($hdr.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15D_HDR_MISSING: $x"}}
foreach($x in @('WdfExecutionLevelPassive','WdfSynchronizationScopeDevice','WdfIoQueueDispatchSequential','queueConfig.PowerManaged=WdfFalse','H15dLiveEvtDeviceD0Entry','H15dLiveEvtDeviceD0Exit','InterlockedCompareExchange(&context->d0,0,0)==0','InterlockedCompareExchange(&context->consumed,1,0)','gateStorage','H15dLiveEvtReleaseHardware','H15dLiveEvtSurpriseRemoval','gate->SurpriseRemove()','gate->CloseForRelease()','PciConfigAttestation before','PciConfigAttestation applied','RtlCompareMemory(expected,applied.Snapshot().config','PciConfigBootPolicy policy','policy.Apply(device,pci,*gate)','policy.Restore()','H15dAppliedReadbackExact','H15dFinalBaselineExact')){if($src.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15D_SRC_MISSING: $x"}}
foreach($x in @('READ_REGISTER_','WRITE_REGISTER_','MmMapIoSpace','WdfDma','glk_boot','hda_transport','ipc_interrupt','firmware_source')){if(($src+$proj).IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15D_FORBIDDEN: $x"}}
$policy=Get-Content (Join-Path $root 'm062\driver\pci_config_boot_policy.cpp') -Raw
foreach($x in @('ReadConfigImage(bus,liveConfig)','IdentityMatches(liveConfig,evidence)','StableHeaderMatches(liveConfig,evidence)','CapabilityStructureMatches(liveConfig,evidence)','OwnedDwordsMatch(liveConfig,evidence)','PciConfigBootFailure::CgctlGateLost','PciConfigBootFailure::PgctlGateLost')){if($policy.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15D_POLICY_MISSING: $x"}}
if($policy.IndexOf('RtlCompareMemory(liveConfig,evidence.config,kPciConfigSnapshotBytes)',[StringComparison]::OrdinalIgnoreCase)-ge0){throw 'H15D_POLICY_FULL_256_BYTE_LIVE_FREEZE_FORBIDDEN'}
foreach($x in @('pci_config_attestation.cpp','pci_config_boot_policy.cpp')){if($proj.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15D_PROJECT_MISSING: $x"}}
foreach($x in @('Class=Extension','ExtensionId={53f678f1-2b3c-4b2e-a15d-360031980001}','PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06','AddFilter=Phaser360H15dLive','FilterPosition=Upper','KmdfLibraryVersion=1.31')){if($inf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15D_INF_MISSING: $x"}}
foreach($x in @("[Convert]::ToUInt32('8338E458',16)","[Convert]::ToUInt32('00000010',16)","[Convert]::ToUInt32('807B0DFF',16)","[Convert]::ToUInt32('00000014',16)","[Convert]::ToUInt32('807B0DFD',16)",'GENERIC_READ|GENERIC_WRITE','DeviceIoControl H15D transaction','MANIFEST_TRANSACTION_CONTRACT_MISMATCH','CODE_SIGNING_EKU_MISSING','PHASER_DIAGNOSTIC_FILTER_ALREADY_ATTACHED','$ioAttempted=$true','((-not $ioAttempted) -or $ioComplete)','TRUST_RETAINED_FOR_SAFETY=TRUE','H15D_FILTER_REMAINS_IN_COMPOUND_UPPER_FILTERS','DriverVersion -ceq $before.DriverVersion','DriverProvider -ceq $before.DriverProvider','HardwareIds|Where-Object {$_ -ceq $ExactHwid}','CERT_PUBLISHER_REMAINS','CERT_ROOT_REMAINS','WRITE_RESTORE_COMPLETED','BASELINE_RESTORED','TRUST_RESTORED','PciConfigWrite=''ONLY_0x44_BIT2_AND_0x48_BIT1_WITH_EXACT_RESTORE''','Mmio=''NO''','DspBoot=''NO''')){if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15D_RUNNER_MISSING: $x"}}
foreach($x in @('bcdedit','/reboot','/disable-device','/enable-device','Set-ItemProperty','New-ItemProperty','Remove-ItemProperty')){if($run.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "H15D_RUNNER_FORBIDDEN: $x"}}
if($wf -notmatch '(?m)^\s*workflow_dispatch:\s*$' -or $wf -match '(?m)^\s*(push|pull_request):\s*$'){throw 'H15D_WORKFLOW_NOT_MANUAL_ONLY'}
foreach($x in @('-KeyExportPolicy NonExportable','Copy-Item -LiteralPath .\m062\h15d_live\Run-H15dLiveR1.ps1 -Destination $package','PHASER360_H15D_LIVE_R1_BOUNDED_PCI_TRANSACTION_PACKAGE',"PciConfigWrite='ONLY_0x44_BIT2_AND_0x48_BIT1_WITH_EXACT_RESTORE'",'PrivateKeyExported=$false')){if($wf.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "H15D_WORKFLOW_MISSING: $x"}}
Write-Host 'H15D_LIVE_R1_STATIC_TESTS=PASS; expected=0x44_00000010+0x48_807B0DFF; applied=0x44_00000014+0x48_807B0DFD; one_shot=YES; exact_restore=REQUIRED; mmio=NO; dma=NO; dsp_boot=NO; playback=NO'
