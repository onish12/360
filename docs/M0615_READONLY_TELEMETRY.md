# M0.6.15H11 read-only software telemetry

H11 adds one deliberately narrow observability surface before any physical
installation package exists.

The interface reports only software state already maintained by the driver. A
query is not allowed to wake the device, enter D0, read MMIO, synchronize the
interrupt, issue DSP IPC or change any device state.

The milestone remains non-installable and no SYS is uploaded.

## Why observability comes before physical M1

A first DSP-boot experiment must have a way to distinguish:

- firmware staged;
- resources prepared;
- a D0 attempt in progress;
- the per-D0 generation number;
- cleanly completed D0 attempts;
- failed D0 attempts;
- terminal surprise removal;
- the most recent D0Entry status.

Without a bounded read-only status contract, diagnosing a first hardware run
would otherwise depend on live register probing or ambiguous external symptoms.

H11 therefore establishes software telemetry before creating an INF/install
package.

## Fixed ABI

`TelemetrySnapshotV1` is exactly 32 bytes and contains only scalar values:

- ABI version and size;
- flags:
  - firmware loaded;
  - resources prepared;
  - D0 active;
  - terminal removed;
- current/latest session generation;
- completed D0 count;
- failed D0 count;
- last D0Entry NTSTATUS;
- one reserved zero field.

A compile-time assertion fixes the structure size at 32 bytes.

The ABI contains no:

- pointer;
- virtual or physical address;
- BAR;
- IRQ vector/affinity;
- register value;
- DMA address;
- firmware pointer or firmware bytes.

## Atomic software mirror

`TelemetryState` is separate from the lifecycle fields used for driver
decisions.

It uses interlocked operations only. Existing PnP/power transitions update the
mirror after their normal state transition.

The telemetry mirror is one-way: no boot, shutdown, resource, interrupt or
safety decision reads it.

Individual fields are atomic. A query racing a lifecycle transition may observe
adjacent fields from either side of that transition; consumers must use the
generation/counters/status as observational evidence, not as a transactional
control protocol.

This is intentional: telemetry must never become a synchronization dependency
for the hardware state machine.

## Query-only IOCTL

H11 defines one IOCTL:

`IOCTL_PHASER360_QUERY_STATUS`

The numeric contract is `0x00226000`, corresponding to:

- FILE_DEVICE_UNKNOWN;
- function 0x800;
- METHOD_BUFFERED;
- FILE_READ_ACCESS.

The request accepts no input payload.

Unknown IOCTLs are rejected. A non-zero input length is rejected. An output
buffer smaller than `TelemetrySnapshotV1` is rejected by the WDF output-buffer
retrieval path.

On success the driver copies one 32-byte software snapshot and completes the
request with exactly that information length.

## Non-power-managed queue

`CreateTelemetryEndpoint` creates the default sequential queue with:

`config.PowerManaged = WdfFalse`

The status queue is therefore intentionally outside the device's power-managed
I/O dispatch path. H11 also statically forbids calls that could request a power
transition, including WdfDeviceStopIdle/WdfDeviceResumeIdle.

The IOCTL implementation contains no hardware-access API.

## Device interface

A dedicated device-interface GUID is registered only after:

1. the exact embedded firmware has passed the owned-copy CNG pin;
2. the dormant interrupt shell has been created;
3. the DeviceOwner is fully composed.

Creating the interface does not execute PrepareHardware or D0Entry.

There is still no INF or install package, so H11 does not expose this interface
on the physical Lenovo.

## Lifecycle mirror validation

The existing H5 deterministic sequence is reused with telemetry enabled:

1. Prepare resources -> ResourcesPrepared set;
2. D0 generation 1 enters -> D0Active + generation 1 + success status;
3. clean exit -> D0Active clear, completed=1;
4. D0 generation 2 fails -> active clear, generation=2, failed=1,
   last status failure;
5. D0 generation 3 succeeds -> generation=3;
6. clean exit -> completed=2, failed=1;
7. Release -> ResourcesPrepared clear;
8. next Prepare -> ResourcesPrepared set again.

The original lifecycle counters and the telemetry mirror are asserted together.

A separate unit test exercises the mirror's flags, counters, ABI and status
without any WDF hardware object.

## Static no-hardware guard

The H11 guard requires:

- `PowerManaged=WdfFalse`;
- sequential queue;
- exactly the query IOCTL contract;
- zero input;
- buffered output retrieval;
- device-interface registration;
- software `QueryTelemetry` only;
- telemetry source compiled by both real/static WDK projects;
- telemetry unit test included in CI.

It forbids from the telemetry path:

- READ_REGISTER/WRITE_REGISTER;
- MmMapIoSpace/MmUnmapIoSpace;
- WdfInterruptSynchronize;
- WdfDeviceStopIdle/WdfDeviceResumeIdle;
- input-buffer retrieval;
- GlkBoot/ColdPower/IpcInterrupt/Command calls;
- address/pointer/hardware fields in the telemetry ABI.

All H8/H9/H10 binary link, import and reproducibility audits remain active.

## Safety boundary

H11 adds no:

- INF;
- catalog;
- signing certificate;
- install/bind/restart command;
- physical Lenovo execution;
- new MMIO operation;
- new DSP command;
- SSP1/SSP2/PDM programming;
- MAX98357A or DA7219 access;
- WaveRT/ACX/PortCls path;
- playback.

The next physical-package work must preserve this query-only observability and
the project-wide no-playback hold. The first hardware milestone remains limited
to DSP boot -> FW_READY/IPC -> clean shutdown.


## Verified CI evidence (2026-09-21)

Final H11 source/guard commit:
`547a34e53df2e7a7304b9a93379e399de92fdadf`.
Tree: `81175f251413e8a53388ea21cfd3946900a35941`.

The preceding H11 implementation commit `a7cf06a3` passed real WDK
compilation, all telemetry/unit tests and the H8-H10 linked-driver audits. Its
specific H11 workflow failed only because the static guard required the literal
workflow marker `H11_READONLY_TELEMETRY_STATIC_TESTS=PASS`, while that marker
was emitted by the guard script but not present in the workflow text inspected
by the same script. Commit `547a34e5` adds that workflow marker; no driver or
telemetry implementation code changed.

Final verification:

- H11 WDK/KMDF run `35635171620`, job `106450703306`: PASS.
- Real WDK component library compile: `WDK_DMA_LIBRARY=PASS`.
- PnP/lifecycle regression:
  `SOF_PNP_RESOURCES_TESTS=516 PASS; hardware=NOT_TOUCHED`.
- Telemetry mirror:
  `H11_TELEMETRY_STATE_TESTS=14 PASS; atomic_mirror=YES; hardware=NONE`.
- Integrated GLK/IRQ/repeated-D0 model:
  `SOF_GLK_BOOT_TESTS=327112 PASS; windows_api=SIMULATED; hardware=NONE`.
- F4 read-only resource collector:
  `IRQ_CAPTURE_SELFTEST=PASS`,
  `IRQ_CAPTURE_STATIC_TESTS=PASS`,
  `mutation_commands=REJECTED`.
- H8 temporary linked driver remains non-distributed:
  342,016 bytes, SHA-256
  `e36938ffdda9c903bf5a5205feb13832ce4c3cfed52f25e98d5daefb5eda9f0b`,
  then deleted before upload.
- H9 PE/import audit: PASS, x64/Native, CNG present, user-mode/audio imports
  absent.
- H10 reproducible rebuild: PASS; both temporary driver images are bit-identical
  at the H11 source state and contain IMAGE_DEBUG_TYPE_REPRO.
- H11 static query-only guard:
  `H11_READONLY_TELEMETRY_STATIC_TESTS=PASS;
  queue_power_managed=FALSE; method=BUFFERED; access=READ; input=NONE;
  software_mirror=ONLY; hardware_reads=NO; hardware_writes=NO;
  d0_trigger=NO; sys_upload=NO; inf=ABSENT; playback=NO`.
- Windows and Linux offline-parser jobs on run `35635171587`: PASS.
- Development artifact
  `PHASER360_M0615H11_READONLY_TELEMETRY`: ID `10656111333`,
  515,395 bytes; GitHub archive SHA-256
  `5d197f31db705969a146736ee492c8bf5b24e5847589f55ec2c7fde77a62677f`.

The artifact still contains no SYS or firmware RI image. H11 was not installed
or executed on the physical Lenovo and adds no codec/amplifier/audio path.
