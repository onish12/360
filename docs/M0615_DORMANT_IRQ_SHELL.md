# M0.6.15H1 dormant device-lifetime interrupt shell

M0.6.15H1 creates the framework interrupt object without attaching boot/DSP
hardware ownership. This is deliberately separated from resource binding and
ColdPower.

## WDF model

Microsoft allows WdfInterruptCreate from EvtDriverDeviceAdd with
WDF_INTERRUPT_CONFIG.InterruptRaw and InterruptTranslated both NULL. After PnP
assigns resources, the framework associates interrupt resources with the
framework interrupt objects. Creating in EvtDevicePrepareHardware is also
supported, but then both descriptor pointers must be valid.

For this DEV_3198 hardware contract, the live PCI capability reports a maximum
of one message and M0.6.15G admits exactly one interrupt pair. Therefore the
future device owner needs exactly one framework interrupt shell; it does not
pre-create unused extra MSI objects.

## Dormant safety

CreateDormant creates the existing wait-lock, work item, DPC and DIRQL interrupt
object with no raw/translated descriptors, no GlkBoot pointer and no DSP BAR
pointer. It performs no MMIO.

While hardwareEnableAllowed_ is false:

- EvtInterruptEnable records framework connection and returns success without
  Mask(), register reads/writes or interrupt synchronization;
- EvtInterruptDisable returns success without Mask();
- ISR returns FALSE because the bridge is never armed/ready;
- Running and Arm fail closed;
- Command returns the default State result;
- Pop returns false;
- no DPC/work item is queued.

The existing explicit-descriptor Create path remains enabled for the older
boot/IRQ model tests and preserves its hardware-mask behavior. H1 therefore
does not silently weaken already-tested shutdown/enable contracts.

## Deferred work

H1 intentionally does not add the method that binds a dormant shell to
PnpResources/GlkBoot. That will be a separate submilestone after H1 is green,
because resource admission, D0Entry boot completion and permission for the
subsequent framework EvtInterruptEnable must be ordered together.

In particular, a dormant shell must never decide on its own that mapped BARs
are sufficient permission for MMIO.

## Verification

The new deterministic wrapper test:

1. rejects null-device and wrong-IRQL dormant creation;
2. creates with InterruptRaw/InterruptTranslated both NULL;
3. forbids all fake MMIO;
4. runs framework Enable;
5. injects an ISR;
6. calls Running, Arm, Pop and Command;
7. runs framework Disable;
8. requires zero register writes and zero WdfInterruptSynchronize calls;
9. deletes the test WDF children only after disconnect.

Real WDK compilation is required because WdfInterruptCreate parameter/lifecycle
rules are framework contracts, not just shim behavior.

No driver install, device binding, MMIO, DSP boot, codec/amplifier operation or
playback is authorized by H1.
