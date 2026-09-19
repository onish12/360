# M0.6.1: cold-boot coordination and IPC3 ready-header validation

This is development code, not an installable driver. No Windows DMA/MMIO/IRQ
backend exists yet. The M0.5.1 kernel is unchanged; do not repeat its probe.

## Confirmed starting state

The September 17 repair report confirms RECOVERY_CLEANUP_COMPLETE, successful
deletion of oem37.inf, preserved Intel package/service checks and an unbound
controller (Problem 0; empty Service/INF). Its before-journal matches the prior
uploaded failure. Snapshot bytes are unchanged, SHA-256:
5e2612971e5681636c999b3aca8845c083778ff190402446f7da14bef5fc8b18.

## Implemented behavior

The cold-boot coordinator parses the entire image and requires a backend image
approval before preparation. DMA preparation receives only the CPD payload,
excluding unsigned XMan. The HDA stream tag must be 1..15 before constructing
the APL/GLK ROM command with tag-minus-one.

The backend must produce fresh ROM INIT_DONE and FW_ENTERED observations.
Halted states are refused even if their low bits resemble success. Explicit
DMA quiescence precedes ready-header reading and memory release. Partial
prepare/start errors also require quiescence. Failed quiescence retains
resources and forbids release, shutdown or further ready processing. There are
no automatic retries. Primary and cleanup errors are separately retained.

The IPC3 parser reads the fixed 108-byte FW_READY header without packed casts.
It checks command, nested 60-byte version structure, ABI major 3, caller-provided
maximum ABI minor and reserved fields. It clears outputs on failure and accepts
unaligned input. Mailbox offsets/sizes and flags are recorded as UNTRUSTED DATA,
never followed as MMIO addresses. Extended window validation is not implemented.

The highest successful result is ReadyHeaderValidated, not operational IPC or
working sound. The backend contract requires synchronous bounded operations,
fresh events for this boot, immutable input and exclusive device ownership.
It also requires correct device-visible DMA addresses, descriptor teardown,
deadline enforcement and owned-core recovery. Those requirements are not
implemented merely because a function pointer exists.

## What tests establish

Synthetic image approval is a mock, NOT RSA/CSE authentication. Tests cover:
all truncated ready prefixes; unsupported command, version and ABI; reserved
fields; unaligned input; 10,000 deterministic mutations; stream tag bounds;
malformed image/incomplete backend; denied approval; partial preparation/start;
invalid/halted ROM states; missing/invalid ready header; failed quiescence;
shutdown/release failures and primary-error preservation.

Linux ASan/UBSan and Windows MSVC jobs compile and exercise the source with
warnings as errors. The existing SHA-256-pinned official firmware regression
still checks the structural parser; it does not authenticate or boot firmware.
No new hardware result is claimed by a CI pass.

## Remaining before a physical boot test

1. Windows WDF DMA adapter and HDA stream/BDL/SPIB implementation, with correct
   DMA-visible addresses, cancellation, allocation lifetime and verified stop.
2. APL/GLK core, clock-consumer, ROM doorbell and IPC interrupt handling with
   finite polling deadlines and recovery.
3. Real image approval/authentication and validated extended IPC3 windows.
4. Integration with device ownership/recovery and the manual hardware-write
   build gate. Amplifier enable stays separate from DSP boot.

Codec routing, audio DMA and WaveRT remain later work. No new installation or
audit command is needed from the user for this source-only milestone.

## Primary references reviewed

- [Gemini Lake uses APL operations and IPC3](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/pci-apl.c)
- [APL core and loader selection](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/apl.c)
- [Payload stripping, ROM command and transfer ordering](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda-loader.c)
- [Core power/reset/stall behavior](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda-dsp.c)
- [ROM control and status masks](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda.h)
- [Ready/version and extended-window layouts](https://github.com/torvalds/linux/blob/v6.12/include/sound/sof/info.h)
- [IPC3 command header](https://github.com/torvalds/linux/blob/v6.12/include/sound/sof/header.h)
- [ABI encoding](https://github.com/torvalds/linux/blob/v6.12/include/uapi/sound/sof/abi.h)
- [Ready handling and separate window setup](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/ipc3.c)

Independent implementation of the published contracts. Upstream implementation
files are not vendored. Test timeouts are mock policies, not measured Windows
hardware timing guarantees.
