# M0.6.15 H15J — HDA quiescence and DSP-status gate

H15I proved the isolated HDA GCTL.CRST 0 -> 1 -> 0 transition and exact return
to the Intel binding baseline. The physical H15I capture also showed one new
hardware-side observation: DSP ADSPIS changed from 0x00000000 to 0x00040000
after CRST became ready and remained 0x00040000 after GCTL returned to zero.
H15I did not write the DSP BAR.

H15J does not interpret that bit. It closes the remaining HDA reset precondition
and extends read-only DSP status capture.

## One-shot transaction

The exact H15I baseline is required. H15J then:

1. captures the existing HDA/DSP baseline and PCI attestation;
2. holds the already-reset controller for 500 us;
3. writes only HDA GCTL.CRST from 0 to 1 and waits for CRST=1;
4. waits 1000 us and captures HDA CORBCTL.RUN, HDA RIRBCTL.RUN, every stream
   RUN bit derived from GCAP, plus DSP ADSPCS, ADSPIC, ADSPIS, HIPCI, HIPCIE,
   HIPCCTL and ROM status;
5. requires CORBCTL.RUN=0, RIRBCTL.RUN=0 and all stream RUN bits=0;
6. only after that proof writes GCTL.CRST from 1 back to 0;
7. requires exact GCTL restoration and records the final DSP-status snapshot.

If the ready-state quiescence proof fails after CRST has been set, the driver
does not issue the CRST clear. The transaction fails closed and the runner
retains the experimental package/trust instead of claiming restoration.

DSP BAR remains PAGE_READONLY. There is no DSP MMIO write, ADSPCS write, PCI
configuration write, DMA allocation, WDF interrupt ownership, firmware load,
DSP boot or playback. No HDA register other than GCTL.CRST is written.

## Why this gate exists

Intel High Definition Audio Specification rev. 1.0a requires CORB/RIRB RUN and
all stream RUN bits to be cleared before CRST is written to 0 for a clean
restart. H15I verified GCTL itself but did not capture those RUN bits while
CRST was ready.

The project also needs to characterize the H15I ADSPIS 0x00040000 observation
before authorizing the first ADSPCS mutation. H15J adds ADSPIC and HIPCCTL to
the read-only DSP snapshot without assigning undocumented meaning to ADSPIS
bit 18.

## Sources checked

- Intel High Definition Audio Specification rev. 1.0a, GCTL.CRST reset rules.
- Intel public GCTL register documentation.
- Linux SOF Intel HDA register definitions in sound/soc/sof/intel/hda.h.
- Linux SOF HDA/DSP sequencing in sound/soc/sof/intel/hda-dsp.c and hda.c.
- Microsoft KMDF translated-resource and PrepareHardware/ReleaseHardware docs.
- Microsoft MmMapIoSpaceEx and READ_REGISTER_ULONG documentation.
- PHASER360 H15I source and verified physical H15I result.
