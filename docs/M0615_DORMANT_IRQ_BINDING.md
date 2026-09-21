# M0.6.15H2 dormant PnP-to-IRQ software binding

M0.6.15H2 joins the prepared PnP resource contract to the device-lifetime
interrupt shell without granting hardware access to the interrupt callbacks.
It remains non-installable and does not boot the DSP.

## Prepared binding contract

PnpResources::CopyDormantInterruptBinding exports a short-lived snapshot only
when all of the following remain true:

- PrepareHardware completed and the shared HardwareAccessGate is still open;
- exactly one interrupt pair is present;
- the pair passes the M0.6.15G admission rule;
- the mapped DSP BAR is present with the reviewed 0x100000 length;
- the selected pair still matches the sole interrupt entry;
- surprise removal has not terminally closed the gate.

The snapshot contains:

- the shared HardwareAccessGate pointer;
- the current mapped DSP virtual address and length;
- the borrowed raw/translated interrupt descriptors;
- LINE/MESSAGE classification and message count.

The descriptor pointers are evidence for this resource lifetime only. The
interrupt shell validates them during binding but does not retain them.

## Dormant shell binding

IpcInterrupt::BindDormant accepts the short-lived PnP snapshot only at
PASSIVE_LEVEL and only for a device-lifetime shell created by CreateDormant.

It requires:

- no previous active binding in the shell;
- no framework Enable seen yet for this binding;
- a fresh GlkBoot owner;
- the same live HardwareAccessGate for PnP and GlkBoot;
- aligned 1 MiB DSP mapping;
- structurally matching raw/translated interrupt descriptors;
- LINE with messageCount=0, or MESSAGE with messageCount=1.

On success it stores only GlkBoot and the DSP BAR mapping. It deliberately
leaves hardwareEnableAllowed_ false.

Consequently:

- CanStartBeforeEnable returns false;
- Sync returns false before any WdfInterruptSynchronize call;
- framework EvtInterruptEnable remains hardware-inert;
- ISR remains unarmed;
- ColdPower cannot use this binding to start the DSP.

The separate hardware-enable grant is intentionally absent from H2.

## Resource lifetime release

IpcInterrupt::UnbindDormant is software-only. It succeeds only after framework
disconnect/Disable (or before Enable was ever seen), while no interrupt work is
pending and the shell has never been armed.

It clears GlkBoot/DSP pointers and per-binding framework state while preserving
the device-lifetime WDF interrupt shell. This permits a later PrepareHardware
cycle to bind a reassigned DSP mapping without retaining a pointer into the
previous resource lifetime.

A connected shell is not allowed to unbind its BAR lifetime.

## Deterministic verification

The PnP resource test verifies:

- line-based single-pair binding publication;
- one-message MSI binding publication;
- rejection of multi-message MSI;
- rejection of multiple interrupt pairs;
- rejection after ReleaseHardware;
- rejection after SurpriseRemoval;
- the exported DSP pointer/length exactly match the current mapped bundle.

The IRQ/boot wrapper verifies:

- invalid DSP length and inconsistent LINE message count are rejected;
- wrong IRQL is rejected;
- a valid binding attaches the same gate to a fresh GlkBoot;
- duplicate binding is rejected;
- CanStartBeforeEnable remains false;
- framework Enable -> ISR -> Disable with MMIO forbidden performs no MMIO and
  no WdfInterruptSynchronize;
- unbind is rejected while framework-connected;
- unbind succeeds after Disable;
- the same device-lifetime shell can bind/unbind another prepared lifetime.

No ColdPower entry, firmware transfer, DMA publication, interrupt-source mask,
codec/amplifier operation or playback is authorized by H2.
