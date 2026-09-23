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
