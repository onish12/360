# M0.6.11 explicit deferred-work drain

The IRQ bridge now owns a WDFDPC and WDFWORKITEM so teardown can wait for deferred
callbacks using documented KMDF APIs. ColdPower performs Stop, DrainStopped,
then GlkBoot.Shutdown. This closes the M0610 queue-drain policy gap for orderly
shutdown with an accessible device. It is still a development static library.

## Primary sources checked before implementation

- [Microsoft: WdfDpcCreate](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdpc/nf-wdfdpc-wdfdpccreate)
- [Microsoft: WdfDpcEnqueue](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdpc/nf-wdfdpc-wdfdpcenqueue)
- [Microsoft: WdfDpcCancel](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdpc/nf-wdfdpc-wdfdpccancel)
- [Microsoft: WdfWorkItemCreate](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfworkitem/nf-wdfworkitem-wdfworkitemcreate)
- [Microsoft: WdfWorkItemEnqueue](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfworkitem/nf-wdfworkitem-wdfworkitemenqueue)
- [Microsoft: WdfWorkItemFlush](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfworkitem/nf-wdfworkitem-wdfworkitemflush)

WdfDpcEnqueue is available at any IRQL. WorkItemEnqueue requires at most DISPATCH.
Cancel(TRUE) waits for a DPC to be canceled or finish; its Boolean return says
whether it was queued, not whether the wait succeeded. WorkItemFlush waits for
queued/running work and must not be called by that work item's own thread.

## Callback and ownership changes

Create allocates a device-parented wait lock, work item, DPC, then interrupt.
Every callback object has an owner context. DPC/work item AutomaticSerialization
is FALSE; explicit wait-lock/interrupt synchronization remains in use. All
allocation failures unwind created objects before returning, with no MMIO.

The DIRQL ISR still checks ownership and masks the IPC source before queuing.
It now calls WdfDpcEnqueue. The DISPATCH_LEVEL DPC only enqueues the work item:
no MMIO, transport operation, allocation or blocking wait. The PASSIVE worker
keeps the existing serialized notification polling and rearm/fault behavior.
EvtInterruptWorkItem is no longer registered. No undocumented interrupt flush
method, global DPC flush or manually invoked callback is used in production.

## DrainStopped contract

Call from the serialized PASSIVE PnP path after successful Stop, never from the
worker or code invoked by that worker. Stop synchronizes with the ISR, closes
access and prevents all later DPC enqueues. DrainStopped then:

1. Checks the closed gate while holding the wait lock, then releases the lock.
2. Calls WdfDpcCancel(TRUE), which cancels queued execution or waits for an
   executing DPC, including any work-item enqueue it performs.
3. Calls WdfWorkItemFlush after the DPC producer has finished.
4. Reacquires the wait lock and records completion/reset of the pending flag.

Neither blocking wait holds the lock required by the worker. A worker flushed
after Stop observes closed and does no MMIO or interrupt synchronization.
DrainStopped itself does no MMIO and can finish after interrupt disconnection,
but ColdPower normally calls it before framework Disable and DSP shutdown.
Callers must not concurrently rebind, enable the interrupt, or destroy owners.

Successful repeated drains are idempotent. A refused drain does not authorize
release or rebinding. RebindStopped now requires the recorded drain in addition
to framework Disable and closed state, even if a queued callback ran earlier.
ColdPower invokes drain automatically; direct users of the bridge must add it.
CancelBeforeEnable records a trivially drained state because no ISR could have
queued a DPC in that lifecycle phase.

There is no custom wall-clock timeout around the framework waits. This is the
KMDF completion contract, not a guarantee of recovery from a hung kernel/worker.
After a failed hardware mask, Stop remains unconfirmed and DrainStopped refuses;
forced power-down and surprise-removal policy remain outside this implementation.

## Validation scope

The host model checks IRQL and that cancel/flush run without the wait/IRQ lock.
It tests a queued DPC, a DPC that already queued work, and a modeled executing
DPC finishing during Cancel(TRUE) with a FALSE return. It also tests failure at
all four object-creation stages, drain refusal before confirmed stop, repeated
drain, no MMIO during drain with unavailable mappings, and two-session reuse.

Local strict GCC ASan/UBSan run: 199,647 assertions, simulated WDF/MMIO only.
LeakSanitizer is disabled locally because of ptrace. The fake scheduler models
selected interleavings; this is not real multicore stress or Driver Verifier.
Full PnP registration, failed Enable teardown, surprise removal, firmware trust,
platform routing, codec/topology integration and WaveRT remain outstanding.
No installable audio driver or Lenovo execution is claimed.

## Verified CI evidence (2026-09-19)

Implementation commit: `526c44ed860465f419886cf2d9f923e44f64bb96`.
Tree: `9820f70a12689f5c2fded61e61c27d13afbcc54b`.

- [WDK run 35466674958](https://github.com/onish12/360/actions/runs/35466674958),
  job `105960163634`: real WDK library build and all seven host tests pass.
- [Offline run 35466674946](https://github.com/onish12/360/actions/runs/35466674946):
  Windows job `105960163560` passes 11 tests; Linux job `105960163617`
  passes 10 tests with the workflow's ASan/UBSan instrumentation.
- All three logs report `SOF_GLK_BOOT_TESTS=199647 PASS; windows_api=SIMULATED;
  hardware=NONE`. These are modeled assertions, not device trials.
- [PHASER360_M0611_WDK_DEFERRED_DRAIN](https://github.com/onish12/360/actions/runs/35466674958/artifacts/10591706969):
  artifact `10591706969`, 197,045 bytes. GitHub reports SHA-256
  `84103b1a3cf526bb2b190d62eed11f9e9f4665c6745d4db9247eb9a8eab98a28`.
  This digest is artifact-service metadata, not an independently downloaded hash.

The next documentation-only commit records this evidence and README status.
No hardware execution, installable driver or audio playback is claimed.
