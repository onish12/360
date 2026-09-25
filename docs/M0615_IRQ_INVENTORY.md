# M0.6.15E interrupt resource inventory — no selection

M0.6.15E makes the assigned interrupt resources inspectable without selecting,
creating, enabling or programming an interrupt. It remains a development static
library and does not boot the DSP or create audio.

## Contract

Windows supplies raw and translated resource lists for the same assigned
hardware resources in the same order. For every paired CmResourceTypeInterrupt
descriptor, this milestone records both descriptors and a decoded immutable
snapshot.

The CM_RESOURCE_INTERRUPT_MESSAGE flag decides which union member is valid:

- flag clear: line-based interrupt, read u.Interrupt from raw and translated;
- flag set: message-signaled interrupt, read u.MessageInterrupt.Raw from the raw
  descriptor and u.MessageInterrupt.Translated from the translated descriptor.

A raw/translated pair that disagrees about CM_RESOURCE_INTERRUPT_MESSAGE is
rejected as structurally ambiguous. A message-signaled raw descriptor with
MessageCount zero is also rejected because it describes no usable assigned
message.

No numeric vector, IRQL, affinity, share disposition or trigger flag is treated
as the "expected Lenovo value" in this milestone. They are inventory data only.

## Captured inventory

Each interrupt entry retains the borrowed raw/translated descriptor pointers
for a later WdfInterruptCreate step and also captures:

- kind: line-based or message-signaled;
- raw and translated ShareDisposition;
- raw and translated Flags;
- raw MessageCount for message-signaled resources;
- raw level/vector/affinity for line-based resources;
- raw vector/affinity for message-signaled resources;
- translated level/vector/affinity using the correct union member.

The borrowed descriptor pointers still expire with ReleaseHardware. The copied
numeric inventory does not authorize using a stale descriptor later.

## Why selection remains deferred

Microsoft documents that MSI provides one assigned interrupt descriptor whose
raw MessageCount describes the assigned message count, while MSI-X provides a
separate assigned descriptor for each message. Windows can also assign a
line-based interrupt. A driver must tolerate the set actually assigned by PnP.

The current PHASER360 evidence has not yet captured the Lenovo's assigned
interrupt descriptor shape under our own PrepareHardware path. Therefore this
milestone does not assume line-based, MSI, MSI-X, one vector, one message or any
specific vector/IRQL/affinity.

The next physical evidence step must be read-only inventory. Only after that
evidence is reviewed may a later milestone select the descriptor pair supplied
to WDF_INTERRUPT_CONFIG.InterruptRaw/InterruptTranslated.

## Verification

The deterministic host model verifies both union families:

- line-based raw and translated level/vector/affinity are captured from
  u.Interrupt;
- message-signaled raw MessageCount/vector/affinity and translated
  level/vector/affinity are captured from the respective MessageInterrupt
  members;
- ShareDisposition and Flags are retained exactly;
- wake-hint and other non-selection flag information is preserved;
- message-vs-line disagreement is rejected before BAR mapping;
- a zero-message message descriptor is rejected before BAR mapping;
- the existing bounded interrupt-count, PnP/power, surprise-removal and resource
  lifetime tests remain active.

This is still a WDF host model. Real WDK compilation and Windows/Linux
regression must pass for the exact source before M0.6.15E is closed.

## Primary Microsoft references reviewed

- Raw and Translated Resources.
- CM_PARTIAL_RESOURCE_DESCRIPTOR.
- Using Interrupt Resource Descriptors.
- WDF_INTERRUPT_CONFIG.
- Finding and Mapping Hardware Resources.

No physical Lenovo execution, WdfInterruptCreate, DSP boot, codec/amplifier
programming or playback is authorized by M0.6.15E.


## Verified CI evidence (2026-09-21)

Final implementation commit: `e8678d81eb9be8d285ae18502b707eb34114a75c`.
Tree: `d578e2f5339843e2f74e0e5eca7d0e91c04da5da`.

- WDK/KMDF run `35573123679`, job `106248861373`: real WDK
  compilation passes, followed by all 10 selected host tests.
- Windows/Linux run `35573123785`: Windows job `106248862269` passes
  all 14 tests; Linux job `106248862157` passes all 12 tests with ASan/UBSan.
- PnP reports `SOF_PNP_RESOURCES_TESTS=360 PASS;
  irq_inventory=LINE_AND_MESSAGE; irq_selection=DEFERRED;
  power_skeleton=REGISTERED; surprise_callback=REGISTERED;
  paired_raw_translated=YES; hardware=NOT_TOUCHED`.
- The integrated boot/IRQ model remains
  `SOF_GLK_BOOT_TESTS=265876 PASS; hardware=NONE`.
- Windows also reports the existing real-CNG checks:
  `SOF_CNG_PIN_TESTS=10 PASS` and
  `SOF_PINNED_REFERENCE_TESTS=13 PASS`.
- Development artifact `PHASER360_M0615E_WDK_IRQ_INVENTORY`: ID
  `10626427860`, 279,933 bytes. GitHub reports archive SHA-256
  `db493ef41aace2215f8854f210eea71adf237c1732434aa4ec290372c596bd8f`.

The first E implementation attempt failed Linux compilation because aggregate
assignment with bare `{}` no longer matched the expanded PnpResourceView.
The final implementation uses explicit `PnpResourceView{}` value resets and
all evidence above is for that corrected exact source.

No physical Lenovo execution, WdfInterruptCreate, DSP boot, codec/amplifier
programming or playback is claimed by M0.6.15E.
