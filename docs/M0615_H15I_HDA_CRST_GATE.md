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


## Verified physical Lenovo execution — 2026-09-23

Physical transaction:
`RESULT_H15I_TRANSACTION_20260923_165534_ddba961e.zip`

Archive SHA-256:
`3b3d33bbf100bbddd236aafc6739b18831401df82a680d59ff9872458c8a0c5e`

All 13 entries in `SHA256SUMS.txt` independently re-hashed successfully.
`target_before.json` and `target_after.json` are identical. H15I was active
as `Phaser360H15i` / `oem29.inf` / version `0.6.15.210`; final binding
returned to `IntcAudioBus` / `oem14.inf` / `9.22.0.4832`, status OK,
problem code 0. Handoff, write restoration, snapshot, baseline restoration and
trust restoration all reported TRUE.

Register sequence:
- before: GCTL `0x00000000`, ADSPCS `0x001D003C`, ADSPIS `0x00000000`,
  HIPCI `0x00000000`, HIPCIE `0x00420000`, ROM `0x01006701`;
- ready: GCTL `0x00000001`, ADSPCS unchanged, ADSPIS `0x00040000`;
- restored: GCTL `0x00000000`, ADSPCS unchanged, ADSPIS `0x00040000`.

H15I did not write the DSP BAR. The ADSPIS change is therefore recorded as a
hardware-side observation and is not assigned undocumented semantics.
