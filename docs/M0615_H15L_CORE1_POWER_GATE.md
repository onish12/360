# M0.6.15 H15L — reversible core1 SPA/CPA power handshake

H15K proved that ADSPCS CSTALL/CRST transitions can be written and restored on
the physical Gemini Lake target. H15L isolates power-request/acknowledge behavior.

## Why core1

The exact physical baseline is ADSPCS=0x001D003C. For the host-managed cores:
- core0: SPA=1, CPA=0;
- core1: SPA=0, CPA=0.

The core0 request/ack mismatch makes a reversible first SPA test ambiguous.
Core1 starts from a matched 0/0 state, so it supports an exact round trip.

## Transaction

Preconditions:
- exact DEV_3198 target, PCI policy and BAR layout;
- HDA controller idle: CORBCTL.RUN=0, RIRBCTL.DMA_EN=0, 13 stream RUN bits=0;
- exact ADSPCS=0x001D003C.

Only ADSPCS is writable and only core1 CSTALL, CRST and SPA fields are allowed:
1. set CSTALL1;
2. set CRST1, yielding 0x001D023E;
3. set SPA1 and poll CPA1 until set;
4. capture the powered state;
5. clear SPA1 and poll CPA1 until clear;
6. require 0x001D023E again;
7. clear CRST1;
8. clear CSTALL1;
9. require exact ADSPCS=0x001D003C.

CPA is never written. All core0 ADSPCS fields are excluded by the write helper.
HDA BAR remains PAGE_READONLY.

If SPA1 was written, reset/stall rollback is allowed only after CPA1 is proven
clear. If CPA1 cannot be proven clear, core1 remains stalled/reset and the
runner retains the package/trust for recovery instead of unstalling an unknown
power state.

The runner records exact hardware restore before broad parser validation and
uses exact AddService matching for published packages.

No HDA MMIO write, PCI config write, DMA, IRQ ownership, firmware, ROM purge,
DSP run/unstall, codec programming or playback is authorized.

Public Linux SOF reference behavior:
- power-up sets SPA and polls CPA;
- power-down clears SPA and polls CPA clear;
- stall/reset is performed before core power-down.

Passing H15L proves only the reversible core1 power handshake. Core0 power state
remains untouched and requires a later separately designed gate.
