# M0.6.15 H15K — isolated ADSPCS CSTALL/CRST gate

H15J physically proved the HDA transport quiescent around GCTL.CRST and exposed
a runner-only ABI validation bug; the kernel result itself was successful and
restored GCTL exactly. H15K is the first physical DSP-MMIO mutation gate.

## Why H15K does not touch SPA yet

The verified Lenovo baseline is ADSPCS=0x001D003C. With the public Intel/SOF
field layout this means, for cores 0/1:
- CRST[1:0]=0;
- CSTALL[1:0]=0;
- SPA[1:0]=1 (core0 requested active, core1 not requested);
- CPA[1:0]=0 (neither core reports current power active).

Because SPA(core0)=1 while CPA(core0)=0, clearing and later restoring SPA would
not guarantee restoration of the original CPA status. H15K therefore isolates
only the reversible reset/stall fields. Power state is deferred to H15L.

## Exact transaction

Preconditions are the exact target, PCI policy, BAR layout and register baseline,
plus HDA transport idle (CORBCTL.RUN=0, RIRBCTL.DMA_EN=0 and 13/13 SDnCTL.RUN=0).

Only DSP ADSPCS offset 0x04 is writable. The transaction is:
1. baseline 0x001D003C;
2. set CSTALL[1:0] -> expected 0x001D033C;
3. set CRST[1:0] -> expected 0x001D033F;
4. restore CRST[1:0] -> expected 0x001D033C;
5. restore CSTALL[1:0] -> exact 0x001D003C.

Every stage is read back with a bounded 50 ms poll. If CRST rollback is not
proven, CSTALL is intentionally not cleared. This keeps an uncertain core
stalled/reset instead of unstalling an unproven state.

HDA BAR is PAGE_READONLY. DSP BAR is PAGE_READWRITE only because ADSPCS is
written. The write helper rejects every mask outside CSTALL[1:0]/CRST[1:0];
SPA/CPA bits cannot be requested by this gate.

There is no HDA MMIO write, PCI config write, DMA, WDF interrupt ownership,
firmware load, ROM purge, DSP run/unstall, codec programming or playback.

The runner derives exact hardware-restore proof from the returned restored
snapshot before the broad metadata validator. This prevents a parser-only error
from repeating the H15J cleanup failure after a proven exact MMIO rollback.

## Sources checked

- Linux SOF sound/soc/sof/intel/hda.h: ADSPCS CRST, CSTALL, SPA and CPA field
  shifts/masks.
- Linux SOF sound/soc/sof/intel/hda-dsp.c: stall/reset, reset enter/leave,
  power-up/down and core-run ordering.
- PHASER360 src/sof/glk_rom.cpp: existing independent GLK cold-state model.
- Verified Lenovo H15J physical result: ADSPCS=0x001D003C and HDA transport idle.

Passing H15K authorizes only a separately bounded H15L power-state gate. It does
not authorize firmware, DMA, IRQ ownership or playback.
