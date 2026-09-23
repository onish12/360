# M0.6.15 H15N — isolated PPCTL.GPROCEN reversible gate

H15M physically proved Processing Pipe capability at 0x0800 (header 0x00030500)
with PPCTL=0 and PPSTS=0. Linux SOF enables PPCTL.GPROCEN before pre-firmware
clock/power-gating and core power-up.

H15N requires the exact H15M capability chain, uses the already-validated
GCTL.CRST 0->1 transition, sets only PPCTL.GPROCEN bit30, and polls CPA0
read-only for at most 50 ms. CPA0 observation is diagnostic and is excluded from
the success mask. It then clears GPROCEN, proves PPCTL exact restoration, clears
GCTL.CRST and proves GCTL exact restoration.

No ADSPCS write, EM2 write, PCI config write, DMA, IRQ ownership, firmware,
DSP boot, codec programming or playback is permitted.


## Verified physical Lenovo execution — H15N, 2026-09-23

`RESULT_H15N_TRANSACTION_20260923_204407_73ab8c5d.zip`:
all 13 SHA256SUMS entries re-hashed successfully. Status
`H15N_PPCTL_GPROCEN_AND_GCTL_ROLLBACK_COMPLETE`, flags `0xFFFFEFFF`;
the only intentionally absent flag is CPA0Observed. PPCTL changed exactly
`0x00000000 -> 0x40000000 -> 0x00000000`.
CPA0 was not observed. During GPROCEN=1, the read-only DSP snapshot changed to
ADSPCS `0x00000303`, HIPCIE `0x00000000`, ROM_STATUS `0xFFFFFFFF`.
After GPROCEN clear, ADSPCS/HIPCIE/ROM_STATUS returned to
`0x001D003C / 0x00420000 / 0x01006701`. Intel binding and trust were restored.
