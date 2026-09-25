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

A successful H15E capture proves only that the reviewed BAR resources can be mapped read-only and that the selected register snapshot is accessible. It does not authorize M1 DSP boot by itself.


## Verified physical Lenovo execution — 2026-09-23

Physical capture:
`RESULT_H15E_LIVE_R0_TRANSACTION_20260923_055541_d186a189.zip`

Archive SHA-256:
`56d60ec1591648f0cfc5fa1f7d986f663d036d42c53caa9bf1f6c3231a0a82c3`

Integrity and rollback:
- all 12 entries listed in `SHA256SUMS.txt` independently re-hashed successfully;
- `target_before.json` and `target_after.json` are byte-identical;
- final Intel state: `IntcAudioBus`, `oem14.inf`, version `9.22.0.4832`,
  provider `Intel(R) Corporation`, status `OK`, problem code `0`;
- temporary H15E package: `oem29.inf`;
- final compound upper-filter set: empty;
- `CAPTURE_COMPLETED=TRUE`, `BASELINE_RESTORED=TRUE`,
  `TRUST_RESTORED=TRUE`.

Captured PCI/resource evidence:
- flags `0x000007FF`, capture NTSTATUS `0x00000000`;
- vendor/device `8086:3198`, header type `0x00`, first conventional
  capability `0x50`, capability count `4`;
- PGCTL `0x00000010`, CGCTL `0x807B0DFF`;
- HDA physical base `0xCEEE0000`, length `0x4000`;
- DSP physical base `0xCEF00000`, length `0x100000`.

Captured HDA registers:
- GCAP `0x6701`;
- VMIN/VMAJ `0x00/0x01` (HDA 1.0);
- GCTL `0x00000101`;
- Intel EM2 `0x04007000`.

Captured DSP registers:
- ADSPCS `0x00000303`;
- ADSPIS `0x00000000`;
- HIPCI `0x00000000`;
- HIPCIE `0x00000000`;
- ROM status `0xFFFFFFFF`.

For the APL/GLK bit layout used by this project, `ADSPCS=0x00000303`
means both cores are reset and stalled, with SPA/CPA clear. That is the exact
cold-core control state expected by the later ROM initialization precondition.
The `ROM_STATUS=0xFFFFFFFF` read occurred while DSP core power request/status
were both clear; therefore this capture does not treat it as evidence of ROM
failure. The production ROM helper still rejects `0xFFFFFFFF` once ROM-status
access is required during an active boot sequence.

This closes H15E-LIVE R0 for read-only MMIO visibility on the reviewed Lenovo.
It proves resource mapping and register visibility only; it does not authorize
concurrent MMIO writes while the Intel function driver owns DEV_3198.
