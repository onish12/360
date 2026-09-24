# M0.6.3 HDA stream transport

This extends the WDK static library with actual Windows register access and
integration with BootDma. It is not an installable driver and is not linked into
the read-only M0.5.1 probe. There is no DriverEntry, INF, ROM power sequencer,
firmware authentication, IPC mailbox service, PortCls or audio endpoint.

## Implemented

BootStream discovers the first output stream from live GCAP and discovers PP
and SPIB capabilities through the LLCH chain. Reads are bounded; cycles,
duplicate required capabilities, overlapping capability regions and unsupported
stream layouts are rejected. Capability pointers must be DWORD-aligned, lie
beyond all stream descriptors, remain inside the mapped HDA BAR, and the walk
is capped at 32 unique entries. The former project-only 0x400..0x0fff ceiling
was removed after the physical Lenovo H15M capture proved the valid chain
0x0c00/ID2 -> 0x0800/ID3 -> 0x0500/ID1 -> 0x1f00/ID5 -> 0x0700/ID4.

Selection performs no writes. It requires exclusive cold-controller ownership:
all streams stopped/unbound, stream interrupts and SSYNC clear, position-buffer
DMA disabled, GCTL enabled, GPROCEN enabled and Intel EM2 L1SEN disabled. These
platform prerequisites must be established by a future power/PnP owner, not by
this component. No historical BAR address is used.

Configuration resets the selected stream and polls reset acknowledgement. It
uses the APL/GLK PROCEN format quirk: couple the stream, write format 0x40, then
decouple it. It programs tag 1, CBL, LVI, the 32-bit BDL address and SPIB. Readback
must match before the state becomes Prepared. The known payload size is 286720
bytes with LVI 69. This code accepts only the page-split layout from M0.6.2.

Start sets RUN with stream interrupt enables clear. This is a polling transport;
observing RUN is not proof of firmware execution. The caller must initialize ROM
before Start and subsequently poll/validate ROM state. Each acknowledgement poll
has at most 1000 attempts with 10-us stalls; this is a bounded poll budget, not a
promise of exact elapsed time. Reset exit also has a 3-us delay.

StopDetach first clears RUN and requires its acknowledgement. Failure stops
cleanup before descriptor addresses are cleared. After acknowledged stop it
clears SPIB, BDL addresses, CBL/LVI and tag, restores coupling and checks readback.
IsDetached performs fresh reads; cached state alone never authorizes release.

HdaTransport uses READ_REGISTER/WRITE_REGISTER byte, word and DWORD APIs against
a caller-owned translated, resident, read/write, noncached HDA BAR mapping. It
checks access width, alignment, range and PASSIVE_LEVEL. SD_CTL and W1C SD_STS
are accessed as separate bytes, avoiding accidental status acknowledgements.
It publishes BootDma ownership before Configure can write any descriptor address,
then supplies the real stream readback verifier to BootDma::Release.

## Lifetime and validation boundaries

One HdaTransport object handles one attempt. The parent driver must serialize
all calls, keep the device powered and mapping valid, and call StopAndRelease
before object destruction, BAR unmapping or WDF parent deletion. Failed stop or
readback retains buffers. No destructor frees possibly active DMA. Surprise
removal and PnP teardown policy remain the responsibility of the future owner.

Unit tests execute production BootStream against a byte-accurate register model,
including W1C, stuck reset/RUN, dropped writes, inaccessible registers and each
I/O failure point (including writes that take effect before reporting failure).
They check the format quirk order and preserve unrelated PP bits. They do not
validate actual bus posting, physical DMA or firmware execution.

The Windows adapter is compiled with WDK/KMDF. The MMIO adapter and real hardware
are not executed by the user-mode model. No new audit or Lenovo test is requested
at this stage; running an old package would not execute this component.

## Primary sources checked before implementation

- [Linux v6.12 HDA stream setup/reset/SPIB](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda-stream.c)
- [Linux v6.12 code-loader preparation and stop behavior](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda-loader.c)
- [Linux v6.12 SOF HDA definitions](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda.h)
- [Linux v6.12 HDA register/capability layout](https://github.com/torvalds/linux/blob/v6.12/include/sound/hda_register.h)
- [Linux v6.12 APL/GLK PROCEN format quirk](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/apl.c)
- [Microsoft READ_REGISTER_ULONG](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-read_register_ulong)
- [Microsoft WRITE_REGISTER_ULONG](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-write_register_ulong)

Independent MIT implementation; Linux source was consulted, not copied.

## Recorded verification — 2026-09-18

Source: `83c711cc341d935ca13995b04d04167f7f43e042`.

- [WDK push build 35309164233](https://github.com/onish12/360/actions/runs/35309164233): real KMDF/WDK compilation passed with warnings as errors; all three transport/DMA test executables passed.
- [Windows and Linux push build 35309164157](https://github.com/onish12/360/actions/runs/35309164157): seven Windows / six Linux test executables passed; Linux used ASan/UBSan; pinned firmware reference passed.
- Stream model: 38597 assertions, including 128 injected I/O failure points. These are assertions and simulated fault points, not hardware runs.
- Artifact: `PHASER360_M063_WDK_HDA_TRANSPORT`, ID `10533040478`, 61347 bytes. GitHub-reported archive SHA-256: `509a41381452effa42a17ff03613bc6a917810d1e6447fb953f1724f70350e30`. This digest has not been independently checked after download.

The local standalone stream test also passed ASan/UBSan. Local LeakSanitizer was
disabled because the execution environment uses ptrace; the Linux CI sanitizer
run did not use that local exception. No physical HDA/DSP execution is claimed.
