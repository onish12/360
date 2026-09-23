# M0.6.15 H15H — function-driver PCI pre-firmware policy transaction

H15G proved exact function-driver ownership and read-only MMIO visibility, but
captured ADSPCS 0x001D003C and ROM status 0x01006701. That exact pair has been
reported upstream on Apollo Lake during DSP reset failure. H15H therefore does
not attempt firmware boot or MMIO mutation.

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
