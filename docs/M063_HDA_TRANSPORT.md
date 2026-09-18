# M0.6.3 HDA stream transport

This extends the WDK static library with actual Windows register access and
integration with BootDma. It is not an installable driver and is not linked into
the read-only M0.5.1 probe. There is no DriverEntry, INF, ROM power sequencer,
firmware authentication, IPC mailbox service, PortCls or audio endpoint.

## Implemented

BootStream discovers the first output stream from live GCAP and discovers PP
and SPIB capabilities through the LLCH chain. Reads are bounded; cycles,
duplicate required capabilities, overlapping capability regions and unsupported
stream layouts are rejected. The conservative capability window is 0x400..0xfff
and must also lie beyond all stream descriptors; this is a project policy,
not an assertion that every HDA controller uses that layout.

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
