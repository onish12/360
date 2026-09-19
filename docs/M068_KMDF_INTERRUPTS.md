# M0.6.8 KMDF IPC interrupt bridge

This development component registers real KMDF callbacks and connects the M067
notification transport to a DIRQL ISR and PASSIVE_LEVEL interrupt work item.
It remains a static library, not a PnP/audio driver or installable package.
No Lenovo execution, IRQ delivery or audio latency has been validated.

## Sources checked before implementation

- [Microsoft: passive interrupt limitations, including MSI](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/supporting-passive-level-interrupts)
- [Microsoft: WDF_INTERRUPT_CONFIG](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfinterrupt/ns-wdfinterrupt-_wdf_interrupt_config)
- [Microsoft: WdfInterruptCreate and resource lifetime](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfinterrupt/nf-wdfinterrupt-wdfinterruptcreate)
- [Microsoft: interrupt work-item callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfinterrupt/nc-wdfinterrupt-evt_wdf_interrupt_workitem)
- [Microsoft: queue work item from ISR](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfinterrupt/nf-wdfinterrupt-wdfinterruptqueueworkitemforisr)
- [Microsoft: WdfInterruptSynchronize](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfinterrupt/nf-wdfinterrupt-wdfinterruptsynchronize)
- [Microsoft: WdfWaitLockAcquire](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfsync/nf-wdfsync-wdfwaitlockacquire)
- [Linux v6.12: ADSPIC/ADSPIS and HIPCCTL bits](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda.h)

Microsoft requires DIRQL handling for MSI. The bridge therefore sets
PassiveHandling=FALSE and uses a separate PASSIVE_LEVEL work item, not a
passive ISR. It supplies no DPC and sets AutomaticSerialization=FALSE because
its work item uses explicit synchronization.

## Required caller sequence

1. Construct IpcInterrupt and GlkBoot as C++ objects with nonpaged device-context
   lifetime. Zeroed WDF storage alone is not a substitute for construction.
2. In EvtDevicePrepareHardware, pass the assigned raw/translated interrupt
   descriptors, the same live GLK DSP mapping used by GlkBoot, and its owner to
   Create. Create validates descriptor types, pointer alignment and minimum
   register extent. It allocates a device-parented wait lock and interrupt
   context but does no MMIO. Partial creation failure deletes the wait lock.
3. The framework Enable callback masks DSP IPC sources and opens only the
   software enabled gate. It does not start firmware or unmask notifications.
4. Complete the existing GlkBoot.Prepare/Transfer sequence. Arm requires a usable
   command transport. It enables only HIPCCTL BUSY and ADSPIC IPC, leaving DONE
   disabled because command replies are still polled.
5. While attached/armed, use bridge Command and Pop. Do not access GlkBoot
   concurrently or bypass the bridge. The future driver must stop admission of
   new calls before power-down.
6. Call Stop from EvtDeviceD0ExitPreInterruptsDisabled, before GlkBoot.Shutdown,
   before the framework disconnects the interrupt, and before unmapping BARs.
   Stop is terminal for this instance. A successful return confirms masks and
   removes the bridge's mapping pointer. A false return requires retaining the
   mapping and explicit recovery; it is not permission to unmap/free resources.
7. Keep IpcInterrupt alive until its device-parented WDF objects and callbacks
   have been destroyed by the framework. After successful Stop, queued work
   cannot access the boot owner or DSP mapping. The bridge does not delete the
   device or its interrupt object itself.

The design supports one cold-boot/D0 session per instance. Full suspend/resume,
surprise-removal recovery and repeated PrepareHardware lifetimes are not yet
implemented. The PnP owner must select the correct vector(s), ensure exclusive
controller ownership, set up platform interrupt routing, maintain the mapping
and enforce the lifetime contract. This component does not infer resources,
program PCI MSI capability registers, configure HDA global routing or touch
physical BAR addresses. No new hardware audit is needed to compile it.

## ISR, work item and serialization

The ISR runs under the framework interrupt lock. It claims only ADSPIS IPC with
HIPCT BUSY, preserves other register bits and masks ADSPIC IPC plus HIPCCTL
BUSY/DONE before queuing the framework work item. It performs no mailbox read,
message ACK, allocation, blocking wait or transport call. Unrelated shared
interrupt sources return FALSE. Inaccessible registers fault the software gate;
a mask failure does not masquerade as successful quiescence.

The work item acquires a separate WDF wait lock at PASSIVE_LEVEL. Short
WdfInterruptSynchronize callbacks protect gate state and MMIO mask/unmask
operations at DIRQL. The long transport operation never runs under the interrupt
spin lock. Work calls GlkBoot.PollNotifications while IRQ sources are masked,
then rearms only on success. Failed transport leaves notifications masked and
faults the bridge gate. The four-entry FIFO and supported message subset retain
M067's bounds; there is no automatic WaveRT endpoint callback or queue consumer.

Command takes the same wait lock, masks through synchronization, runs the
existing bounded polling command, and rearms only if its transport remains
usable. Pop takes the wait lock and copies a retained event without MMIO; this
also permits retrieval after a transport fault. Running reports software gate
and transport usability, not proof that an interrupt reached Windows. A command
can have a valid reply even if IRQ rearming subsequently fails; Running exposes
that separate health condition.

Stop takes the wait lock, waiting for an active transport operation to finish,
then synchronizes masking and terminal state with the ISR. A previously queued
work item may still execute, but its closed gate prevents MMIO/boot access.
No wait lock is acquired at DIRQL. Framework Enable/Disable callbacks already
run under the interrupt lock and do not recursively synchronize. Correct PnP
sequencing is required: directly disabling power while a work item runs,
without Stop, violates this component's contract.

## Verification scope

The production Windows adapter is included in the real WDK project. Host tests
invoke its registered callbacks through a fake WDF scheduler/lock model that
checks IRQL, lock order and allowed MMIO ranges. Tests cover creation failure,
boot gating, non-owned interrupts, masking and deferred delivery, command/IRQ
serialization, queued work after Stop/unmapping, invalid notifications, failed
unmask rollback and failed Stop mask readback with later explicit retry.

These are deterministic modeled interleavings, not a real multiprocessor race
stress test, Driver Verifier run, ISR latency measurement or device test.
Remaining work includes the actual PnP/power driver, firmware trust/binding,
complete notification handling, topology/codec setup, stream DMA and WaveRT.
