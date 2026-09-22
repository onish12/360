# M0.6.15H15C read-only PCI configuration attestation

H15C adds a fail-closed PCI configuration-space gate before HDA MMIO
initialization. It does not write PCI configuration space and it does not
authorize physical M1 boot.

## Windows access contract

The driver requests GUID_BUS_INTERFACE_STANDARD from its own PCI stack with
WdfFdoQueryForInterface at PASSIVE_LEVEL. It performs exactly one bounded
GetBusData read of the conventional 256-byte PCI configuration space and then
dereferences the bus interface.

SetBusData is not called.

This follows the Windows-supported PCI configuration-space access path. Windows
owns the PCI header and the entire linked capability list; only configuration
space outside those regions may later be considered vendor-defined.

## Exact target attestation

The read-only capture requires:

- Vendor ID 0x8086;
- Device ID 0x3198;
- PCI header type 0;
- a valid, acyclic conventional capability chain;
- every capability pointer at or above 0x50.

The type-0 common header ends at 0x3f. Requiring the complete linked
capability chain to remain at/above 0x50 proves, on the actual target instance,
that bytes 0x40..0x4f are outside the standard header and capability list.

H15C records, without interpreting or modifying them:

- 0x44: candidate Intel PGCTL/TCSEL value;
- 0x48: candidate Intel CGCTL value.

The capture occurs before HdaController::Initialize, so a failed PCI
attestation causes zero HDA-controller mutation and zero DSP mutation.

## Why this stage is read-only

Linux SOF for APL/GLK names 0x44 as PCI_TCSEL/PCI_PGCTL and 0x48 as PCI_CGCTL
and changes ADSP power/clock-gating bits around firmware boot. Public Intel
register documentation also exposes PGCTL at 0x44 and CGCTL at 0x48 on later
Intel HDA implementations. That is strong architectural corroboration, but it
is not sufficient by itself to authorize writes on this exact DEV_3198 target.

The next stage must use H15C live evidence from the Lenovo to confirm the
actual capability layout and initial 0x44/0x48 values before any SetBusData
path is introduced.

## Sources

- Microsoft Learn: Accessing PCI Device Configuration Space.
- Microsoft Learn: WdfFdoQueryForInterface.
- Microsoft Learn: PCI_COMMON_CONFIG.
- Linux SOF intel/hda.h and intel/hda-ctrl.c.
- Intel public HDA register documentation for PGCTL/CGCTL on newer platforms.

## Safety boundary

PCI_CONFIG_WRITE=FALSE
SETBUSDATA_CALLS=0
HDA_MMIO_BEFORE_ATTESTATION=FALSE
DSP_MMIO_BEFORE_ATTESTATION=FALSE
PHYSICAL_M1_BOOT_AUTHORIZED=FALSE
