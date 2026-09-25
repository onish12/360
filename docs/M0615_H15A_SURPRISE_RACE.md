# M0.6.15H15A surprise-removal race closure

H15A closes two races found during the post-H14 audit. It does not add new
hardware behavior and does not authorize target installation.

## 1. Single terminal-removal truth

KMDF does not serialize EvtDeviceSurpriseRemoval with the other PnP/power
callbacks. The repeated-D0 owner previously duplicated the terminal state in a
plain bool named removed_ and the surprise callback also inspected irqBound_.
Those fields are otherwise owned by the serialized PnP/power path, so accessing
them from the unsynchronized callback created a data race.

H15A removes removed_ completely. HardwareAccessGate::Removed(), already backed
by interlocked operations, is now the only terminal-removal truth. The surprise
callback touches no repeated-lifecycle bool. It records atomic telemetry and
unconditionally requests IpcInterrupt::FenceForSurpriseRemoval(); that method
uses its own WDF wait lock and performs no MMIO.

active_, prepared_ and irqBound_ remain ordinary bools because they are no
longer accessed by EvtDeviceSurpriseRemoval and remain inside the serialized
PnP/power lifecycle.

## 2. PrepareHardware post-callback linearization

PrepareHardware already checked the gate before invoking lifecycle_.prepared.
Surprise removal could nevertheless occur during that callback, after which
Prepare returned success without another gate observation.

H15A adds one final gate check after successful lifecycle preparation. If the
terminal removal won during the callback, Release() unwinds the lifecycle bundle
and both BAR mappings while phase is still Prepared, then Prepare returns
STATUS_INVALID_DEVICE_STATE. A removal that wins after this final atomic
observation is ordered after successful preparation and follows the normal
surprise-removal teardown sequence.

The regression model fires the real registered surprise callback from inside
the Prepared hook and requires the exact sequence {prepared, surprise, release}
with no live mappings left.

## Safety boundary

H15A does not install a driver, modify target trust, boot the physical DSP,
program HDA/SSP/PDM/codecs, create endpoints or play audio.

## Primary references

- Microsoft Learn: Surprise-Removal Sequence
  https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/surprise-removal-sequence
- Microsoft Learn: EVT_WDF_DEVICE_SURPRISE_REMOVAL
  https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_surprise_removal
- Microsoft Learn: Using Automatic Synchronization
  https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/using-automatic-synchronization
- Microsoft Learn: Using Framework Locks
  https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/using-framework-locks
- Microsoft Learn: WdfWaitLockAcquire
  https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfsync/nf-wdfsync-wdfwaitlockacquire
- Microsoft Learn: WDM IRPs and KMDF event callback functions
  https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/wdm-irps-and-kmdf-event-callback-functions
