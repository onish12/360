# M0.6.15 H15G — function-driver read-only MMIO through D0

H15F proved that PHASER360 can become the active function driver for DEV_3198,
receive the exact controller resources and enter D0, then roll back exactly to
the Intel baseline.

H15G keeps the same exact-target function-driver handoff but adds only one new
capability: the HDA and DSP resources are mapped with
PAGE_READONLY | PAGE_NOCACHE during PrepareHardware, retained through D0, read
once during D0Entry, and unmapped in ReleaseHardware.

The D0 snapshot reads:
- HDA GCAP 0x00, VMIN 0x02, VMAJ 0x03, GCTL 0x08, Intel EM2 0x1030;
- DSP ADSPCS 0x04, ADSPIS 0x0c, HIPCI 0x48, HIPCIE 0x4c,
  ROM status 0x80000.

The source and binary gates forbid WRITE_REGISTER_*, PAGE_READWRITE,
BUS_INTERFACE_STANDARD/SetBusData, WdfInterruptCreate, WDF DMA/common buffers,
firmware loading, DSP boot and playback.

A successful H15G result proves that the same BARs previously observed by H15E
remain readable for the full PrepareHardware -> D0 ownership lifetime while
PHASER360 is the function driver. It still does not authorize any hardware
mutation.
