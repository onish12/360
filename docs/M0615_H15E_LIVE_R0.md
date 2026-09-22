# M0.6.15 H15E-LIVE R0 — transient read-only MMIO gate

H15D-LIVE R1 proved the bounded PCI policy transition and exact rollback on the
physical Lenovo. No prior M0.5/M0.5.1 MMIO probe was executed successfully on
the rebuilt target, so M1 DSP boot remains blocked on live MMIO evidence.

H15E-LIVE is a separate exact-target Extension upper filter. During
PrepareHardware it:
1. performs the existing read-only PCI attestation for 8086:3198;
2. requires exactly two translated memory resources with lengths 0x4000
   (HDA BAR) and 0x100000 (DSP BAR);
3. maps each range transiently with PAGE_READONLY | PAGE_NOCACHE;
4. reads only the reviewed register set;
5. unmaps both ranges before returning from PrepareHardware;
6. exposes only the copied software snapshot through a non-power-managed IOCTL.

Reviewed register set:
- HDA: GCAP 0x00, VMIN 0x02, VMAJ 0x03, GCTL 0x08;
- Intel HDA vendor register: EM2 0x1030;
- DSP: ADSPCS 0x04, ADSPIS 0x0c, HIPCI 0x48, HIPCIE 0x4c,
  ROM status 0x80000.

There is no WRITE_REGISTER_* path, no PAGE_READWRITE mapping, no SetBusData,
no PciConfigBootPolicy, no DMA allocation, no interrupt ownership, no firmware
load, no DSP power/reset action, and no playback.

Primary references checked before implementation:
- Linux v6.12 include/sound/hda_register.h for GCAP/VMIN/VMAJ;
- Linux v6.12 sound/soc/sof/intel/hda.h for HDA/DSP BAR conventions,
  ADSPCS and ADSPIS;
- PHASER360 hda_controller.cpp for GCTL and Intel EM2;
- PHASER360 glk_rom.cpp for ADSPCS/HIPCI/HIPCIE/ROM-status offsets;
- Microsoft MmMapIoSpaceEx documentation: PAGE_READONLY and PAGE_NOCACHE;
- Microsoft READ_REGISTER_ULONG documentation: mapped-register read primitive;
- Microsoft MmUnmapIoSpace documentation: release of mapped I/O space.

A successful H15E capture proves only that the reviewed BAR resources can be
mapped read-only and that the selected register snapshot is accessible. It does
not authorize M1 DSP boot by itself.
