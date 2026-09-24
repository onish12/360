$ErrorActionPreference='Stop';Set-StrictMode -Version 2
$root=Join-Path $PSScriptRoot '..'
$r=Get-Content (Join-Path $root 'm062\m1_fast\Run-M1FastSafe.ps1') -Raw
$c=Get-Content (Join-Path $root 'm062\m1_fast\RUN_M1_FAST_SAFE.cmd') -Raw
$w=Get-Content (Join-Path $root '.github\workflows\m1-fast-safe-package.yml') -Raw
$d=Get-Content (Join-Path $root 'm062\driver\boot_dma.cpp') -Raw
$l=Get-Content (Join-Path $root 'm062\driver\repeated_device_lifecycle.cpp') -Raw
$h=Get-Content (Join-Path $root 'm062\driver\hda_transport.cpp') -Raw
foreach($x in @(
 'm1-fast-safe-20260924-r3-dma-lifetime','EXACT_WINDOWS_BUILD_19044_REQUIRED','PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06',
 "BaselineInf='oem14.inf'","BaselineVersion='9.22.0.4832'",'/export-driver',
 'M1_FAST_RECOVERY_POINTER.txt','/add-driver','$bindAttempted=$true','ForceUpdate($ExactHwid,$pkg.Inf)',
 '[Phaser360.M1FastNative]::Query($TelemetryGuid,$TelemetryIoctl,32)','WaitTelemetry([int]$seconds=15)','telemetry_interface_wait.json','Start-Sleep -Milliseconds 500',
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
foreach($x in @('-NonInteractive','Run-M1FastSafe.ps1','m1-fast-safe-20260924-r3-dma-lifetime')){if($c.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "M1_FAST_CMD_MISSING:$x"}}
foreach($x in @('verify-sof-reference.py','generate-embedded-firmware.py','phaser360_m1_boot.vcxproj','Inf2Cat.exe','New-SelfSignedCertificate','KeyExportPolicy NonExportable','phaser360_m1_fast_safe.cer','package_manifest.json','upload-artifact')){if($w.IndexOf($x,[StringComparison]::OrdinalIgnoreCase)-lt0){throw "M1_FAST_WORKFLOW_MISSING:$x"}}
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
foreach($x in @('phaser360_dma_owner_tests','phaser360_glk_boot_tests','M1_R3_HOST_TESTS_FAILED')){
 if($w.IndexOf($x,[StringComparison]::Ordinal)-lt0){throw "M1_R3_WORKFLOW_GUARD_MISSING:$x"}
}

Write-Host 'M1_FAST_SAFE_STATIC_TESTS=PASS; exact_target=YES; baseline_backup_before_bind=YES; recovery_pointer_before_bind=YES; telemetry=DOUBLE_READ; dma_lifetime=PREPARE_TO_RELEASE; rollback=AUTOMATIC; playback=NO; reboot=NO'
