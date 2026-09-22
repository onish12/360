# M0.6.15 H15D-LIVE R1

H15D-LIVE R1 is a separate manual-only Extension upper filter. H15C remains
read-only and unchanged.

The driver hard-gates the exact previously captured target baseline:
PGCTL(0x44)=0x00000010 and CGCTL(0x48)=0x807B0DFF.

The only authorized applied state is PGCTL=0x00000014 and CGCTL=0x807B0DFD.
The filter uses PciConfigAttestation immediately before the write and the
already-tested PciConfigBootPolicy for apply/restore. A fresh attestation after
restore must recover the exact original dwords. The IOCTL is one-shot.

The gate contains no DSP/HDA MMIO path, DMA owner, firmware source, IPC path or
audio playback path. The package workflow is manual-only and never installs on
a target from CI.


## R1 hardening gates

The Extension queue remains explicitly non-power-managed because it is above
the Intel power-policy owner. Hardware transaction admission is instead gated
by EvtDeviceD0Entry/EvtDeviceD0Exit state. Device callbacks and the request
queue use device synchronization at PASSIVE_LEVEL; SurpriseRemoval remains
terminal through the shared HardwareAccessGate.

Immediately before the first SetBusData, PciConfigBootPolicy re-reads the
complete 256-byte PCI configuration image and requires byte-for-byte equality
with the attestation passed into Apply. Applied-state evidence and final
rollback evidence also use full 256-byte snapshots.


## Verified physical Lenovo execution — 2026-09-22

Physical transaction result:
`RESULT_H15D_LIVE_R1_TRANSACTION_20260922_221718_efe58948.zip`

Archive SHA-256:
`34800264bf28fe64173d23ba9a38648f5e780dd799dfbf69747976521595c004`

Observed transaction evidence:
- NTSTATUS: `0x00000000`;
- result flags: `0x000007FF` (all 11 required H15D flags set);
- vendor/device: `8086:3198`;
- PGCTL: `0x00000010 -> 0x00000014 -> 0x00000010`;
- CGCTL: `0x807B0DFF -> 0x807B0DFD -> 0x807B0DFF`;
- full 256-byte PCI applied-state readback matched the exact expected image;
- full 256-byte PCI post-restore image matched the pre-write image exactly;
- Intel target state before/after was byte-identical in the captured JSON:
  service `IntcAudioBus`, INF `oem14.inf`, version `9.22.0.4832`,
  provider `Intel(R) Corporation`, status `OK`, problem code `0`;
- H15D temporary package was published as `oem29.inf`;
- final compound upper-filter set returned to empty;
- transaction reported `WRITE_RESTORE_COMPLETED=TRUE`,
  `BASELINE_RESTORED=TRUE`, and `TRUST_RESTORED=TRUE`;
- all 12 entries in the result archive `SHA256SUMS.txt` independently
  re-hashed successfully.

This closes H15D-LIVE R1 for the reviewed Lenovo target. It proves only the
bounded PCI policy transition and exact rollback. It does not itself prove DSP
MMIO, HDA DMA, ROM entry, FW_READY/IPC, endpoint creation, codec programming or
audio playback.
