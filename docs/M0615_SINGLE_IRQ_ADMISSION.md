# M0.6.15G single interrupt admission — no WDF interrupt

M0.6.15G converts the interrupt inventory into a deliberately narrow admission
rule for the Gemini Lake DEV_3198 controller. It still does not call
WdfInterruptCreate, enable an interrupt, touch MMIO, boot the DSP or create
audio.

## Live evidence used

The reviewed F4 target capture proves:

- exactly one allocated Configuration Manager IRQ resource is present;
- HDA/DSP physical resources still match 0xCEEE0000/0x4000 and
  0xCEF00000/0x100000;
- DEVPKEY_PciDevice_InterruptSupport = 3;
- DEVPKEY_PciDevice_InterruptMessageMaximum = 1.

Windows pciprop.h defines InterruptSupport bits as LineBased=1, Msi=2 and
MsiX=4. Value 3 therefore means this PCI function reports line-based and MSI
hardware support, but no MSI-X support bit. MessageMaximum=1 limits the reported
hardware message capacity to one.

The current Intel-driver ConfigMgr IRQ resource is edge-triggered and has
IRQD_Alloc_Num 0xFFFFFFFB (-5). That value is retained as corroborating
evidence only. It is not used as the production LINE/MSI discriminator because
Configuration Manager's IRQ_RESOURCE does not expose the kernel
CM_RESOURCE_INTERRUPT_MESSAGE bit.

## Admission rule

CopySingleInterruptForCreate succeeds only while the prepared resource lifetime
and hardware-access gate remain valid and:

1. exactly one paired interrupt descriptor is present;
2. both borrowed raw/translated pointers are non-null interrupt descriptors;
3. raw and translated MESSAGE classification still agrees;
4. line-based descriptors have no message count; or
5. message-signaled descriptors have exactly one raw MessageCount.

Anything else fails closed and clears the output object.

The selector does not constrain level, vector, affinity, share disposition,
wake flags or any ConfigMgr negative IRQ number. Those values remain PnP-owned
assignment data.

This policy intentionally accepts either line-based or one-message MSI. The
actual signaling kind for our future driver is decided only by
CM_RESOURCE_INTERRUPT_MESSAGE in the raw/translated descriptors supplied to
its own PrepareHardware callback.

## Why multi-message is rejected

M0.6.15E can inventory a valid multi-message MSI descriptor. Inventory support
is broader than device admission. The live PCI capability property reports
InterruptMessageMaximum=1 for this exact DEV_3198 function, so M0.6.15G rejects
MessageCount > 1 rather than silently expanding the interrupt object model.

A future hardware revision or different PCI identity would require a separately
reviewed contract rather than bypassing this check.

## Tests

The host test requires:

- one line-based pair -> admitted;
- one message-signaled pair with MessageCount=1 -> admitted;
- one message-signaled pair with MessageCount=4 -> inventoried but not admitted;
- two otherwise-valid interrupt pairs -> inventoried but not admitted;
- released/closed resource lifetime -> no pair returned;
- all previous raw/translated mismatch, zero-message, resource, power and
  surprise-removal tests remain active.

No WDF interrupt object is created by this milestone.

## Next boundary

After G passes real WDK plus Windows/Linux regression, the next milestone may
compose the admitted pair into the existing IpcInterrupt object creation path.
That step must still be tested without DSP boot first. Interrupt creation and
framework connection are not permission to enable DSP interrupt sources.

The no-playback hold remains unchanged.
