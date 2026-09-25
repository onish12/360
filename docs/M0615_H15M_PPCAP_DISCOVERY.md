# M0.6.15 H15M — HDA Processing Pipe capability discovery

H15L R3 physically proved that a direct core1 SPA1 request never appears in ADSPCS,
even immediately after the write. The live baseline remains ADSPCS=0x001D003C:
for the public Intel/SOF field layout, core0 SPA is already requested while CPA0
is not acknowledged.

Before changing another DSP bit, H15M measures a missing pre-firmware condition.
Linux SOF enumerates the HDA extended capability chain, locates Processing Pipe
capability ID 3 and enables PPCTL.GPROCEN before firmware run. H15M does not enable it.

The transaction:
1. requires exact Intel/PCI/HDA/DSP baseline and idle HDA transport;
2. uses only the already-validated H15I GCTL.CRST 0->1 transition;
3. while HDA is ready, reads LLCH and walks at most 12 linked capabilities;
4. records every capability header and, if ID 3 is found, reads PPCTL and PPSTS;
5. reports GPROCEN bit30 and PIE bit31 without writing them;
6. restores only GCTL.CRST to its exact original value.

DSP BAR is PAGE_READONLY. There is no PPCTL write, EM2 write, PCI config write,
ADSPCS write, DMA, IRQ ownership, firmware, DSP boot or playback.


## Verified physical Lenovo execution — 2026-09-23

`RESULT_H15M_TRANSACTION_20260923_203154_b336fb8a.zip`: all 13 manifest
hashes verified. Status `H15M_PPCAP_DISCOVERY_AND_GCTL_ROLLBACK_COMPLETE`,
flags `0x07FFFFFF`, NTSTATUS `0x00000000`.
Capability chain: `0x0C00/ID2 -> 0x0800/ID3 -> 0x0500/ID1 -> 0x1F00/ID5 -> 0x0700/ID4`.
PP offset `0x0800`, header `0x00030500`, PPCTL/PPSTS both zero,
therefore GPROCEN=0 and PIE=0. GCTL and Intel binding restored exactly.
