# M0.6.2 Windows DMA memory component

This is a kernel-mode static library, not a driver installer. No DriverEntry,
INF, MMIO writes, firmware execution or audio endpoint is added. M0.5.1 remains
read-only. The controller was last confirmed detached and the Intel package
preserved by the successful 2026-09-17 repair; do not repeat that probe.

## Implemented

BootDma allocates real KMDF common buffers for the approved firmware payload
and its buffer descriptor list. It obtains device logical addresses through
WdfCommonBufferGetAlignedLogicalAddress, not CPU physical addresses.

The initial implementation intentionally uses a 32-bit scatter/gather DMA
profile; all address extents are checked against the 32-bit limit. It does not
infer hardware capability from a historical snapshot. Buffers request 4096-byte
alignment (mask 4095). The independent builder emits 16-byte little-endian
descriptors, splits at 4096 bytes, allows at most 256 entries and 1 MiB total,
zeros unused descriptors, and leaves IOC disabled. These are deliberately
conservative boot-transfer policy limits, not a claim that 1 MiB is a hardware
maximum. The known 286720-byte CPD payload uses 70 entries / LVI 69.

Prepare copies a caller-approved payload; it does not authenticate the firmware
or strip XMan. The boot coordinator is responsible for both. Allocation failure
unwinds buffers in reverse order. Invalid address extents do the same.

Publish returns BDL logical address, exact CBL and LVI, and marks ownership as
potentially hardware-visible BEFORE the caller writes any register. A barrier
orders CPU buffer writes. Release refuses to free published buffers unless the
caller's verifier confirms both stopped DMA and detached descriptor addresses.
A verifier failure retains all handles. Unpublished resources can be released
directly; repeated release is harmless.

## Integration contract and remaining work

Every API call must be serialized at PASSIVE_LEVEL while WDFDEVICE is alive.
BootDma must be constructed, must not be copied, and must be explicitly released
before destruction. WDF device-parent deletion can also free child objects:
the future driver's D0-exit, removal and failure paths MUST quiesce hardware
before parent deletion. This class cannot prevent an external parent teardown.
The quiescence callback is a contract, not hardware proof supplied by this library.

The driver must not publish addresses except through Publish. No callback
currently touches the real controller. Hardware-backed quiescence, stream
selection/reset, SPIB, ROM control, interrupt handling, firmware trust,
FW_READY mailbox validation and PortCls/codec integration remain outstanding.
No hardware package should be built merely by linking this component.

## Verification

CI compiles the production component against WDK 10.0.28000.2526 / KMDF 1.33,
warnings as errors. User-mode tests execute the same allocator source against
a deliberately separate WDF shim to inject each allocation failure, invalid
logical addresses, incorrect IRQL, duplicate prepare/publish and failed
quiescence. They verify payload copy, reverse cleanup, retained ownership and
the 70-entry reference layout. The pure descriptor tests cover unaligned CPU
output, 32/64-bit address limits, overflow, maximum capacity and final fragments.
The existing Linux job runs these with ASan/UBSan. These tests do not establish
real WDF runtime, IOMMU, driver removal or device behavior.

## Primary references

- [KMDF common buffer allocation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfcommonbuffer/nf-wdfcommonbuffer-wdfcommonbuffercreatewithconfig)
- [Device logical addresses](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfcommonbuffer/nf-wdfcommonbuffer-wdfcommonbuffergetalignedlogicaladdress)
- [Alignment mask contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdfcommonbuffer/ns-wdfcommonbuffer-_wdf_common_buffer_config)
- [Common buffer ownership](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/using-common-buffers)
- [DMA profiles](https://learn.microsoft.com/en-us/windows-hardware/drivers/wdf/enabling-dma-transactions)
- [Linux v6.12 SOF HDA descriptor implementation](https://github.com/torvalds/linux/blob/v6.12/sound/soc/sof/intel/hda-stream.c)
- [HDA descriptor count](https://github.com/torvalds/linux/blob/v6.12/include/sound/hda_register.h)

Independent MIT implementation; no Linux source copied.
