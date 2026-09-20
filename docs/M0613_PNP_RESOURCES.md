# M0.6.13 PnP resource callbacks (resource-only)

PnpResources registers real KMDF PrepareHardware and ReleaseHardware callbacks
before WdfDeviceCreate. Prepare validates the assigned translated memory
resources and maps the measured GLK HDA/DSP extents. Release unmaps them without
accessing registers. The component compiles in the development WDK library;
there is still no DriverEntry, DeviceAdd, INF, installer or audio playback here.

## Primary sources reviewed before implementation

- [Register callbacks before WdfDeviceCreate](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nf-wdfdevice-wdfdeviceinitsetpnppowereventcallbacks)
- [PrepareHardware ordering and failures](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_prepare_hardware)
- [ReleaseHardware after power-down](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_release_hardware)
- [D0Entry failure receives no D0Exit](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_d0_entry)
- [D0Exit and special power transitions](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_d0_exit)
- [Post-interrupt-enable ordering](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_d0_entry_post_interrupts_enabled)
- [Pre-interrupt-disable ordering](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfdevice/nc-wdfdevice-evt_wdf_device_d0_exit_pre_interrupts_disabled)
- [Map assigned memory](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-mmmapiospaceex)
- [Resource descriptors are borrowed, not modifiable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfresource/nf-wdfresource-wdfcmresourcelistgetdescriptor)
- [Unmap mapped memory at stop/remove](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-mmunmapiospace)

## Resource and lifetime contract

Configure initializes the supplied device attributes with the adapter's primary
context and installs only PrepareHardware/ReleaseHardware. The future DeviceAdd
must use those attributes in WdfDeviceCreate, then Attach a normally constructed,
noncopyable owner before returning. Its lifetime must extend past WDF device
cleanup. The context starts zeroed by WDF. Missing attachment rejects Prepare;
Release still succeeds after that failure. Configure replaces attributes, so it
must not overwrite another component's already configured context or callbacks.
A device/owner can only be attached once. All calls are serialized at PASSIVE.

Prepare accepts exactly two ordinary translated memory descriptors, in list
order: HDA 0x4000 bytes then DSP 0x100000 bytes. It reuses the existing checked
resource contract for nonzero, aligned, nonoverlapping, signed-range addresses.
It does not sort by address, copy historical physical addresses, mutate the
framework lists, or use raw bus addresses for mapping. Addresses above 4 GiB
are permitted. Read-only/write-only memory flags and MemoryLarge encodings are
rejected. Other resource types are ignored by this memory-only component;
**interrupt availability, routing and descriptor pairing are not validated**.
The resource count is bounded at 64; missing/null descriptors fail before maps.
This strict contract may reject other hardware layouts by design. Resource shape
alone does not establish PCI identity: future DeviceAdd/INF must enforce the
exact reviewed device and exclusive ownership before invoking this component.

Mappings use PAGE_READWRITE | PAGE_NOCACHE for eventual register access, but this
version performs no reads/writes and exposes no mapped pointers or handles.
If the second map fails, the first is immediately unmapped. Release tolerates
failed Prepare, repeated release and later preparation with reassigned addresses.
It unmaps DSP before HDA and clears ownership. A duplicate Prepare while mapped
fails without disturbing the current maps. No destructor performs implicit WDF
or MMIO cleanup; the registered Release callback owns that operation.

## Why DSP boot is not connected yet

The normal ColdPower sequence is insufficient to cover every framework failure.
D0Entry failure omits D0Exit; an interrupt-enable failure can bypass the intended
post-enable arm path; surprise removal can make registers inaccessible. Returning
an error from ReleaseHardware or retaining a pointer does not stop Windows from
tearing down device-parented objects or removing power. The existing BootDma
retention rule therefore cannot be advertised as complete PnP recovery.

This adapter deliberately has no DMA or interrupt consumers and no public mapping
accessor. Connecting PinnedFirmware.Enter requires a composed device lifetime,
failed-enable and surprise-removal fencing, draining without synchronizing a
disconnected interrupt, and DMA quiescence/teardown guarantees. Multi-D0 fresh
GlkBoot ownership and hibernation behavior also need integration. Do not add a
pointer getter and call Enter without resolving those paths. This milestone is
resource callback integration, not a complete boot/power driver or a fix for
those outstanding paths. The old M0.5.1 read-only probe is unchanged.

## Verification

The host test invokes the callbacks captured by the fake registration API and
checks null/unattached owners, wrong IRQL, duplicate attachment/start, malformed
resources, each allocation failure, idempotent release, reverse unmapping and
repeated starts with new translated addresses above 4 GiB. A distinct raw list
checks that raw addresses are not mapped. No register or interrupt API is linked
into this test, and no physical device is accessed. The shim is not real KMDF;
WDK compilation separately checks the real declarations and callback ABI.

Local GCC ASan/UBSan: 134 assertions pass (LeakSanitizer disabled under ptrace).
The shared WDF shim changes are also checked against the existing integrated
boot/IRQ suite. CI evidence for the exact source commit follows.


## Verified CI evidence (2026-09-20)

Implementation commit: `1d8852d2047c13fc06af5b35d5f62e4abc6e94c2`.
Tree: `4dc90682e156f8b3a3ee0cb5db5a1f522c5f8f39`.

- [WDK run 35493888145](https://github.com/onish12/360/actions/runs/35493888145),
  job `106033364591`: real WDK compilation passes, followed by 10 host tests.
- [Offline run 35493888199](https://github.com/onish12/360/actions/runs/35493888199):
  Windows job `106033364835` passes 14 CTest tests; Linux job `106033364944`
  passes 12 CTest tests with ASan/UBSan instrumentation.
- All three jobs report `SOF_PNP_RESOURCES_TESTS=134 PASS` with simulated callbacks.
  The existing integrated boot/IRQ test retains 199,647 passing assertions.
- Windows additionally checks the official fixture with real CNG: 10 hash and
  12 combined snapshot/entry assertions pass. Boot execution remains simulated.
- [Development artifact PHASER360_M0613_WDK_PNP_RESOURCES](https://github.com/onish12/360/actions/runs/35493888145/artifacts/10599473163):
  ID `10599473163`, 248,136 bytes. GitHub reports SHA-256
  `ca9519cfc8e5471a8fc0cbe500daa9b6c239116831296f0851e651c1c8a67b1a`.
  This is service-reported archive metadata, not an independently computed hash.
  The archive contains a static development library and source/contracts, not
  an installable driver. No hardware execution is claimed.

A subsequent documentation-only commit records these results and README status.
