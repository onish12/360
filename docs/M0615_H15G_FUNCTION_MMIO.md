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

A successful H15G result proves that the same BARs previously observed by H15E remain readable for the full PrepareHardware -> D0 ownership lifetime while PHASER360 is the function driver. It still does not authorize any hardware mutation.


## Verified physical Lenovo execution — 2026-09-23

Physical transaction:
`RESULT_H15G_TRANSACTION_20260923_161222_5ebe795a.zip`

Archive SHA-256:
`2b62c9eff2117835453895b9382480a5858134ffd56f74c2bcfcaf0683c7ed64`

Integrity and rollback:
- all 13 entries listed in `SHA256SUMS.txt` independently re-hashed successfully;
- `target_before.json` and `target_after.json` are byte-identical;
- H15G was active as `Phaser360H15g`, `oem29.inf`, version `0.6.15.190`,
  provider `PHASER360 Experimental`, status `OK`, problem code `0`;
- final Intel baseline returned exactly to `IntcAudioBus`, `oem14.inf`,
  version `9.22.0.4832`, provider `Intel(R) Corporation`, status `OK`;
- `HANDOFF_COMPLETED=TRUE`, `SNAPSHOT_COMPLETED=TRUE`,
  `BASELINE_RESTORED=TRUE`, `TRUST_RESTORED=TRUE`.

Framework/resource evidence:
- snapshot version `1`, size `120`, flags `0x00001FFF`,
  last status `0x00000000`;
- `PrepareCount=1`, `D0EntryCount=1`;
- raw resources `5`, translated resources `5`;
- memory resources `2`, interrupt resources `1`;
- HDA `0xCEEE0000 / 0x4000`;
- DSP `0xCEF00000 / 0x100000`.

Read-only D0 register snapshot:
- HDA GCAP `0x6701`;
- HDA VMIN/VMAJ `0x00/0x01`;
- HDA GCTL `0x00000000`;
- Intel EM2 `0x04007000`;
- DSP ADSPCS `0x001D003C`;
- DSP ADSPIS `0x00000000`;
- DSP HIPCI `0x00000000`;
- DSP HIPCIE `0x00420000`;
- DSP ROM status `0x01006701`.

The exact `ADSPCS=0x001D003C` and `ROM status=0x01006701` pair has
also been reported by upstream SOF on Apollo Lake during a DSP reset failure
state. H15G therefore closes function-driver read-only MMIO visibility, but the
captured state is not treated as a valid cold-ROM boot precondition.

The next physical gate must not jump directly to firmware boot. It should apply
only the already-attested H15D PCI pre-firmware policy while PHASER360 owns the
function, read the same MMIO registers without writing them, then restore the
full PCI configuration exactly.
