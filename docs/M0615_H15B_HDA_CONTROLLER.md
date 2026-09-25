# M0.6.15H15B HDA controller ownership

H15B removes the dependency on HDA global state left by the previous Intel
driver before the PHASER360 firmware code-loader path starts.

## Boot ownership

The new HdaController runs before BootStream::Select:

1. validates GCAP and the mapped 0x4000-byte HDA BAR;
2. disables INTCTL, SSYNC and the DMA position-buffer enable bit;
3. drives GCTL.CRST into reset and requires readback;
4. holds reset for 500 us;
5. drives CRST ready and requires readback, then allows 1000 us;
6. reasserts the no-global-IRQ/no-sync/no-position-buffer policy;
7. clears Intel EM2.L1SEN for the DSP boot/runtime interval;
8. discovers PP and SPIB through LLCH, never fixed offsets;
9. requires every stream descriptor to be cold and clears W1C status;
10. enables only PPCTL.GPROCEN, leaving PIE/per-stream PROCEN clear;
11. disables and zeroes every SPIB stream before BootStream selects output DMA.

BootStream still owns the selected code-loader stream and its APL/GLK
PROCEN/FMT ordering, BDL, CBL, LVI, format, SPIB value, RUN and detach.

## Lifetime correction

The controller is not quiesced when firmware DMA stops. GlkBoot::Transfer stops
and releases only the boot stream/DMA, while GPROCEN and the HDA global state
remain alive for FW_READY and IPC runtime.

GlkBoot::Shutdown performs the order:

1. close command admission;
2. stop/detach/release any boot DMA;
3. power down the DSP;
4. quiesce HDA global state.

Quiesce clears GPROCEN, PIE, SPIB, INTCTL, SSYNC and DPLBASE enable, then
restores the pre-boot EM2.L1SEN value. Failure remains retryable.

## Remaining physical boundary

Linux SOF on APL/GLK also changes PCI configuration-space TCSEL, CGCTL and
PGCTL. H15B deliberately does not add PCI config-space writes. Physical M1 boot
therefore remains unauthorized until that state is owned or live evidence
proves it already satisfies the required contract.

No codec, SSP/PDM, endpoint or playback programming is added.

## Sources reviewed

- Intel High Definition Audio Specification rev. 1.0a.
- Linux SOF intel/hda-ctrl.c.
- Linux SOF intel/hda.c.
- Linux SOF intel/hda-stream.c.
- Linux SOF intel/hda-loader.c.
- Linux SOF intel/apl.c.
