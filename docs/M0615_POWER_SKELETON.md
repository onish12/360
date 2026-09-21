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


## Verified CI evidence (2026-09-21)

Implementation commit: `376c9a978206d38f5263e9f53aedfff910d08864`.
Tree: `13533c2e3cf5e5ab5a304ff552cb95f41415c65d`.

- WDK/KMDF run `35572367041`, job `106246514642`: real WDK
  compilation passes, then all 10 selected host tests pass. PnP reports
  `SOF_PNP_RESOURCES_TESTS=313 PASS; power_skeleton=REGISTERED;
  surprise_callback=REGISTERED; paired_raw_translated=YES;
  irq_selection=DEFERRED; hardware=NOT_TOUCHED`.
- Windows/Linux run `35572367172`: Windows job `106246514910` passes
  all 14 tests; Linux job `106246515245` passes all 12 tests with ASan/UBSan.
  The integrated boot/IRQ model remains `SOF_GLK_BOOT_TESTS=265876 PASS`.
- Windows also reports `SOF_CNG_PIN_TESTS=10 PASS` and
  `SOF_PINNED_REFERENCE_TESTS=13 PASS` using real Windows CNG for the
  official hash-pinned fixture checks.
- Development artifact `PHASER360_M0615D_WDK_POWER_SKELETON`: ID
  `10626895273`, 276,725 bytes. GitHub reports archive SHA-256
  `6ecca4b70fb78e3ccd00b47c4db1f1a822ded748578f22af1b81e58ff81531eb`.

No physical Lenovo execution, DSP boot, interrupt selection, codec/amplifier
programming or playback is claimed by M0.6.15D.
