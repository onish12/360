# M0.6.15 H15O — staged pre-firmware power-policy gate

H15N proved GPROCEN alone latches and rolls back, but CPA0 remains clear.
While GPROCEN was set, DSP BAR reads changed from the stable baseline
ADSPCS=0x001D003C / ROM_STATUS=0x01006701 to ADSPCS=0x00000303 /
ROM_STATUS=0xFFFFFFFF, then returned when GPROCEN was cleared.

Linux SOF enables Processing Pipe during probe, then before firmware run calls
hda_dsp_ctrl_clock_power_gating(false): clear CGCTL.ADPSDCGE bit1, clear
EM2.L1SEN bit13, and set PGCTL.ADSPPGD bit2. H15O reproduces those conditions
one at a time, taking a read-only DSP/HDA snapshot after each step.

There is no ADSPCS or other DSP BAR write. CPA0 is diagnostic and excluded from
the required success mask. Rollback restores CGCTL, EM2, PGCTL, GPROCEN and
GCTL exactly, and proves the complete 256-byte PCI config image equals baseline.
