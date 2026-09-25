# M0.6.9 cold-start power coordination

ColdPower connects the existing GlkBoot and IpcInterrupt owners for one cold
D0 session. It is compiled into the WDK static library. It does not register
PnP callbacks, map resources, authenticate firmware or create a device/installer.
It is a prerequisite for the full PnP owner, not completion of that owner.

## Primary sources checked before implementation

- [Microsoft: PrepareHardware](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_prepare_hardware)
- [Microsoft: ReleaseHardware](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_release_hardware)
- [Microsoft: D0Entry and failure handling](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_d0_entry)
- [Microsoft: D0EntryPostInterruptsEnabled](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_d0_entry_post_interrupts_enabled)
- [Microsoft: D0ExitPreInterruptsDisabled](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_d0_exit_pre_interrupts_disabled)

D0Entry precedes interrupt enable. On a failed D0Entry, KMDF does not call
D0Exit. ReleaseHardware occurs after power-down/disconnection and is not the
place to attempt live DSP or DMA cleanup. PrepareHardware can also fail and
still receive ReleaseHardware. These facts require distinct early and active
cleanup paths; merely calling the M068 Stop method everywhere is insufficient.

## Contract and implemented sequence

The future device owner constructs GlkBoot, IpcInterrupt and ColdPower in
nonpaged storage and keeps them alive through framework object destruction.
It creates IpcInterrupt in PrepareHardware using assigned resources and the
same DSP mapping it later supplies to Enter. It authenticates and binds the
payload/XMan bytes from one image and retains them throughout Enter.

All ColdPower methods and state queries require caller serialization; operational
methods require PASSIVE_LEVEL. The caller prevents concurrent framework Enable
while using the before-enable methods. No client may bypass the bridge to access
GlkBoot. Before exiting, the caller stops admission of client commands and Pops.

- Enter is for D0Entry before the first framework Enable callback. It checks
  the bridge's startup gate, prepares and transfers firmware, and requires every
  success flag including DMA release, FW_READY and command readiness.
- Failure performs immediate cleanup before returning the original Prepare
  failure or a transfer configuration error. CancelBeforeEnable closes software
  access without MMIO or WdfInterruptSynchronize; no interrupt has been enabled.
  GlkBoot.Shutdown then stops DMA and powers down DSP. Failed shutdown leaves
  CanReleaseMappings false and preserves the underlying DMA retention contract.
  RetryEarlyCleanup permits an explicit retry only while still in D0 before
  any framework Enable. There is no automatic retry loop.
- AfterInterruptsEnabled is for the post-enable callback. It arms notification
  delivery only after successful boot. An arm failure immediately attempts the
  active shutdown sequence and still returns failure.
- BeforeInterruptsDisabled first closes/masks the bridge, then shuts down the
  boot owner. A failed bridge stop prevents DSP shutdown and mapping release.
  A later explicit call can retry only while interrupts remain connected and
  the device remains accessible in D0.
- CanReleaseMappings becomes true only after confirmed cleanup. It does not
  authorize deleting the owners: queued WDF callbacks still require their
  device-context lifetime. The future ReleaseHardware callback may unmap only
  after that gate and appropriate framework ordering, without new MMIO here.

M068 Stop now returns success without interrupt synchronization when already
closed. This also permits a queued work item or repeated stop after successful
shutdown/disconnection without accessing released mappings. CancelBeforeEnable
refuses a bridge whose Enable callback has ever run, including failed Enable.
Its software cancellation alone is NOT proof that DSP/DMA have stopped.

## Limits before driver integration

This helper is deliberately one-session: a second Enter is rejected. It does
not implement suspend/resume, resource rebalance, surprise removal, failed
framework interrupt-enable teardown, or forced power-down after failed cleanup.
A false cleanup result cannot prevent Windows from continuing removal/power
transitions. Retaining a pointer is not a complete system-level recovery policy.
Those paths, callback registration, BAR/vector selection, parent DMA lifetime,
firmware trust and platform routing must be completed before installing a driver.

No hardware audit, Intel driver modification or security-setting change is
required for this compilation. Codec/topology setup, audio stream DMA and WaveRT
remain absent. The existing read-only probe is unchanged.

## Verification scope

The test shim now rejects WdfInterruptSynchronize when the interrupt is not
connected. Tests execute production wrappers for normal boot/IRQ/shutdown,
early HDA failure, missing FW_READY, retained DMA after stuck RUN, failed IRQ
unmask, failed IRQ mask with explicit recovery, queued work after disconnection,
and rejected second boot. They check that early cleanup never synchronizes an
unconnected interrupt and that confirmed cleanup permits no later MMIO.

Local strict GCC ASan/UBSan run: 154,185 modeled assertions passed. LeakSanitizer
is disabled locally because the execution environment uses ptrace; CI provides
separate sanitizer verification. This is simulated WDF/MMIO, not a Lenovo test.

## Verified CI evidence (2026-09-19)

Implementation commit: `9a89b71148d9abd75d60538d6678f97f968a9b94`.
Tree: `df52db05a89afc90f873d1abb992f0299907f5b6`.

- [WDK run 35465782981](https://github.com/onish12/360/actions/runs/35465782981),
  job `105957731713`: real WDK library compilation and all seven host tests pass.
- [Offline run 35465782950](https://github.com/onish12/360/actions/runs/35465782950):
  Windows job `105957731601` passes all 11 tests; Linux job `105957731494`
  passes all 10 tests with the workflow's ASan/UBSan instrumentation.
- All three logs report `SOF_GLK_BOOT_TESTS=154185 PASS; windows_api=SIMULATED;
  hardware=NONE`. These are modeled assertions, not independent hardware trials.
- [PHASER360_M069_WDK_COLD_POWER](https://github.com/onish12/360/actions/runs/35465782981/artifacts/10591490982):
  artifact `10591490982`, 186,851 bytes. GitHub reports SHA-256
  `20b7b4737d17e73a0869f94d616b1d426c0d9082eb84a5e1f38fb351dd6be565`.
  This is service-reported metadata, not an independently downloaded archive hash.

This evidence/README update follows the tested implementation as a documentation
change. No Lenovo execution, installable driver or audio playback is claimed.
