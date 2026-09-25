# M0.6.15A terminal hardware-access fence

This submilestone adds a software access fence before composing the full KMDF
PnP/power owner. It is intentionally not an installable driver and does not
configure DA7219, MAX98357A, SSP/PDM audio streams, WaveRT/ACX, volume, gain or
speaker enable. It must not be used to produce sound.

## Safety objective

The immediate invariant is narrow and testable: once surprise removal is
observed, no new HDA/DSP register read/write and no new
WdfInterruptSynchronize call may begin through the existing boot, HDA transport,
IPC interrupt, command, notification or shutdown paths.

HardwareAccessGate lives in ordinary cached nonpaged device-context memory. Its
state changes use Interlocked operations; no Interlocked operation is performed
on MMIO/noncached mappings. States are Closed, Open and terminal Removed.
PrepareHardware will eventually own Closed -> Open. ReleaseHardware may perform
Open -> Closed only for a normal resource lifetime. Removed never reopens.

GlkBoot and HdaTransport now require the shared gate before any MMIO. The IRQ
bridge checks the same boot-owned gate before register access or interrupt
synchronization, and Pop also refuses retained notification delivery after the
gate is closed. ColdPower binds each fresh GlkBoot to the same gate and refuses
active power operations when access is not allowed.

After the gate becomes Removed, FenceForSurpriseRemoval closes only the IRQ
bridge's PASSIVE software admission under its wait lock, without MMIO, interrupt
synchronization, clearing the queued-work record, or claiming a successful
hardware mask. It deliberately does not modify fields owned by the interrupt
lock because EvtDeviceSurpriseRemoval is not synchronized with other PnP/power
callbacks. The normal KMDF surprise-removal sequence can subsequently reach
EvtDeviceD0ExitPreInterruptsDisabled and EvtInterruptDisable. Only after the
framework Disable/disconnect evidence exists may DrainStopped flush the queued
DPC/work item; the drain still does not authorize DSP/DMA release.

## What this does not solve

Closing the software gate is not proof that hardware DMA stopped, that an
interrupt source was masked, or that the framework can safely delete every
device-parented WDF object. Surprise removal can occur outside the orderly
shutdown sequence. A later composed owner must still define parent lifetime,
resource callback ordering, boot-DMA retention during a concurrent D0Entry
failure/removal, and the exact ReleaseHardware policy.

Therefore a failed cleanup after Removed remains a failure. The new gate never
turns inability to touch removed hardware into a false successful shutdown.
No physical Lenovo execution is authorized by this change.

## Primary references reviewed

- Microsoft, Surprise-Removal Sequence.
- Microsoft, WDM IRPs and KMDF event callback ordering: the surprise-removal
  sequence includes EvtDeviceSurpriseRemoval, then the power-down path including
  EvtDeviceD0ExitPreInterruptsDisabled, EvtInterruptDisable, D0Exit and
  ReleaseHardware.
- Microsoft, EvtDevicePrepareHardware.
- Microsoft, Introduction to Hardware Resources.
- Microsoft, WdfDpcCancel and WdfWorkItemFlush.
- Microsoft, WdfObjectDelete and framework object lifetime.
- Microsoft, InterlockedCompareExchange.

## Verification added

The integrated Windows-wrapper model boots to command-ready, enables/arms the
IRQ bridge, queues a notification, then marks the shared gate Removed while
work is pending. It also injects an ISR after the terminal gate is closed and
requires that the ISR perform no fake MMIO. From that point the test forbids
every fake MMIO access and
records WdfInterruptSynchronize calls. Running, Arm, Pop, Command, active
shutdown, the explicit software-only surprise-removal fence, queued DPC/work
completion, framework Disable, drain and direct GlkBoot shutdown are exercised.
The test also requires drain to remain refused before framework Disable, then
succeed as a software drain afterward. The invariant requires zero new MMIO
writes and zero new interrupt-synchronization calls after removal.

This deterministic host model is not proof of real KMDF concurrency, physical
DMA quiescence or hardware behavior. WDK compilation and Windows/Linux
sanitizer/regression results must pass before M0.6.15A is considered complete.


## Verified CI evidence (2026-09-20)

Final implementation commit: `1c028cdaed3bbbd74ace1399a19d209ddcc85dbd`.
Tree: `bcd2e839184241651a9445c846c39914b2b2e877`.

- WDK/KMDF run `35536086135`, job `106145256500`: real WDK compilation
  passes and all 10 selected host tests pass. The integrated production-wrapper
  model reports `SOF_GLK_BOOT_TESTS=265876 PASS`.
- Windows/Linux run `35536086156`: Windows job `106145256686` passes all
  14 CTest tests; Linux job `106145256749` passes all 12 tests with the
  workflow's ASan/UBSan instrumentation. Both report 265,876 integrated
  assertions. These are modeled assertions, not independent hardware trials.
- The Windows fixture step reports `SOF_CNG_PIN_TESTS=10 PASS` with real Windows
  CNG and the official hash-pinned fixture, plus
  `SOF_PINNED_REFERENCE_TESTS=13 PASS` with real CNG and simulated boot.
- Development artifact `PHASER360_M0615A_WDK_ACCESS_FENCE`: ID
  `10613236668`, 260,454 bytes. GitHub reports archive SHA-256
  `3817ccc23e0f26d947de0dc17b90ae8769d9d6e2d0083f1f3a2e56c6bcac1104`.
  This is service-reported archive metadata, not an independently downloaded
  and recomputed digest.

The first implementation attempt exposed a regression in the malformed-XMan
test because the new gate intentionally rejected an unbound boot owner before
the old test reached IPC validation. That test was corrected to establish its
intended precondition. A second audit then removed unsynchronized IRQ-state
writes from the surprise-removal fence and added an ISR-after-removal regression.
The final evidence above is for the corrected implementation only.

No Lenovo execution, installable driver, codec/amplifier programming or playback
is claimed by M0.6.15A.
