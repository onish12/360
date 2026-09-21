# M0.6.15H4 composed single-D0 PnP lifecycle

M0.6.15H4 is the first milestone that composes the already-reviewed PnP
resource owner, dormant framework interrupt shell, pinned firmware entry,
GlkBoot and ColdPower behind the real KMDF callback ordering.

It remains a static-library/model milestone. It is not an installable driver and
is not executed on the physical Lenovo.

## Architecture

PnpResources remains the sole owner of HDA/DSP mappings and the only component
that opens/closes the shared HardwareAccessGate.

A copied PnpLifecycleOps table allows one DeviceLifecycle consumer to receive
only the ordered resource/power events. The table is installed before
PrepareHardware and cannot be replaced once resource preparation begins.

DeviceLifecycle owns no BAR mapping. It holds references to:

- one device-lifetime IpcInterrupt shell;
- one GlkBoot attempt;
- one PinnedFirmware owner;
- the same HardwareAccessGate used by PnpResources;
- one ColdPower state machine over that boot/IRQ/gate tuple.

H4 deliberately composes exactly one D0 attempt per GlkBoot. Arbitrary repeated
D0 sleep/resume with fresh boot-owner construction is not claimed yet.

## PrepareHardware

After raw/translated resources are validated, both BARs are mapped, the gate is
opened and the single IRQ pair passes the G admission rule, PnpResources calls
the lifecycle Prepared hook.

DeviceLifecycle then BindDormant-attaches the current DSP mapping and shared gate
to the already-created DeviceAdd interrupt shell. The raw/translated descriptor
pointers are validated during that handoff but are not retained by the IRQ
owner.

If the lifecycle rejects Prepared, PnpResources unwinds the mappings and closes
the gate before returning the failure.

## D0Entry

The ordered H4 path is:

1. GrantBootStart;
2. PinnedFirmware::Enter;
3. ColdPower::Enter;
4. require the existing boot evidence to reach commandReady;
5. GrantFrameworkEnableAfterBoot;
6. return success to KMDF.

Only step 5 permits the subsequent framework EvtInterruptEnable callback to
touch DSP interrupt-source registers.

If firmware admission fails before ColdPower starts, the still-dormant binding
is unbound directly. If ColdPower starts but D0Entry fails and confirms early
cleanup, ResetDormantClosedSession removes the old boot/BAR/grants before the
failed D0Entry returns. This is required because KMDF does not provide the
driver a matching D0Exit callback after a failed D0Entry.

A new explicit ColdPower::AbortBeforeInterruptsEnabled path covers the narrower
case where boot succeeded but the second software grant fails before framework
interrupt Enable.

## D0Entry tail SurpriseRemoval race

SurpriseRemoval can race the final portion of D0Entry.

If the hardware-access gate becomes terminal after boot work but before
EvtInterruptEnable, the lifecycle uses the terminal software fence and
ResetDormantRemovedSession. Because framework Enable has not happened, there is
no ISR/DPC/work item to drain.

PnpResources independently rechecks the gate after the consumer D0Entry returns.
If SurpriseRemoval wins in the final handoff window, PnP invokes the terminal
fence and exit cleanup immediately before returning failed D0Entry. It does not
wait for a framework D0Exit that will not be supplied for that failed entry.

No hardware shutdown is claimed on the terminal removed path.

## Post-interrupt enable

EvtInterruptEnable is still the existing IRQ implementation. After it returns,
EvtDeviceD0EntryPostInterruptsEnabled calls ColdPower::AfterInterruptsEnabled,
which arms the reviewed interrupt path only after command-ready firmware.

A post-enable arm failure is returned to KMDF and the existing ColdPower
shutdown/fallback state remains available for the power-down callbacks.

## Normal power-down

The order remains:

1. EvtDeviceD0ExitPreInterruptsDisabled;
2. ColdPower::BeforeInterruptsDisabled;
3. interrupt Stop/mask;
4. DPC cancel and work-item flush;
5. DSP/DMA shutdown;
6. framework EvtInterruptDisable;
7. EvtDeviceD0Exit;
8. fallback AfterInterruptsDisconnected if pre-disable could not confirm Stop;
9. ResetDormantClosedSession;
10. later ReleaseHardware unmaps the BARs.

PnpResources advances to PreInterruptsDisabled even if the consumer pre-disable
hook reports failure, so the post-framework-disconnect D0Exit fallback remains
reachable.

ReleaseHardware calls the lifecycle release hook before closing the gate or
unmapping. A still-bound/unsafe consumer makes release fail closed in the model.

## Surprise removal while operational

EvtDeviceSurpriseRemoval first makes the HardwareAccessGate terminal, then
notifies DeviceLifecycle.

The lifecycle only calls FenceForSurpriseRemoval. No MMIO, interrupt
synchronization, DSP shutdown or DMA-quiescence claim is made there.

The subsequent pre-disable callback remains software-only. EvtInterruptDisable
may report that a hardware mask cannot be confirmed, but the gate prevents any
actual register access. After framework disconnect, D0Exit drains deferred
software work and ResetDormantRemovedSession clears stale BAR/boot/grant
pointers without claiming that hardware was quiesced.

This distinction is intentional: normal removal is confirmed shutdown;
surprise removal is terminal no-new-access abandonment.

## Deterministic tests

The PnP test now verifies:

- lifecycle installation is one-shot and only before preparation;
- Prepared is called only after full resource validation/map/gate open;
- D0Entry, post-enable, pre-disable, D0Exit and Release ordering;
- pre-disable failure still transitions to the D0Exit fallback phase;
- SurpriseRemoval closes the gate before the consumer notification;
- a consumer Prepared failure unwinds both mappings;
- a SurpriseRemoval race at the tail of D0Entry invokes immediate compensating
  fence/exit cleanup before failed entry returns.

The integrated GLK/IRQ test verifies the real DeviceLifecycle consumer:

- normal composed boot and framework Enable/PostEnable;
- notification delivery;
- forced pre-disable mask failure followed by successful D0Exit fallback after
  framework disconnect;
- failed D0Entry cleanup with no synthetic D0Exit;
- firmware admission failure before ColdPower entry;
- terminal removal after command-ready boot but before framework Enable;
- terminal removal while operational with MMIO forbidden during all remaining
  teardown;
- stale binding/grants are cleared before lifecycle Release succeeds.

## Known H4 boundary

One GlkBoot object still represents one boot attempt. After a completed H4 D0
session, another D0Entry on the same composite is intentionally rejected.

The next milestone must provide fresh GlkBoot/ColdPower ownership for arbitrary
D0 cycles without weakening the H2/H3 resource and permission gates.

H4 also does not provide DriverEntry, DeviceAdd implementation, INF, firmware
file acquisition, codec programming, amplifier control or any audio endpoint.

No physical Lenovo execution or playback is authorized by this milestone.
