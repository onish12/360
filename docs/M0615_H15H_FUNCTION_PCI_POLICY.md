# M0.6.15 H15H — function-driver PCI pre-firmware policy transaction

H15G proved exact function-driver ownership and read-only MMIO visibility, but captured ADSPCS 0x001D003C and ROM status 0x01006701. That exact pair has been reported upstream on Apollo Lake during DSP reset failure. H15H therefore does not attempt firmware boot or MMIO mutation.

H15H changes exactly one dimension relative to H15G:
- BAR0 and BAR4 remain PAGE_READONLY | PAGE_NOCACHE;
- no WRITE_REGISTER_* exists;
- after an explicit one-shot IOCTL in D0, the already-reviewed H15D PCI policy
  clears CGCTL bit 1 and sets PGCTL bit 2;
- the full 256-byte PCI config image is attested before, after apply and after
  restore;
- the same HDA/DSP register set is read before policy, while policy is applied,
  and after restore;
- the policy is restored synchronously before the IOCTL returns.

No DMA, WDF interrupt ownership, firmware loading, DSP boot or playback is
permitted. Success requires full PCI config restoration byte-for-byte.


## Verified physical Lenovo execution — 2026-09-23

Physical transaction:
`RESULT_H15H_TRANSACTION_20260923_164000_2b81083d.zip`

Archive SHA-256:
`bb47a8691ac6fb1069e1abf1809fcfbbadd5415ce7528cda1f67c4b856634074`

Integrity and rollback:
- all 13 entries listed in `SHA256SUMS.txt` independently re-hashed successfully;
- `target_before.json` and `target_after.json` are byte-identical;
- H15H was active as `Phaser360H15h`, `oem29.inf`, version `0.6.15.200`,
  provider `PHASER360 Experimental`, status `OK`, problem code `0`;
- final Intel baseline returned exactly to `IntcAudioBus`, `oem14.inf`,
  version `9.22.0.4832`, provider `Intel(R) Corporation`, status `OK`;
- `HANDOFF_COMPLETED=TRUE`, `WRITE_RESTORE_COMPLETED=TRUE`,
  `SNAPSHOT_COMPLETED=TRUE`, `BASELINE_RESTORED=TRUE`,
  `TRUST_RESTORED=TRUE`.

PCI transaction:
- before: PGCTL `0x00000010`, CGCTL `0x807B0DFF`;
- applied: PGCTL `0x00000014`, CGCTL `0x807B0DFD`;
- restored: PGCTL `0x00000010`, CGCTL `0x807B0DFF`;
- flags `0x0001FFFF`, NTSTATUS `0x00000000`.

MMIO remained exactly unchanged at all three observation points
(before / PCI-applied / PCI-restored):
- HDA GCAP `0x6701`, VMIN/VMAJ `0x00/0x01`,
  GCTL `0x00000000`, Intel EM2 `0x04007000`;
- DSP ADSPCS `0x001D003C`, ADSPIS `0x00000000`,
  HIPCI `0x00000000`, HIPCIE `0x00420000`,
  ROM status `0x01006701`.

Therefore the bounded H15D PCI pre-firmware policy is necessary to reproduce
the reviewed SOF policy but is not, by itself, sufficient to change the
observed DSP state. The next isolated hardware mutation should be the standard
HDA GCTL.CRST reset/ready transition, with DSP MMIO remaining read-only and
with exact HDA GCTL restoration before returning to Intel.
