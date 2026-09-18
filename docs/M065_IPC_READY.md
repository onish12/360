# M0.6.5 GLK FW_READY and IPC window gate

Development static library only; no installable driver or audio playback. This
extends M064. Firmware entry alone is still not success. No Lenovo execution
has been performed. Existing M051 diagnostic and recovery packages are untouched.

## Implementation

The reviewed SOF 1.9 cAVS firmware writes only the 108-byte FW_READY record into
the DSP uplink mailbox. Window descriptors are in XMan, not appended at runtime.
The hash-pinned SOF 1.9.3 image in tests/fixtures/sof_glk_reference.json confirms
seven windows, a 400-byte window record, and zero element header sizes. Its
XMan window element is padded to 416 bytes. Fixed FW_READY mailbox fields are
not interpreted as MMIO addresses.

ParseIpc3Windows validates an exact XMan slice (not a full firmware image),
bounds all elements, rejects duplicate window records and region types,
unsupported flags/types, bad sizes/alignment, arithmetic overflow, overlaps,
and regions outside a 128-KiB window or the supplied DSP BAR. The supported
initial uplink is 0x81000. Uplink and downlink must exist; optional regions are
validated but not accessed. It accepts element header size zero (upstream 1.9)
or 24. Output is zero on failure. This is a deliberately narrow GLK profile,
not a generic parser for all future SOF ABIs.

GlkBoot.Prepare now requires XMan bytes and an ABI minor ceiling in addition to
the payload. BOTH must belong to the SAME caller-authenticated image. Neither
structural parser authenticates firmware or enforces that binding. The future
image owner must enforce it. Invalid XMan fails before WDF allocation or MMIO
writes. The manifest is parsed into owned value data; input pointers are not
retained. Use ABI ceiling 20 for the currently pinned reference.

Ipc3Receive is configured without hardware access. Arm runs only after cold
power-down, checks both core power/reset/stall and masked IPC interrupts, then
clears stale HIPCT BUSY (W1C) and verifies clear. A pre-existing FW_READY can
therefore never satisfy a later boot. The Windows wrapper arms before ROM
initialization and calls Receive only after ROM entry and confirmed HDA DMA
stop/release. A failed DMA stop causes no IPC acknowledgment and retains buffers.

Receive waits for a fresh legacy GLK FW_READY doorbell, reads exactly 108 bytes
at 0x81000, validates the fixed header/ABI/reserved fields, rechecks the doorbell,
and sends the host W1C acknowledgment. No descriptor-controlled MMIO reads or
writes occur. It uses a five-second monotonic deadline, checks time again after
capture, rejects a backward clock, and caps polling at 10,000 attempts for a
frozen clock. This bounds attempts, not a malfunctioning callback's wall time.
Bad/missing/late messages return errors, expose no ready/window data and receive
no success ACK. Receive and Arm are single-use. Interrupts stay masked: no ISR,
IPC command queue, notifications or audio pipeline is implemented.

After ACK, BUSY is not required to be zero: firmware may already have published
another message. A successful callback records the ACK write, not a hardware
proof that firmware consumed it. MMIO callbacks and caller serialization are
trusted. Stable doorbell checks are not cryptographic protection against hostile
firmware. The Windows window accessor becomes unavailable on Shutdown.

TransferResult separately records started, firmwareEntered, dmaReleased,
ipcReady, romError and ipcError. Prepare errors can be inspected through
RomError()/IpcError(). Shutdown still preserves the primary errors and refuses
DSP power changes while DMA cannot be stopped.

## Primary sources checked before implementation

- [Linux v6.12 IPC3 receive/extended windows](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/ipc3.c)
- [Linux v6.12 legacy HDA IPC acknowledgment](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda-ipc.c)
- [Linux v6.12 HDA registers and SRAM windows](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda.h)
- [SOF v1.9 FW_READY publication and manifest windows](https://github.com/thesofproject/sof/blob/v1.9/src/platform/intel/cavs/platform.c)
- [SOF v1.9 fixed IPC3 ABI structures](https://github.com/thesofproject/sof/blob/v1.9/src/include/ipc/info.h)
- [SOF v1.9 XMan element definitions](https://github.com/thesofproject/sof/blob/v1.9/src/include/kernel/ext_manifest.h)
- [SOF v1.9 FW_READY doorbell encoding](https://github.com/thesofproject/sof/blob/v1.9/src/ipc/ipc3/handler.c)
- [SOF v1.9 cAVS mailbox mapping](https://github.com/thesofproject/sof/blob/v1.9/src/platform/intel/cavs/include/cavs/lib/mailbox.h)

## Verification and remaining integration

Host tests cover truncated/malformed manifests, invalid ranges, duplicate and
overlapping regions, absent windows, all 35 successful-path I/O failure points,
stale/missing/late FW_READY, malformed headers, changed doorbells, invalid power
or interrupt state, backward/frozen clocks, and deadline boundaries. Production
Windows wrapper code runs against fake WDF/MMIO, including failed DMA stop,
invalid manifest before writes, stale notification and shutdown visibility.
CI also tests the actual official manifest after its existing SHA-256 gate.
These models do not establish actual device behavior.

Remaining: authenticated image ownership and manifest/payload binding, PnP/D0
resource/power integration, operational IPC and notification handling, SOF
components/topology, codecs/amplifier/SSP configuration, and WaveRT endpoints.
No installation or new user-side audit is required for this development step.
