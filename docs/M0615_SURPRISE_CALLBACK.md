# M0.6.15C framework surprise-removal fence

M0.6.15C connects the terminal HardwareAccessGate to KMDF's real
EvtDeviceSurpriseRemoval callback. It remains a development static library:
there is no DriverEntry, DeviceAdd, INF, DSP boot, selected interrupt, codec,
amplifier, endpoint or playback path.

## Microsoft contracts rechecked before implementation

The implementation was reviewed against these primary Microsoft contracts:

- EVT_WDF_DEVICE_SURPRISE_REMOVAL: callback signature is VOID(WDFDEVICE), it
  runs at PASSIVE_LEVEL, is registered through
  WdfDeviceInitSetPnpPowerEventCallbacks, and is not synchronized with other
  PnP/power callbacks.
- Surprise-Removal Sequence: EvtDeviceSurpriseRemoval is called before the
  remaining removal callbacks; when the device was already outside the working
  state, ReleaseHardware can follow immediately.
- WDM IRPs and KMDF callbacks: a surprise removal from D0 normally proceeds
  through D0ExitPreInterruptsDisabled, EvtInterruptDisable, D0Exit and
  ReleaseHardware after the surprise callback.
- EvtDevicePrepareHardware and hardware resource mapping: raw/translated lists
  remain valid through ReleaseHardware; translated memory is mapped during
  PrepareHardware and unmapped during ReleaseHardware.
- Raw and Translated Resources: the lists describe the same resource set in the
  same order; additional descriptors may be interleaved.
- InterlockedExchange/CompareExchange: these operations are atomic with respect
  to other Interlocked operations and must not be used on noncached/MMIO memory.

## Callback behavior

PnpResources::Configure now registers EvtDeviceSurpriseRemoval. The callback
touches only the immutable device-context owner pointer and its software
HardwareAccessGate. It performs no BAR access, unmapping, DMA action, interrupt
synchronization, queue drain, firmware operation or WDF deletion.

This narrow callback is intentional because KMDF does not serialize surprise
removal with other PnP/power callbacks. The gate transition is the only operation
that must win immediately: once Removed is observed, guarded HDA/DSP/IRQ
consumers refuse new hardware access.

## Prepare/Release race corrections

M0.6.15B had correct sequential behavior but did not yet expose the gate through
the real surprise callback. Wiring that callback also requires race-safe resource
transitions:

- CloseForRelease now treats terminal Removed as already closed for resource
  cleanup without changing Removed back to Closed.
- ReleaseHardware therefore performs one atomic close attempt instead of a
  separate Removed check followed by close; a concurrent surprise transition
  cannot strand cleanup in a false invalid-state result.
- PrepareHardware checks for Removed after the first BAR mapping and refuses to
  create the second mapping when surprise removal has already won.
- After both mappings and OpenForPrepare, PrepareHardware checks the gate once
  more before reporting success. If removal won before that linearization point,
  this resource-only owner unwinds the mappings and returns failure.
- CopyPreparedView takes a snapshot and rechecks gate access before returning.
  The view remains borrowed; any future consumer must still check the same gate
  on each actual hardware use.

None of these rules claims that a future composed DMA/boot driver can unmap
resources simply because the gate is closed. M0.6.15C still has no DMA/boot/IRQ
consumer. Parent-object DMA lifetime and composed teardown remain later work.

## Verification added

The PnP host model now captures the registered EvtDeviceSurpriseRemoval callback
rather than calling HardwareAccessGate::SurpriseRemove directly. It checks:

- the callback is actually registered;
- a callback on an unattached device context does not alter another owner's gate;
- a prepared owner becomes terminal Removed through the framework callback;
- ReleaseHardware can unmap the resource-only mappings after that transition
  without reopening the gate;
- subsequent PrepareHardware remains permanently rejected;
- a deterministic race injected from the first MmMapIoSpaceEx call invokes the
  surprise callback before DSP mapping/opening. Prepare must stop after one map,
  unmap that HDA mapping, return failure, and leave Removed terminal.

The fake callback scheduler is deterministic, not a real multicore KMDF race
test. Real WDK compilation plus Windows/Linux regression and sanitizer runs must
pass for the exact source before this submilestone is closed. No Lenovo
execution or playback is authorized by this milestone.
