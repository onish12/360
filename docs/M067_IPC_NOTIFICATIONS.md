# M0.6.7 IPC3 notification capture and FIFO delivery

Development static library only. No hardware execution or audio playback.
This extends M066: supported notifications no longer fault a command merely
because HIPCT BUSY appears concurrently with its HIPCIE DONE response.

## Source-checked format and supported subset

The firmware uplink is separate from the host request/reply mailbox. HIPCT
BUSY announces a message; host W1C acknowledgment permits firmware to publish
the next one. Doorbell bit 31 is BUSY, so compare message commands with that bit
masked: trace command 0x90020000 already contains bit 31. Do not compare the
entire command to the doorbell with BUSY removed.

Supported messages are deliberately narrow:

| Message | Command excluding component ID | Exact bytes |
| --- | --- | --- |
| Stream position | 0x600a0000 | 76 |
| Stream XRUN | 0x60090000 | 76 |
| Trace DMA position | 0x90020000 (no component ID) | 24 |

Stream messages require comp_id equal to the command's low 16 bits. All three
require the embedded reply error field to be zero. Remaining payload bytes are
preserved, not interpreted as pointers, DMA addresses or trusted counters.
Unknown messages, malformed envelopes, nonzero errors, ID mismatches and changed
headers fault the transport without acknowledgment. Component/control events,
panic decoding and duplicate FW_READY handling are not implemented. Panic-like
or other unknown doorbells cannot be mistaken for supported messages through a
stale mailbox: command/doorbell consistency is checked before acknowledgment.

Primary sources checked before implementation:

- [Linux v6.12 receive dispatch and envelope bounds](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/ipc3.c)
- [Linux v6.12 HDA IPC doorbells and W1C acknowledgment](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda-ipc.c)
- [Linux v6.12 IPC3 message commands](https://github.com/torvalds/linux/blob/v6.12/include/sound/sof/header.h)
- [Linux v6.12 packed stream-position structure](https://github.com/torvalds/linux/blob/v6.12/include/sound/sof/stream.h)
- [Linux v6.12 trace-position structure](https://github.com/torvalds/linux/blob/v6.12/include/sound/sof/trace.h)
- [SOF v1.9 stream and trace notification construction](https://github.com/thesofproject/sof/blob/v1.9/src/ipc/ipc3/helper.c)

## Ownership and failure contract

Ipc3Command.Bind copies and bounds the uplink from the completed M065 gate,
including a non-overlap check against the downlink. The command's register
checks now drain supported notifications before reading fresh DONE/request
status, including pre-publication, reply waiting and reply validation. No other
code or callback runs inside capture. Four fixed-size FIFO slots avoid dynamic
allocation and unbounded processing.

PollNotifications runs a bounded drain while idle and verifies running core 0
and masked interrupts. It does not wait for an event. PopNotification performs
no I/O, preserves FIFO order and also works after a transport fault so retained
captures remain available. The Windows GlkBoot wrappers enforce PASSIVE_LEVEL
and the existing boot gate. Close/Shutdown discard the logical queue and disable
access before power-down/unmapping. All operations require caller serialization;
there is no concurrent ISR/DPC consumer or internal lock in this component.

Each message is fully captured and validated, then the header and doorbell are
rechecked. The slot becomes visible in the FIFO before the ACK attempt. If the
write or its completion is uncertain, the transport faults and retains the
capture with ackAttempted=true and acknowledged=false. A successful ACK callback
sets acknowledged=true; this does not prove firmware consumed the acknowledgment.
A failed subsequent register read can fault the transport while retaining an
acknowledged capture. Failed capture before ACK produces no queued partial event.

Do not require BUSY to clear after ACK: the next message may already be pending,
including an identical notification. A silently ignored hardware write cannot
be distinguished from a repeated identical message by this protocol; a stuck
BUSY therefore eventually reaches the queue limit and faults rather than looping.
When all four slots are occupied and another message is pending, QueueFull
faults the transport, preserves queued messages and does not ACK the pending
one. Dequeuing after a fault does not reactivate the channel. No event is silently
overwritten or coalesced. The existing 500-ms deadline covers notification work
during an exchange; independent idle polling gets its own 500-ms deadline. The
four-slot limit also bounds processing with a frozen clock.

This is capture and caller-driven FIFO delivery, not operating-system period
notification or audio recovery. The caller must process position/XRUN/trace
payloads before those can drive WaveRT behavior. IRQ setup, component/control
notifications, panic diagnostics, topology/codec configuration and stream DMA
remain outstanding. No installer or hardware test is added.

## Tests

The MMIO model covers all three message types, trace command bit 31, simultaneous
reply and notification, FIFO wrap/order, four-message bursts, fifth-message
backpressure, malformed size/command/error/component ID, header change, late
capture, Close, and every successful notification-path I/O failure point with
both pre-write and posted-write behavior. Existing command/timeout/DMA tests
remain in place. Production Windows wrapper code also runs a polling/pop cycle
against fake WDF/MMIO. These tests establish model behavior, not Lenovo operation.
