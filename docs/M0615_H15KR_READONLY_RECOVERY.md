# H15KR — read-only recovery probe after H15K

H15KR is a recovery-only function driver for the exact DEV_3198 target. It maps
both HDA and DSP BARs PAGE_READONLY | PAGE_NOCACHE and captures the post-H15K
state without modifying hardware.

It reads:
- HDA GCAP/VMIN/VMAJ/GCTL, CORBCTL, RIRBCTL, all stream RUN bits, Intel EM2;
- DSP ADSPCS, ADSPIC, ADSPIS, HIPCI, HIPCIE, HIPCCTL and ROM status.

It performs no MMIO write, PCI config write, DMA, interrupt ownership, firmware
load, DSP boot, codec programming or playback.

Purpose: determine whether the failed H15K run restored ADSPCS exactly to
0x001D003C. Cleanup back to Intel is performed only by the user-space recovery
runner after the snapshot proves both ADSPCS exact restoration and HDA idle.
