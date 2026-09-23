# M0.6.15 H15I — isolated HDA GCTL.CRST transaction

H15H proved that the bounded PGCTL/CGCTL policy applies and restores exactly,
but does not alter the observed HDA/DSP register state.

H15I isolates the next production-order mutation: HDA controller CRST.
The exact live H15G/H15H baseline requires GCTL=0x00000000 while PHASER360 is
the function driver.

The one-shot D0 transaction:
1. performs read-only PCI attestation and requires PGCTL=0x10 / CGCTL=0x807B0DFF;
2. requires the exact H15H register baseline;
3. maps HDA BAR PAGE_READWRITE and DSP BAR PAGE_READONLY;
4. waits 500 us while HDA is already in reset;
5. writes only GCTL bit 0 from 0 to 1 and polls CRST=1;
6. waits 1000 us and captures HDA/DSP registers;
7. writes only GCTL bit 0 back to 0, polls CRST=0 and captures again;
8. requires the full GCTL dword to match the original before success.

There is no PCI config write, no DSP BAR write, no ADSPCS write, no DMA,
no WDF interrupt ownership, no firmware load, no DSP boot and no playback.
