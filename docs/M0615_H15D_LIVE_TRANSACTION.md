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
