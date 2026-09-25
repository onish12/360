# M0.6.15 H15F — function-driver ownership handoff

Purpose: validate the PnP ownership transition from the verified Intel baseline
to a PHASER360 function driver and back, before any M1 hardware mutation.

H15F deliberately contains no hardware-access implementation. Its only runtime
work is to record framework lifecycle transitions and inspect the translated
CM_RESOURCE_LIST metadata supplied by KMDF.

Success while H15F owns DEV_3198 requires:
- exact function-driver bind to PCI\\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06;
- PrepareHardware observed;
- exactly two translated memory resources;
- memory lengths 0x4000 and 0x100000 in the already verified HDA/DSP order;
- exactly one translated interrupt resource;
- D0Entry observed;
- read-only software snapshot available through a non-power-managed IOCTL.

The source and import gates forbid:
- MmMapIoSpace / MmMapIoSpaceEx / MmUnmapIoSpace;
- READ_REGISTER_* / WRITE_REGISTER_*;
- BUS_INTERFACE_STANDARD / GetBusData / SetBusData;
- WdfInterruptCreate;
- WDF DMA/common-buffer allocation;
- firmware loading or DSP boot;
- audio endpoint/playback code.

Deployment is intentionally forced because Microsoft documents that PnPUtil
will not force a lower-ranked driver onto a device. The runner first publishes
the signed H15F package, then calls UpdateDriverForPlugAndPlayDevicesW with
INSTALLFLAG_FORCE against the exact full hardware ID. Rollback deletes only the
published H15F OEM INF and requires the device to return to the exact preflight
Intel identity.

Live baseline pinned from the verified H15E transaction:
- Windows build 19044;
- service IntcAudioBus;
- Intel INF oem14.inf;
- driver version 9.22.0.4832;
- provider Intel(R) Corporation;
- HDA resource base 0xCEEE0000, length 0x4000;
- DSP resource base 0xCEF00000, length 0x100000.

H15F does not authorize DSP boot. It isolates function-driver ownership and
rollback as a separate physical gate.


## Verified physical Lenovo execution — 2026-09-23

Physical transaction:
`RESULT_H15F_HANDOFF_20260923_153658_255f9fa9.zip`

Archive SHA-256:
`659d13dc7ba35dab8d23c1b525a5a8e44a524af70c11d1dd0e6974839d4ff0e7`

Integrity and ownership evidence:
- all 13 entries listed in `SHA256SUMS.txt` independently re-hashed successfully;
- H15F became the active function driver:
  service `Phaser360H15f`, INF `oem29.inf`, version `0.6.15.181`,
  provider `PHASER360 Experimental`, status `OK`, problem code `0`;
- framework snapshot: version `1`, size `96`, flags `0x000003FF`,
  last status `0x00000000`;
- `PrepareCount=1`, `D0EntryCount=1`;
- raw resources `5`, translated resources `5`;
- translated memory resources `2`, interrupt resources `1`;
- HDA resource `0xCEEE0000 / 0x4000`;
- DSP resource `0xCEF00000 / 0x100000`;
- interrupt flags `0x00000000`;
- transaction reported `HANDOFF_COMPLETED=TRUE`,
  `SNAPSHOT_COMPLETED=TRUE`, `BASELINE_RESTORED=TRUE`,
  `TRUST_RESTORED=TRUE`;
- final Intel baseline returned to `IntcAudioBus`, `oem14.inf`,
  version `9.22.0.4832`, provider `Intel(R) Corporation`, status `OK`;
- `target_before.json` and `target_after.json` are byte-identical.

This closes H15F for physical function-driver ownership and exact rollback.
It proves that KMDF delivers the expected controller resources and D0 lifecycle
to PHASER360 while it owns DEV_3198. It does not prove any MMIO mutation, PCI
configuration write, interrupt handling, DMA, firmware load, DSP boot or audio.
