# M0.6.14 failed interrupt startup and exit cleanup

This change corrects a failure-path defect in the IRQ bridge. A failed Stop used
to leave its PASSIVE worker admission open because the same `closed_` flag also
represented successful hardware masking. Queued work could then synchronize an
interrupt after framework disconnection. Stop now closes software admission even
when masking fails; hardware completion remains a separate requirement.

A new ColdPower.AfterInterruptsDisconnected entry supports cleanup in D0Exit
while hardware is still present, powered and mapped. It is not registered by the
resource-only PnpResources adapter, and is not callable from ReleaseHardware or
surprise-removal cleanup. There is still no installable boot/audio driver.

## Primary sources reviewed before implementation

- [Microsoft: EvtInterruptEnable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfinterrupt/nc-wdfinterrupt-evt_wdf_interrupt_enable)
- [Microsoft: EvtInterruptDisable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfinterrupt/nc-wdfinterrupt-evt_wdf_interrupt_disable)
- [Microsoft: WdfInterruptSynchronize](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfinterrupt/nf-wdfinterrupt-wdfinterruptsynchronize)
- [Microsoft: WdfInterruptAcquireLock lifecycle restrictions](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfinterrupt/nf-wdfinterrupt-wdfinterruptacquirelock)
- [Microsoft: D0Exit precedes power reduction except surprise removal](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_d0_exit)
- [Microsoft: surprise removal is not synchronized with PnP/power callbacks](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_surprise_removal)
- [Microsoft: orderly and surprise removal](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/a-user-unplugs-a-device)
- [Microsoft: WdfDpcCancel wait semantics](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdpc/nf-wdfdpc-wdfdpccancel)
- [Microsoft: WdfWorkItemFlush](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfworkitem/nf-wdfworkitem-wdfworkitemflush)
- [Microsoft's KMDF interrupt implementation, reviewed revision b6191d9](https://github.com/microsoft/Windows-Driver-Frameworks/blob/b6191d9543441329154da32f7ab9bdd97228dd3c/src/framework/shared/irphandlers/pnp/interruptobject.cpp)

The public implementation sets its enabled flag only after InterruptEnable
succeeds. Disconnect invokes InterruptDisable conditionally on that flag and on
absence of the surprise-removal flag. Therefore a failed Enable does not justify
assuming that the driver's Disable callback ran. The new tests deliberately
model disconnection without invoking Disable for that case. This source review
does not establish which exact KMDF binary runs on the Lenovo.

## Software closure and hardware proof are different

Stop obtains the PASSIVE wait lock first, waiting for any current command/worker.
It then closes admission before attempting the synchronized IRQ stop. That stop
marks the ISR stopped before trying hardware masking. Regardless of mask result,
subsequent Running/Arm/Command/Pop/Work operations do not access the boot owner,
MMIO or IRQ synchronization. The ISR rejects further work after that attempt.
An executing DPC may still enqueue a worker; that worker sees closed admission.

`closed_` continues to require confirmed masking or the existing before-enable
cancellation. `admissionClosed_` only blocks PASSIVE consumers. Draining queues
never substitutes for masking or DSP/DMA shutdown. Normal pre-disable cleanup
retains Stop -> DPC cancel/wait -> work-item flush -> boot Shutdown ordering.
No waits are performed under the worker's wait lock.

## Explicit post-disconnection contract

ColdPower.AfterInterruptsDisconnected is for the serialized PnP D0Exit thread
**after framework interrupt disconnection and before hardware power reduction**.
The caller must also establish that the hardware is present and accessible. A
failed Enable return alone is not a sufficient call site. The component cannot
query/prove that the framework has disconnected: this is a caller obligation.
No register retry is permitted after unmapping or power loss.

- If Disable ran, StopAfterDisconnect adopts only its recorded successful mask
  result. That adoption performs no MMIO or interrupt synchronization. An armed
  session requires a prior pre-disable Stop attempt that closed admission.
- If Enable failed, or the framework never reached Enable, Disable may not have
  run. In that never-armed case, StopAfterDisconnect retries hardware masking
  directly at PASSIVE, after disconnection, with no IRQ synchronization.
- Software admission stays closed even if this mask fails. Queue drain is allowed
  once framework disconnection is established by the caller; rebind and the
  ColdPower mapping-release gate still remain blocked.
- Only confirmed mask plus completed drain allows boot Shutdown. Only successful
  Shutdown marks the ColdPower session closed. A DSP power-down failure retains
  the release gate; a retry is legal only while the same D0 ownership contract
  still holds.

A successfully enabled and armed session that skipped pre-disable Stop is
rejected, rather than treated as recoverable. These methods do not serialize
arbitrary concurrent Disable/SurpriseRemoval against client code. The existing
NextD0 path still requires recorded Disable, drain and confirmed shutdown; this
change does not permit recycling an interrupt after a failed/missing Enable
without Disable. Production callers must use ColdPower's release gate, not
interpret DrainStopped success as permission to free resources.

## Remaining PnP limits

Surprise removal may run concurrently and may omit expected callbacks. It still
requires a separate no-MMIO access fence across boot, IRQ, command and DMA owners,
plus a complete parent-object teardown policy. Returning failure from D0Exit
cannot prevent Windows from powering down or deleting the device. Retaining
pointers alone cannot guarantee DMA buffer lifetime through parent deletion.
Those conditions are not solved or bypassed by this change. No D0 callback is
connected to the M0613 resource-only adapter, and no laptop test is requested.

## Tests

The production wrappers run in the existing deterministic WDF/MMIO host model.
New cases cover failed Enable with omitted Disable, connection failure with no
Enable, retryable mask failure, DSP shutdown failure, failed pre-disable Stop
with DPC/worker queues, a DPC completing during cancellation, Disable masking
failure, wrong IRQL, repeat cleanup and refusal of missing pre-disable closure.

The model rejects any interrupt synchronization while disconnected. During
software-only portions, it also rejects every MMIO read and write. Negative
Disable-mask tests leave the production release gate blocked; explicitly marked
fixture-only teardown does not claim successful production recovery. These are
ordered callback/interleaving tests, not real KMDF scheduling or concurrency
stress tests. The original regression cases remain in the same executable.

Local GCC ASan/UBSan: 264,785 assertions pass, with LeakSanitizer disabled under
ptrace. CI evidence for the exact implementation commit follows.
No physical hardware execution or playback.


## Verified CI evidence (2026-09-20)

Implementation commit: `eb80b1816b29b48265a3a007907a3c737f2934e1`.
Tree: `9c2f506fe6aebf383bcb2aae746f02e42509a245`.

- [WDK run 35534032135](https://github.com/onish12/360/actions/runs/35534032135),
  job `106139723631`: real WDK compilation passes, followed by 10 host tests.
- [Offline run 35534032116](https://github.com/onish12/360/actions/runs/35534032116):
  Windows job `106139723571` passes 14 CTest tests; Linux job `106139723421`
  passes 12 CTest tests with ASan/UBSan instrumentation.
- All three jobs report `SOF_GLK_BOOT_TESTS=264785 PASS`, with simulated Windows
  APIs and no hardware execution. This count includes the existing regression
  suite, new cases and additional assertions forbidding MMIO during software-only
  cleanup. It is not a count of independent scenarios or hardware tests.
- Windows fixture checks remain green: real CNG 10 hash assertions and 12
  combined pinned-snapshot assertions, with simulated boot entry.
- [PHASER360_M0614_WDK_IRQ_EXIT development artifact](https://github.com/onish12/360/actions/runs/35534032135/artifacts/10612501387):
  ID `10612501387`, 254,271 bytes. GitHub reports SHA-256
  `0560834160186e62fcde89e3d516105285520f803a2b0ddb0e97c0e784ba92b9`.
  This is service-reported archive metadata, not an independently computed hash.
  The archive contains the static development library and sources/contracts;
  no installable driver or audio playback is supplied.

A subsequent documentation-only commit records these results and README status.
