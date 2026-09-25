# M0.6.10 guarded rebinding for a subsequent D0 session

The M069 coordinator can now switch to a freshly constructed GlkBoot owner after
confirmed shutdown. The existing KMDF interrupt and wait lock are retained.
This implements a reusable component boundary, not complete Windows sleep/resume
or a PnP driver. Hardware power restoration and resource ownership remain external.

## Primary sources reviewed before implementation

- [Microsoft: D0Entry](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_d0_entry)
- [Microsoft: D0Exit](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_d0_exit)
- [Microsoft: interrupt Enable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfinterrupt/nc-wdfinterrupt-evt_wdf_interrupt_enable)
- [Microsoft: interrupt Disable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfinterrupt/nc-wdfinterrupt-evt_wdf_interrupt_disable)
- [Microsoft: surprise removal](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_surprise_removal)
- [Microsoft: InterlockedExchange](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-interlockedexchange)
- [Microsoft: InterlockedCompareExchange](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-interlockedcompareexchange)

D0Entry can occur repeatedly; interrupt Enable is called each time. D0Exit
normally follows interrupt disable, while surprise removal is a distinct path.
Therefore this change reuses the framework object only within the same prepared
resource lifetime and does not claim support for resource rebalance or removal.

## NextD0 contract

The PnP owner still serializes lifecycle calls against framework Enable/Disable,
stops client admission before shutdown, and maintains nonpaged owner lifetimes.
The bridge and its pending-work variable must be in ordinary cached, nonpaged
memory; Interlocked operations are not used on device MMIO/noncached mappings.

After successful BeforeInterruptsDisabled, framework Disable and completion of
old queued work, NextD0 accepts a different, fresh GlkBoot object and the DSP
mapping for the next session. The old GlkBoot can then be destroyed; the bridge
and ColdPower remain alive. Do not use a raw-memory copy/reset of an owner with
DMA allocations. The replacement owner remains alive until the next confirmed
shutdown and successful replacement, or full framework teardown.

NextD0 checks Closed state, distinct/fresh boot owner, valid mapping shape,
observed framework Disable, and absence of old pending work under the PASSIVE
wait lock. It performs no MMIO, allocation, interrupt synchronization or forced
queue drain. Failure leaves the binding and release gate unchanged. Success
resets transfer evidence and returns the coordinator to Fresh; it does not boot
firmware. Enter must then run in actual D0, followed by framework Enable and
AfterInterruptsEnabled. Firmware authentication and platform setup remain caller
requirements on every entry.

An ISR sets an interlocked pending flag before queuing work. The worker clears
that flag only after taking the same PASSIVE wait lock used by rebinding. Thus
rebinding cannot overlap an executing old worker or proceed with a recorded
queued worker. A stopped worker returns without boot/MMIO access. The flag is
cleared at worker entry, not exit, so a later ISR queued during rearming cannot
have its pending indication erased by the earlier worker's completion.

This is a conservative gate: if a queued callback has not executed, NextD0
returns false. It does not promise that a worker will run before a particular
power callback. The future PnP owner still needs a documented drain/failure
policy; it must not clear this flag manually, force reuse, or call Enter after
a refused NextD0. An uncertain/coalesced queue outcome may also keep reuse
blocked until a worker runs. This is not a complete liveness guarantee.

## Tests and limits

Production wrappers are exercised through simulated WDF/MMIO. Two-session tests
cover fresh firmware boot and notification delivery after rebinding, rejection
while active/before Disable/with old queued work, invalid mapping and IRQL,
rejection of the old single-attempt owner, and cleanup after missing FW_READY
on the second entry. The shim rejects synchronization while disconnected and
MMIO while mappings are marked unavailable. Local strict GCC ASan/UBSan passes
197,110 modeled assertions; local LeakSanitizer is disabled for ptrace.
The shim's Interlocked functions model deterministic callback order, not real
multicore atomic stress. Real intrinsics are compiled by WDK.

Full callback registration, failed interrupt-enable teardown, surprise removal,
resource rebalance, hibernation policy and system-level cleanup failure recovery
remain outstanding. There is no installer, hardware test or audio output.

## Verified CI evidence (2026-09-19)

Implementation: `5b317ad036b3c4d3e3a2bc5f1d617b7a3a915e3b`.
Tree: `77996b871ad34e3db823cdddc1a33eb558c77cc8`.

- [WDK run 35466211095](https://github.com/onish12/360/actions/runs/35466211095),
  job `105958893067`: real WDK compilation and seven host tests pass.
- [Offline run 35466211121](https://github.com/onish12/360/actions/runs/35466211121):
  Windows job `105958893100` passes 11 tests; Linux job `105958893036`
  passes 10 tests with ASan/UBSan enabled by the workflow.
- All three logs report `SOF_GLK_BOOT_TESTS=197110 PASS; windows_api=SIMULATED;
  hardware=NONE`. Assertion counts are not independent device tests.
- [PHASER360_M0610_WDK_D0_REBIND](https://github.com/onish12/360/actions/runs/35466211095/artifacts/10591466773):
  artifact ID `10591466773`, 191,478 bytes. GitHub reports SHA-256
  `9f44ce467f24e5ae7dbff10a95248ac87a4bb20c63dfb6ba8808447bb4a79c60`.
  This digest is service metadata, not an independently downloaded archive hash.

The subsequent documentation-only commit adds this evidence and README status.
No hardware execution or working audio is claimed.
