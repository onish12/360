# M0.6.15 H15L R2 — reversible core1 SPA/CPA-only handshake

H15L R1 failed before CRST1 or SPA1 were reached. The live result was
Flags=0xFFFFB01F and NTSTATUS=0xC0000182: CSTALL1 was written but never observed.
The same transaction proved exact restoration to ADSPCS=0x001D003C.

The exact baseline has core1 SPA1=0 and CPA1=0. H15L R2 therefore isolates the
power-request/acknowledge pair and removes CSTALL1/CRST1 from the write scope.

## Transaction

Preconditions:
- exact DEV_3198 target and exact PCI policy;
- HDA idle: CORBCTL.RUN=0, RIRBCTL.DMA_EN=0, all 13 stream RUN bits=0;
- exact ADSPCS=0x001D003C.

Only ADSPCS SPA1 may be written:
1. set SPA1;
2. verify SPA1 readback;
3. poll CPA1 until set;
4. require powered ADSPCS=0x021F003C;
5. clear SPA1;
6. verify SPA1 clear;
7. poll CPA1 until clear;
8. require depowered ADSPCS=0x001D003C;
9. read once more and require final ADSPCS=0x001D003C.

CPA1 is read-only. CSTALL and CRST are not written. All core0 ADSPCS fields are
excluded by the write helper. HDA BAR remains PAGE_READONLY.

The poll interval is 500 us for up to 100 iterations (50 ms).

No HDA MMIO write, PCI config write, DMA, IRQ ownership, firmware load, DSP
boot/run, codec programming or playback is authorized.

Public SOF behavior powers a core by setting SPA and polling CPA before the run
sequence. H15L R2 tests only that reversible power handshake. A later gate may
test reset/stall only after this power handshake is proven.
