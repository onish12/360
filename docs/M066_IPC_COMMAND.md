# M0.6.6 GLK IPC3 command/reply transport

This adds a polling command transport to the development WDK static library.
It is not an installer and has not run on the Lenovo. No audio endpoint, codec
configuration or playback is added by this milestone.

## Source-checked wire contract

For legacy APL/GLK IPC3, the host writes the request to the host/downlink mailbox
and writes HIPCI BUSY (0x48, bit 31), without encoding the command in that
register. Firmware writes the reply into the same host mailbox, clears BUSY,
and asserts HIPCIE DONE (0x4c, bit 30). After capturing the reply, the host
acknowledges DONE with a W1C write. HIPCT (0x40) carries firmware-initiated
notifications, not the ordinary command reply. Generic replies use command
0x10000000 and a 12-byte header including signed firmware error. The generic
reply does not echo the request command or carry a transaction sequence number.

Primary sources inspected before implementation:

- [Linux v6.12 HDA IPC send, reply and acknowledgment](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda-ipc.c)
- [Linux v6.12 HDA register definitions](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda.h)
- [Linux v6.12 IPC3 reply bounds and serialized transmission](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/ipc3.c)
- [Linux v6.12 IPC3 headers and 384-byte message limit](https://github.com/torvalds/linux/blob/v6.12/include/sound/sof/header.h)
- [SOF v1.9 generic reply creation](https://github.com/thesofproject/sof/blob/v1.9/src/ipc/ipc3/handler.c)
- [SOF v1.9 cAVS command completion](https://github.com/thesofproject/sof/blob/v1.9/src/drivers/intel/cavs/ipc.c)

These versioned sources match this project's IPC3/cAVS compatibility target;
the code is not presented as an IPC4 implementation.

## Ownership, API and supported subset

Ipc3Command.Bind requires a completed M065 FW_READY/window gate and copies the
validated downlink region. It performs no I/O. The caller must supply the same
live device/mapping used by that gate, exclusive ownership and serialized calls.
GlkBoot supplies these same callbacks and enables Command only after FW_READY
and successful DMA stop/release. TransferResult.commandReady separately reports
that command transport binding succeeded. Shutdown closes the command transport
before DSP power-down, even when DMA stop subsequently fails.

Exchange accepts a caller-built IPC3 request, an expected reply command and a
caller-owned output buffer. No caller pointers are retained. Request and reply
limits are 384 bytes and the validated mailbox extent. Requests need an exact
8-byte-or-larger header and DWORD alignment. Replies need at least 12 bytes.
Supported request groups: topology, component, stream, DAI and trace, with a
nonzero command type. Payload semantics and command-specific lengths remain the
caller's responsibility: this transport does not construct an audio topology.

The expected successful reply command is either the generic reply or exactly
the request command. Typed replies with other command encodings are unsupported.
A negative firmware error must use the 12-byte generic reply; it is acknowledged
and reported as FirmwareError, without output payload, and the channel remains
usable. Positive error values, malformed sizes, wrong commands and inconsistent
headers fail closed. PM commands are excluded because some power-management
replies cannot be read from a powered-down mailbox; there is no fabricated
success reply. Compound/debug/test/probe commands are also outside this subset.

No command is sent automatically after boot. The future topology owner must
build and validate each concrete command and manage any resources it references.

## Transaction sequence and failure behavior

1. Validate and copy the request before MMIO. Invalid arguments leave a usable
   channel intact and perform no I/O.
2. Verify core 0 is running and host IPC interrupts remain masked. Require
   HIPCI idle, no stale DONE, and no pending HIPCT notification.
3. Write the bounded request to the host mailbox and recheck the doorbells.
4. Publish HIPCI BUSY. Mark submitted conservatively before the write attempt:
   even a failing or deadline-expired callback can leave submission uncertain.
5. Poll for DONE with BUSY clear. Read the reply header, validate lengths and
   expected command, then capture bounded remaining bytes.
6. Recheck doorbells and header, acknowledge DONE, and verify DONE and BUSY
   clear. Only then publish a successful reply to the caller's output buffer.

The 500-ms deadline and 1,000-poll cap are explicit project policy, not an
assertion about required hardware timing. Time is checked before and after I/O;
backward and frozen clocks are bounded/rejected. A callback that never returns
cannot be bounded by this library. All-ones register reads fail; payload words
may legitimately contain all ones. No interrupt enable, power transition or
unsolicited message acknowledgment is performed.

A timeout, I/O failure, inconsistent reply, stale DONE, occupied request slot,
or pending firmware notification faults the channel. Later Exchange calls do
no I/O. There is no automatic retry or rebind; reuse needs an explicitly new
boot owner after controlled shutdown. This prevents a delayed reply to a timed-
out request from being accepted for a later request. Caller-visible output is
unchanged on failure. submitted means possibly submitted; acknowledged is set
only after the DONE-clear readback. A failed ACK/readback may still have reached
the firmware and must not be interpreted as an unexecuted command.

Notifications, including panic, are detected but not dispatched or discarded.
A notification concurrent with a command faults this limited polling channel.
This conservative limitation must be replaced by a proper notification/IRQ owner
before continuous audio streaming. Mailbox capture assumes cooperative firmware
ownership until ACK; header/doorbell rechecks do not authenticate firmware or
protect against a malicious DSP changing payload bytes.

Close only invalidates software access; it neither cancels an in-flight DSP
operation nor releases command-referenced resources. The future PnP/power and
stream owners retain responsibility for quiescence, lifetimes and teardown.

## Validation

Tests execute the pure transport against bounded MMIO models and production
Windows wrapper code against fake WDF/register APIs. Coverage includes repeated
valid commands, immediate replies, maximum-sized messages, firmware errors,
stale DONE, busy/notification states, malformed headers, positive error values,
all-ones registers, backward/frozen clocks, both sides of the deadline, dropped
ACKs, changed headers, and every successful-path I/O failure point with both
pre-write and posted-write failure behavior. Output remains unchanged on failed
transactions. The Windows model exercises pre-boot/post-shutdown/IRQL gates,
a successful command and rejection of reuse after timeout.

Remaining: notification dispatch/interrupt ownership, authenticated image owner,
PnP/power integration, command-specific topology/DAI/codec configuration, stream
DMA and WaveRT endpoints. WDK compilation is distinct from hardware validation.

## Verified build evidence

Implementation commit: `4e1a78e321f23da0e4bb72d94a60b7503e42f4ca`.
Implementation tree: `44f16eab80795221a8b5a2675ca1ed6e56f389c6`.
Builds completed on 2026-09-18; final evidence checked on 2026-09-19.

- [Real WDK/KMDF build](https://github.com/onish12/360/actions/runs/35370440869): PASS, job 105682870170; seven host test executables also passed.
- [Windows and Linux verification](https://github.com/onish12/360/actions/runs/35370440819): PASS; Windows 11 test executables (job 105682870092), Linux ASan/UBSan 10 (job 105682870439). The hash-pinned firmware/XMan checks also passed.
- Command model: 70,243 assertions, including 30 I/O failure points with pre-write and posted-write failure behavior.
- Integrated Windows API/MMIO model: 77,634 assertions. These are assertions in simulated tests, not physical-device trials.
- [Development artifact](https://github.com/onish12/360/actions/runs/35370440869/artifacts/10557946850): `PHASER360_M066_WDK_IPC_COMMAND`, 123,311 bytes.
- GitHub-reported archive SHA-256: `65ee219f4e0c0ee187a1aeafd4cd1ec691254bcbec02967d83519dd80ab58dfc`. The archive was not independently downloaded and hashed in this session.

The artifact contains a static library and source/documentation, not an audio
installer. Runtime tests use simulated APIs/registers; hardware execution and
sound remain unverified. This later documentation update changes no tested code.
