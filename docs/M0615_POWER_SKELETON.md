# M0.6.15D KMDF D0 callback skeleton

M0.6.15D registers the KMDF power callbacks needed by the future composed
controller owner, but deliberately performs no DSP boot, MMIO, DMA, interrupt
selection/programming, codec operation, amplifier operation or playback.

The purpose is to make callback ordering an explicit, tested invariant before
ColdPower is connected to real PnP/power ownership.

## Framework ordering

The skeleton registers:

1. EvtDeviceD0Entry
2. EvtDeviceD0EntryPostInterruptsEnabled
3. EvtDeviceD0ExitPreInterruptsDisabled
4. EvtDeviceD0Exit

Microsoft's KMDF sequence places EvtDeviceD0Entry after PrepareHardware and
before EvtInterruptEnable. EvtDeviceD0EntryPostInterruptsEnabled follows the
framework's interrupt-enable callbacks. On power-down, the ordering reverses:
EvtDeviceD0ExitPreInterruptsDisabled runs before EvtInterruptDisable and
EvtDeviceD0Exit runs afterward.

Each of these device power callbacks is PASSIVE_LEVEL. The current
implementation uses that phase separation only as a software lifecycle guard.

## State machine

PnpResources now exposes a small internal lifecycle:

- NoResources
- Prepared
- D0Entered
- Operational
- PreInterruptsDisabled

Successful PrepareHardware moves NoResources -> Prepared.
EvtDeviceD0Entry moves Prepared -> D0Entered only while the terminal hardware
access gate remains open. The post-interrupt callback moves D0Entered ->
Operational. The pre-disable callback accepts Operational or D0Entered and moves
to PreInterruptsDisabled. EvtDeviceD0Exit then returns the owner to Prepared.
ReleaseHardware may unmap only from NoResources or Prepared.

The pre-disable and D0Exit callbacks deliberately do not require Allowed().
After surprise removal the terminal gate is closed, but KMDF still normally
runs the D0 unwind sequence before ReleaseHardware. These skeleton callbacks do
not touch hardware and therefore can safely record that framework ordering.

## Failure semantics

Microsoft documents that if EvtDeviceD0Entry returns failure, the framework does
not call that driver's EvtDeviceD0Exit. For that reason a failed D0Entry leaves
the lifecycle at Prepared, which permits ReleaseHardware without inventing a
synthetic D0Exit.

ReleaseHardware rejects an owner that is still D0Entered, Operational or
PreInterruptsDisabled. This is intentional: once the future boot/DMA/IRQ
consumer is connected, unmapping while the lifecycle still claims D0 would be
unsafe. This milestone establishes that rule before any consumer is attached.

## Verification

The PnP host model checks:

- all four power callbacks are registered;
- they reject calls before PrepareHardware;
- wrong-IRQL D0Entry is rejected without changing phase;
- normal Prepare -> D0Entry -> Post -> Pre -> D0Exit -> Release order;
- duplicate/out-of-order callback calls are rejected;
- power callbacks perform no additional map/unmap activity;
- ReleaseHardware is refused while Operational;
- surprise removal from Operational closes the hardware gate, after which
  PreInterruptsDisabled and D0Exit still succeed without hardware access;
- ReleaseHardware succeeds only after that unwind;
- terminal removal before D0Entry makes D0Entry fail, and ReleaseHardware can
  then proceed directly from Prepared, matching KMDF's documented failed-entry
  semantics;
- the existing removal-during-map and resource-validation tests remain active.

This is still a deterministic WDF shim, not proof of real multicore callback
interleavings. Real WDK compilation, Windows tests and Linux ASan/UBSan tests
must all pass for the exact source before M0.6.15D is closed.

## Primary Microsoft references reviewed

- EVT_WDF_DEVICE_D0_ENTRY
- EVT_WDF_DEVICE_D0_ENTRY_POST_INTERRUPTS_ENABLED
- EVT_WDF_DEVICE_D0_EXIT_PRE_INTERRUPTS_DISABLED
- EVT_WDF_DEVICE_D0_EXIT
- WDM IRPs and WDF Event Callback Functions
- Enabling and Disabling Interrupts
- PnP and Power Management Callback Sequences

No physical Lenovo execution or playback is authorized by this milestone.
