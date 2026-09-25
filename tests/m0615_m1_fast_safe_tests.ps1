$ErrorActionPreference='Stop';Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$r=Get-Content (Join-Path $root 'm062\m1_fast\Run-M1FastSafe.ps1') -Raw
$c=Get-Content (Join-Path $root 'm062\m1_fast\RUN_M1_FAST_SAFE.cmd') -Raw
$w=Get-Content (Join-Path $root '.github\workflows\m1-fast-safe-package.yml') -Raw
$d=Get-Content (Join-Path $root 'm062\driver\boot_dma.cpp') -Raw
$l=Get-Content (Join-Path $root 'm062\driver\repeated_device_lifecycle.cpp') -Raw
$h=Get-Content (Join-Path $root 'm062\driver\hda_transport.cpp') -Raw
$g=Get-Content (Join-Path $root 'm062\driver\glk_boot.cpp') -Raw
$i=Get-Content (Join-Path $root 'm062\driver\ipc_interrupt.cpp') -Raw
$s=Get-Content (Join-Path $root 'm062\driver\stage_trace.cpp') -Raw
$e=Get-Content (Join-Path $root 'm062\driver\driver_entry.cpp') -Raw
$hc=Get-Content (Join-Path $root 'src\sof\hda_controller.cpp') -Raw
$hs=Get-Content (Join-Path $root 'src\sof\hda_stream.cpp') -Raw
$p=Get-Content (Join-Path $root 'm062\driver\pci_config_boot_policy.cpp') -Raw
$st=Get-Content (Join-Path $root 'tests\sof_stream_tests.cpp') -Raw
$gt=Get-Content (Join-Path $root 'tests\sof_glk_boot_tests.cpp') -Raw
foreach($x in @(
 'm1-fast-safe-20260925-r6-pci-live-contract','EXACT_WINDOWS_BUILD_19044_REQUIRED','PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06',
 "BaselineInf='oem14.inf'","BaselineVersion='9.22.0.4832'",'/export-driver',
 'M1_FAST_RECOVERY_POINTER.txt','/add-driver','$bindAttempted=$true','ForceUpdate($ExactHwid,$pkg.Inf)',
 '[Phaser360.M1FastNative]::Query($TelemetryGuid,$TelemetryIoctl,32)','WaitTelemetry([string]$instance,[string]$inf,[int]$seconds=15)','telemetry_interface_wait.json','Start-Sleep -Milliseconds 500',
 'ebaed0db-f9db-42ea-a162-5f4111384051','StartStageTrace','StopStageTrace','M1_R6_STAGE_TRACE.etl','M1_TARGET_FAILED_START_CODE10','DEVPKEY_Device_ProblemStatus',
 '/delete-driver',$null,'BASELINE_RESTORED','TRUST_RETAINED_FOR_SAFETY=TRUE','DO_NOT_REBOOT_UNTIL_TARGET_STATE_IS_REVIEWED=TRUE',
 "AudioPlayback='NO'","CodecProgramming='NO'","SpeakerEnable='NO'","AutomaticReboot='NO'","BcdWrite='NO'"
)){
 if($null-ne$x -and $r.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "M1_FAST_RUNNER_GUARD_MISSING:$x"}
}
if($r.IndexOf('/export-driver',[StringComparison]::OrdinalIgnoreCase)-gt$r.IndexOf('ForceUpdate($ExactHwid,$pkg.Inf)',[StringComparison]::OrdinalIgnoreCase)){throw 'M1_FAST_BASELINE_EXPORT_MUST_PRECEDE_BIND'}
if($r.IndexOf('M1_FAST_RECOVERY_POINTER.txt',[StringComparison]::OrdinalIgnoreCase)-gt$r.IndexOf('ForceUpdate($ExactHwid,$pkg.Inf)',[StringComparison]::OrdinalIgnoreCase)){throw 'M1_FAST_RECOVERY_POINTER_MUST_PRECEDE_BIND'}
if($r.IndexOf('Join-Path (Join-Path $env:SystemRoot ''INF'') $BaselineInf',[StringComparison]::OrdinalIgnoreCase)-ge0){throw 'M1_FAST_SYSTEM_INF_FORCEUPDATE_FORBIDDEN'}
if($r.IndexOf('ForceUpdate($ExactHwid,$baselineExportInf)',[StringComparison]::Ordinal)-lt0){throw 'M1_FAST_EXPORTED_BASELINE_FALLBACK_MISSING'}
foreach($bad in @('bcdedit','Restart-Computer','shutdown.exe','AUDIO_PLAYBACK=YES','CODEC_PROGRAMMING=YES','SPEAKER_ENABLE=YES')){
 if($r.IndexOf($bad,[StringComparison]::OrdinalIgnoreCase)-ge0){throw "M1_FAST_FORBIDDEN:$bad"}
}
foreach($x in @('-NonInteractive','Run-M1FastSafe.ps1','m1-fast-safe-20260925-r6-pci-live-contract')){if($c.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "M1_FAST_CMD_MISSING:$x"}}
foreach($x in @('verify-sof-reference.py','generate-embedded-firmware.py','phaser360_m1_boot.vcxproj','Inf2Cat.exe','New-SelfSignedCertificate','KeyExportPolicy NonExportable','phaser360_m1_fast_safe.cer','package_manifest.json','upload-artifact','M1_FAST_SAFE_R6_PCI_LIVE_CONTRACT_DSP_BOOT','PHASER360_M1_R6_PCI_LIVE_CONTRACT','0.6.15.135')){if($w.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "M1_FAST_WORKFLOW_MISSING:$x"}}
# R3: DMA framework objects belong to the PrepareHardware/ReleaseHardware
# lifetime. D0 may stage/publish the preallocated buffers, but must not create
# the DMA enabler or common buffers.
foreach($x in @(
 'BootDma::PrepareHardware','WdfDeviceSetAlignmentRequirement',
 'WdfDmaEnablerCreate','WdfCommonBufferCreateWithConfig',
 'BootDma::Stage','BootDma::ReleaseSession','BootDma::ReleaseHardware',
 'BootDma::AbandonForRemoval'
)){
 if($d.IndexOf($x,[StringComparison]::Ordinal)-lt0){throw "M1_R3_DMA_GUARD_MISSING:$x"}
}
foreach($x in @(
 'dma_.PrepareHardware(device_,kPinnedPayloadBytes)',
 'sessions_.Begin(device,irq_,gate_,dma_)',
 'dma_.ReleaseHardware()','dma_.AbandonForRemoval()'
)){
 if($l.IndexOf($x,[StringComparison]::Ordinal)-lt0){throw "M1_R3_LIFETIME_GUARD_MISSING:$x"}
}
foreach($bad in @('WdfDmaEnablerCreate','WdfCommonBufferCreateWithConfig','WdfDeviceSetAlignmentRequirement')){
 if($h.IndexOf($bad,[StringComparison]::Ordinal)-ge0){throw "M1_R3_D0_DMA_CREATION_FORBIDDEN:$bad"}
}
foreach($x in @('CM_Get_Device_Interface_List_SizeW','CM_Get_Device_Interface_ListW','CFGMGR32','M1_TARGET_LOST_DURING_TELEMETRY_WAIT','Source=$wq.Source')){
 if($r.IndexOf($x,[StringComparison]::Ordinal)-lt0){throw "M1_R31_TELEMETRY_FALLBACK_GUARD_MISSING:$x"}
}
foreach($x in @('phaser360_dma_owner_tests','phaser360_glk_boot_tests','phaser360_stream_tests','M1_R6_HOST_TESTS_FAILED')){
 if($w.IndexOf($x,[StringComparison]::Ordinal)-lt0){throw "M1_R6_WORKFLOW_GUARD_MISSING:$x"}
}
foreach($bad in @('M1_BIND_REQUIRES_REBOOT','INTEL_FALLBACK_REQUIRES_REBOOT')){
 if($r.IndexOf($bad,[StringComparison]::Ordinal)-ge0){throw "M1_R6_REBOOT_EARLY_ABORT_FORBIDDEN:$bad"}
}
foreach($x in @('$bindRebootSignalled','bind_update_result.json','BindRebootSignalled=$bindRebootSignalled','$fallbackRebootSignalled','intel_fallback_update_result.json','IntelFallbackRebootSignalled=$fallbackRebootSignalled')){
 if($r.IndexOf($x,[StringComparison]::Ordinal)-lt0){throw "M1_R6_REBOOT_EVIDENCE_MISSING:$x"}
}
foreach($src in @($hc,$hs)){
 if($src.IndexOf('next>=0x1000',[StringComparison]::Ordinal)-ge0 -or $src.IndexOf('0x1000-', [StringComparison]::Ordinal)-ge0){throw 'M1_R6_LEGACY_CAP_WINDOW_PRESENT'}
}
foreach($x in @('0x0c00','0x0800','0x0500','0x1f00','0x0700')){
 if($st.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "M1_R6_PHYSICAL_CAP_CHAIN_TEST_MISSING:$x"}
}

foreach($x in @(
 'IdentityMatches(liveConfig,evidence)',
 'StableHeaderMatches(liveConfig,evidence)',
 'CapabilityStructureMatches(liveConfig,evidence)',
 'OwnedDwordsMatch(liveConfig,evidence)',
 'PciConfigBootFailure::IdentityDrift',
 'PciConfigBootFailure::CapabilityStructureDrift',
 'PciConfigBootFailure::OwnedDwordDrift'
)){
 if($p.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "M1_R6_PCI_LIVE_CONTRACT_GUARD_MISSING:$x"}
}
foreach($x in @(
 'H80_PCI_POLICY_ENTER','TracePciPolicyFailure',
 'H81_PCI_POLICY_STATE_FAIL','H86_PCI_IDENTITY_DRIFT',
 'H88_PCI_CAP_STRUCTURE_DRIFT','H89_PCI_OWNED_DWORD_DRIFT',
 'H8B_CGCTL_WRITE_FAIL','H8D_CGCTL_VERIFY_FAIL',
 'H8E_PGCTL_WRITE_FAIL','H8G_PGCTL_VERIFY_FAIL'
)){
 if($h.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "M1_R6_PCI_TRACE_GUARD_MISSING:$x"}
}
if($p.IndexOf('RtlCompareMemory(liveConfig,evidence.config,kPciConfigSnapshotBytes)',[StringComparison]::OrdinalIgnoreCase)-ge0){
 throw 'M1_R6_FULL_256_BYTE_LIVE_FREEZE_FORBIDDEN'
}
foreach($x in @('pciConfig[0x52]^=1u','pciConfig[0x51]=0','Owned dword changing')){
 if($gt.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "M1_R6_PCI_REGRESSION_MISSING:$x"}
}
foreach($x in @('EtwRegister','EtwWriteString','EtwUnregister','PHASER360_R6 ')){
 if($s.IndexOf($x,[StringComparison]::Ordinal)-lt0){throw "M1_R6_STAGE_TRACE_PROVIDER_MISSING:$x"}
}
foreach($x in @('StageTraceRegister','Phaser360EvtDriverUnload','A00_DRIVER_ENTRY','A20_DEVICE_OWNER_OK')){
 if($e.IndexOf($x,[StringComparison]::Ordinal)-lt0){throw "M1_R6_DRIVER_ENTRY_TRACE_MISSING:$x"}
}
foreach($x in @('P20_DMA_PREPARE_ENTER','P25_DMA_PREPARE_OK')){
 if($d.IndexOf($x,[StringComparison]::Ordinal)-lt0){throw "M1_R6_DMA_TRACE_MISSING:$x"}
}
foreach($x in @('D20_LIFECYCLE_D0_ENTER','D40_FIRMWARE_COMMAND_READY','D50_FRAMEWORK_ENABLE_GRANT_OK','POST20_OK')){
 if($l.IndexOf($x,[StringComparison]::Ordinal)-lt0){throw "M1_R6_LIFECYCLE_TRACE_MISSING:$x"}
}
foreach($x in @('H10_HDA_PREPARE_ENTER','H90_HDA_PREPARE_OK')){
 if($h.IndexOf($x,[StringComparison]::Ordinal)-lt0){throw "M1_R6_HDA_TRACE_MISSING:$x"}
}
foreach($x in @('G10_GLK_PREPARE_ENTER','T20_HDA_START_OK','T30_ROM_ENTERED_OK','T50_IPC_READY_OK','T60_COMMAND_READY_OK')){
 if($g.IndexOf($x,[StringComparison]::Ordinal)-lt0){throw "M1_R6_GLK_TRACE_MISSING:$x"}
}
foreach($x in @('I10_INTERRUPT_ENABLE_ENTER','I20_MASK_GLOBAL_OK','I30_MASK_LOCAL_OK','I40_INTERRUPT_ENABLE_OK')){
 if($i.IndexOf($x,[StringComparison]::Ordinal)-lt0){throw "M1_R6_IRQ_TRACE_MISSING:$x"}
}

Write-Host 'M1_FAST_SAFE_STATIC_TESTS=PASS; r6_pci_live_contract=YES; stage_trace=ETW_PRE_BIND; exact_target=YES; baseline_backup_before_bind=YES; recovery_pointer_before_bind=YES; telemetry=DOUBLE_READ; dma_lifetime=PREPARE_TO_RELEASE; rollback=AUTOMATIC; playback=NO; reboot=NO'
