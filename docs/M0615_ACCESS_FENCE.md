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
- Microsoft, WDM IRPs and KMDF event callback ordering.
- Microsoft, EvtDevicePrepareHardware.
- Microsoft, Introduction to Hardware Resources.
- Microsoft, WdfDpcCancel and WdfWorkItemFlush.
- Microsoft, WdfObjectDelete and framework object lifetime.
- Microsoft, InterlockedCompareExchange.

## Verification added

The integrated Windows-wrapper model boots to command-ready, enables/arms the
IRQ bridge, queues a notification, then marks the shared gate Removed while
work is pending. From that point the test forbids every fake MMIO access and
records WdfInterruptSynchronize calls. Running, Arm, Pop, Command, active
shutdown, queued DPC/work completion, framework Disable, drain and direct
GlkBoot shutdown are exercised. The invariant requires zero new MMIO writes and
zero new interrupt-synchronization calls after removal.

This deterministic host model is not proof of real KMDF concurrency, physical
DMA quiescence or hardware behavior. WDK compilation and Windows/Linux
sanitizer/regression results must pass before M0.6.15A is considered complete.
