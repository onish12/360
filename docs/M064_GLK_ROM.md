# M0.6.4 GLK DSP power and ROM handshake

This adds a real-register Windows implementation of the APL/GLK cold-boot
sequence to the existing static library. It remains a development component,
not an installable driver. No code is linked into the read-only M0.5.1 probe.

## Sequence and ownership

GlkRom binds a caller-owned DSP register interface without writes. PowerDown
masks IPC interrupts, verifies the mask, stalls and resets both host-managed
cores, clears their power requests and waits for actual power removal.
Initialize requires that cold state and an idle HIPCI request, then:

1. Clears stale HIPCIE DONE using its W1C bit and verifies it is clear.
2. Powers both cores and waits for both CPA bits.
3. Sets all six APL/GLK SSP SSC1 clock/frame-consumer bits. R8 candidate records
   one readback per port without treating echo of these configuration bits as
   a ROM acknowledgement. I/O failures and all-ones reads still stop boot.
4. Issues cold ROM_CONTROL/PURGE with stream-tag-minus-one encoding.
5. Releases reset/stall on core 0 and checks SPA/CPA/reset/stall together.
6. Waits for a fresh DONE, acknowledges it and verifies its clearing.
7. Stalls/resets/powers down core 1 and waits for ROM INIT_DONE (state 1).

WaitEntered then waits for ROM state 5 after the caller starts HDA DMA. HALTED,
inaccessible all-ones registers, I/O failures and expired deadlines fail closed.
It never accepts HALTED merely because the low state bits equal 1 or 5.

This is explicitly a polling implementation. Unlike the Linux interrupt-driven
path, it leaves IPC interrupts masked; it does not implement FW_READY reception
or operational IPC. FirmwareEntered is not evidence of a functioning endpoint,
validated FW_READY header, sound, or successful real-hardware boot.

The Windows GlkBoot wrapper combines HdaTransport and GlkRom. It requires
caller-approved firmware payload bytes, exclusive ownership of the exact GLK
device, translated resident noncached read/write BAR mappings, D0 and the
existing HDA platform prerequisites. It does not identify/bind PCI devices,
authenticate an image, strip XMan, initialize GCTL/GPROCEN or manage D0i3/L1.

HDA preparation must succeed before any DSP mutation. Its verified stopped
stream state permits initial DSP power-down. A failed HDA preparation cannot
cause DSP writes even through Shutdown. Following Transfer, HDA stop/release is
attempted on both success and failure; result fields distinguish firmware entry
from DMA release. Shutdown refuses DSP writes when HDA stop fails and preserves
the primary ROM error separately from its cleanup return value. A failed stop
retains WDF buffers. The caller must resolve this before parent destruction or
BAR unmapping; automatic WDF parent teardown is not prevented by this library.

## Time and error handling

Polling uses a monotonic clock, an elapsed-time deadline and a separate attempt
cap for a frozen clock callback. The Windows implementation uses
KeQueryInterruptTime (100-ns units converted to microseconds) and nonalertable
KernelMode KeDelayExecutionThread waits, at PASSIVE_LEVEL. A backward clock or
failed wait is rejected. The deadlines are checked before and after reads.
Actual scheduling/clock granularity can exceed the requested wait; no exact
wall-clock completion guarantee is claimed.

Reference budgets: reset/power transitions 50 ms, ROM IPC acknowledgement
500 ms, GLK INIT_DONE 150 ms, firmware entry 3000 ms. Poll waits request 500 us.
These are per-stage limits, not one aggregate boot deadline. No automatic
cold-boot retry or firmware reset is performed after an error.

## Verification boundaries

The production GlkRom logic is tested against a register model, including every
I/O failure point, posted-write failures, stale DONE, missing acknowledgements,
power timeout, halted ROM, backward/frozen clocks and inaccessible registers.
A separate test executes the production Windows GlkBoot/HdaTransport/BootDma
sources together using fake WDF and register APIs: successful transfer, failed
HDA preparation, ROM timeout, stuck HDA RUN, halted firmware, failed HDA start,
and incorrect IRQL. It verifies buffer retention and no DSP writes on an
unconfirmed HDA stop. These tests are simulations, not kernel/hardware runs.

Real Windows APIs are compiled separately with WDK/KMDF, warnings as errors.
Pending: PnP/power owner and platform initialization, firmware trust enforcement,
FW_READY/IPC windows and topology/codec/WaveRT integration. No new Lenovo audit
or execution is requested by this milestone.

## Primary sources checked before implementation

- [Linux v6.12 hda-loader.c: cl_dsp_init and SSP setup](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda-loader.c)
- [Linux v6.12 hda-dsp.c: power/reset/stall and IPC masking](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda-dsp.c)
- [Linux v6.12 hda.h: register offsets, masks and timeouts](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda.h)
- [Linux v6.12 apl.c: core masks and ROM timeout](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/apl.c)
- [Linux v6.12 pci-apl.c: GLK uses apl_chip_info](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/pci-apl.c)
- [Microsoft KeDelayExecutionThread](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kedelayexecutionthread)
- [Microsoft KeQueryInterruptTime](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-kequeryinterrupttime)

Independent MIT implementation; Linux code was consulted, not copied.

## Recorded verification — 2026-09-18

Source revision: `8a63c6303f5454a4451a126d8bd765183b251b4f`.

- [WDK push run 35310059338](https://github.com/onish12/360/actions/runs/35310059338): real WDK/KMDF library compiled, warnings as errors; all five selected test executables passed.
- [Windows/Linux push run 35310059319](https://github.com/onish12/360/actions/runs/35310059319): nine Windows / eight Linux tests passed, including the pinned firmware reference. Linux used ASan/UBSan.
- New ROM model: 38814 assertions and 60 injected I/O failure points. New Windows-wrapper integration model: 8475 assertions. These are assertions in simulations, not hardware measurements.
- CI artifact `PHASER360_M064_WDK_GLK_ROM`, ID `10532863215`, 91238 bytes. GitHub-reported archive digest: `sha256:be27e5b331a684a8c56400c6928f77df2ca2a67525e345b62ab9bdfa77ae6d3d`. No independent archive download/hash verification is claimed.

Local standalone model tests passed ASan/UBSan with LeakSanitizer disabled for
the ptrace-based local environment. CI did not need that local exception.
