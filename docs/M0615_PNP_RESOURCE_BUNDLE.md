# M0.6.15B paired PnP resource lifetime

M0.6.15B connects the M0.6.15A terminal access gate to the existing
PrepareHardware/ReleaseHardware resource adapter. It remains a development
static library. It does not create a boot owner, select an interrupt vector,
load firmware, configure a codec/amplifier, create an endpoint or produce sound.

## Resource contract

Microsoft documents that the raw and translated resource lists describe the
same assigned resources in the same order. This implementation therefore
requires equal bounded list counts, non-null descriptors and matching descriptor
types at each index. It still selects the reviewed two memory resources by
memory occurrence order (HDA 0x4000, DSP 0x100000); unrelated resource types may
be interleaved.

Interrupt descriptors are retained as raw/translated pairs in a bounded borrowed
view, but this milestone deliberately does not choose one. The Lenovo's exact
live interrupt shape has not yet been established by the current physical
evidence, so single-vector/MSI assumptions are not introduced in source.

The borrowed resource view contains the mapped HDA/DSP virtual addresses,
validated lengths and up to eight interrupt descriptor pairs. Descriptor
pointers originate in the framework lists and are valid only during the prepared
resource lifetime. Any future consumer must still use the same
HardwareAccessGate on every MMIO/synchronization path; copying a pointer does not
bypass the terminal removal fence.

## Gate ordering

PrepareHardware keeps the gate Closed while validating lists and while mapping
both memory resources. Only after the entire resource bundle is prepared does it
perform Closed -> Open. A failure leaves the gate closed and unwinds partial
maps.

For an orderly ReleaseHardware, Open -> Closed occurs before either mapping is
unmapped. If surprise removal already changed the gate to terminal Removed, the
resource-only adapter may unmap its mappings because it has no DMA/boot/IRQ
consumer. Removed never returns to Closed or Open, so that owner cannot be reused
for another PrepareHardware lifetime.

This resource-only unmap rule is not the future composed-driver teardown rule.
Once DMA/boot/IRQ consumers are attached, ReleaseHardware must additionally
prove their lifecycle has reached its safe release state. M0.6.15B does not
claim that problem solved.

## Primary sources reviewed

- Microsoft, Raw and Translated Resources: both lists represent the same
  resources in the same order; PCI memory resources are mapped in
  PrepareHardware and unmapped in ReleaseHardware.
- Microsoft, EvtDevicePrepareHardware: resource traversal and mapping occur
  before D0Entry.
- Microsoft, EvtDevicePrepareHardware / resource lifetime: raw/translated list
  handles remain valid until ReleaseHardware returns.
- Microsoft, WDM IRPs and KMDF event callback functions: orderly and surprise
  removal callback ordering.
- Microsoft, EvtDeviceSurpriseRemoval: it is not synchronized with other PnP
  and power callbacks.
- Microsoft, framework object hierarchy and DMA/common-buffer ownership: later
  composed consumers require explicit lifetime control before parent deletion.

## Verification scope

The host test checks mismatched raw/translated counts and types, null
descriptors, unsupported memory encodings/flags, malformed BAR sizes/addresses,
too many interrupt candidates, both map-allocation failures, duplicate prepare,
wrong IRQL, borrowed interrupt-pair identity, gate open/close ordering, reverse
unmapping and repeated normal starts with reassigned addresses above 4 GiB.

A terminal surprise-removal case verifies that copied resource access is refused,
Release unmaps without reopening the gate, and a later Prepare is permanently
rejected.

The WDF/resource model is deterministic and does not touch hardware. Real WDK
compilation plus Windows/Linux regression must pass before this submilestone is
closed.


## Verified CI evidence (2026-09-20)

Implementation commit: `66055d64c1db1922768cf7831dc96b4c323022fe`.
Tree: `75fa96ec71d839325441a45680ca849089ad7ef3`.

- WDK/KMDF run `35536444898`, job `106146221656`: real WDK compilation
  passes, followed by all 10 selected host tests. The PnP suite reports
  `SOF_PNP_RESOURCES_TESTS=218 PASS; paired_raw_translated=YES;
  irq_selection=DEFERRED; hardware=NOT_TOUCHED`; the integrated boot/IRQ model
  remains `SOF_GLK_BOOT_TESTS=265876 PASS`.
- Windows/Linux run `35536444957`: Windows job `106146221821` passes all
  14 tests; Linux job `106146221723` passes all 12 tests with ASan/UBSan.
  The hash-pinned official reference check passes on both supported paths.
- Windows additionally reports `SOF_CNG_PIN_TESTS=10 PASS` using real Windows
  CNG and the official fixture, plus `SOF_PINNED_REFERENCE_TESTS=13 PASS`
  with real CNG and simulated boot.
- Development artifact `PHASER360_M0615B_WDK_PNP_RESOURCE_BUNDLE`: ID
  `10612847751`, 266,940 bytes. GitHub reports archive SHA-256
  `afaab111218723ea91aa1aaa64d720745de1d480b3c27c136868aba457be3bc1`.
  This is service-reported archive metadata, not an independently downloaded
  and recomputed archive hash.

No physical controller access, interrupt selection, DSP boot, DMA consumer,
codec/amplifier programming or playback is claimed by M0.6.15B.
