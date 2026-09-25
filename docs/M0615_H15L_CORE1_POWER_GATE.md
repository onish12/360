# M0.6.15 H15L R3 — SPA1 write/readback telemetry

Physical H15L R2 evidence:
- before ADSPCS=0x001D003C;
- SPA1 write helper returned success;
- SPA1 was never observed set during the 50 ms poll;
- CPA1 was therefore not meaningfully tested;
- rollback was exact to 0x001D003C and Intel binding/trust were restored.

R3 does not broaden the write surface. It writes only SPA1, captures the complete
read-only observation immediately after the write and at +10 us, +100 us and
+500 us, then clears SPA1 and proves exact rollback.

A successful R3 means telemetry and rollback succeeded. It does not require SPA1
or CPA1 to assert. This distinguishes a completely ignored write from a short
transient that the R2 500-us poll cadence could miss.

No CSTALL/CRST write, Core0 write, CPA write, HDA MMIO write, PCI write, DMA,
IRQ ownership, firmware, DSP boot/run, codec programming or playback is allowed.
