# M0.6.15H5 fresh per-D0 ownership

M0.6.15H5 removes the H4 single-D0 limitation without resetting or reusing a
GlkBoot object. Every D0Entry attempt receives a freshly constructed
GlkBoot+ColdPower pair in a dedicated nonpaged WDFMEMORY object.

The milestone remains a static-library/model milestone. It is not an installable
driver and is not executed on the physical Lenovo.

## Ownership split

- PnpResources owns the current prepared HDA/DSP BAR mappings.
- IpcInterrupt owns one device-lifetime dormant KMDF interrupt shell.
- PinnedFirmware owns the immutable approved firmware snapshot.
- RepeatedDeviceLifecycle owns only prepared-resource metadata and D0 ordering.
- D0SessionOwner allocates and constructs one fresh GlkBoot+ColdPower pair for
  each D0Entry attempt.

No GlkBoot internal state is reset. The single-attempt invariant remains intact.

## Per-D0 allocation

D0SessionOwner::Begin creates WDFMEMORY from NonPagedPoolNx, parented to the
WDFDEVICE, and placement-constructs:

- GlkBoot;
- ColdPower bound to that GlkBoot, the device-lifetime IpcInterrupt and the
  shared HardwareAccessGate.

The storage alignment is checked before construction.

The session generation counter increments only after successful construction.

## PrepareHardware no longer binds a boot owner

H4 bound the dormant IRQ shell during Prepared because one external GlkBoot
already existed.

H5 cannot do that: the correct GlkBoot does not exist until D0Entry.

RepeatedDeviceLifecycle::Prepared therefore stores only the current
resource-lifetime binding contract:

- the same HardwareAccessGate;
- current DSP BAR pointer/length;
- current raw/translated single-IRQ descriptors;
- LINE/MESSAGE classification.

The IRQ shell itself remains unbound between D0 sessions.

## D0Entry

For every attempt:

1. allocate/construct a new D0SessionOwner session;
2. BindDormant the IRQ shell to the new GlkBoot and current DSP mapping;
3. GrantBootStart;
4. call PinnedFirmware::Enter -> ColdPower::Enter;
5. require commandReady;
6. GrantFrameworkEnableAfterBoot;
7. return success so KMDF may run EvtInterruptEnable.

If WDFMEMORY allocation fails, no IRQ binding occurs and the next D0Entry may
retry.

If firmware admission fails before GlkBoot is attempted, the dormant IRQ
binding is removed and the unused session is destroyed.

If boot starts and the existing early cleanup confirms shutdown, the closed IRQ
session is reset and the clean session object is destroyed.

A failed D0Entry therefore does not poison the next D0 attempt.

## Normal D0Exit

Normal exit uses the already-reviewed ordering:

- pre-disable Stop/mask;
- deferred DPC/work drain;
- DSP/DMA shutdown;
- framework Disable/disconnect;
- D0Exit fallback if pre-disable could not confirm stop;
- ResetDormantClosedSession;
- D0SessionOwner::ReleaseClean.

ReleaseClean requires:

- ColdPower::CanReleaseMappings() == true;
- command transport no longer usable.

Only then are the C++ session objects destroyed and their WDFMEMORY object
explicitly deleted.

The resource contract remains prepared, so a later D0Entry can allocate a new
generation and bind the same device-lifetime IRQ shell again.

## Failed middle generation

The deterministic H5 sequence includes:

- D0 generation 1: successful boot and clean shutdown;
- D0 generation 2: deterministic FW_READY failure and complete failed-entry
  cleanup;
- D0 generation 3: successful fresh boot and clean shutdown.

This directly verifies that a failed single-attempt GlkBoot object cannot
contaminate the following D0 session.

## Resource rebalance boundary

After all active D0 state is gone, ReleaseHardware invokes the lifecycle release
hook. H5 clears the borrowed PnP binding metadata.

A subsequent PrepareHardware lifetime may provide different HDA/DSP virtual
addresses and a different interrupt descriptor pair. The old binding is not
retained by the IRQ shell or by an active D0 session.

## Surprise removal

SurpriseRemoval remains terminal.

If removal occurs while a D0 session is active:

- the shared HardwareAccessGate closes first;
- IRQ admission is fenced software-only;
- no further MMIO or WdfInterruptSynchronize is permitted;
- after framework disconnect, deferred software work is drained;
- ResetDormantRemovedSession clears stale IRQ boot/BAR/grant pointers;
- D0SessionOwner::AbandonRemoved destroys the per-D0 C++ session and its
  WDFMEMORY storage without claiming DSP shutdown or DMA quiescence.

If removal wins after commandReady but before framework Enable, no ISR/DPC/work
can exist. The just-created generation is abandoned inside failed D0Entry; a
matching D0Exit is not required.

The terminal gate prevents any later D0 generation.

## Deterministic validation

H5 adds wrapper coverage for:

- three D0 attempts on one prepared resource lifetime;
- fresh session generation for every attempt;
- successful #1 -> failed #2 -> successful #3;
- WDFMEMORY allocation failure followed by successful retry;
- no active per-D0 allocation remaining after clean or failed entry cleanup;
- ReleaseHardware boundary followed by a new prepared BAR binding;
- terminal removal after an earlier successful D0;
- terminal removal with queued IRQ work and all subsequent MMIO forbidden;
- terminal race after commandReady but before framework Enable.

Older H1-H4 tests remain in the same integrated suite.

## Still intentionally absent

H5 does not add:

- DriverEntry or EvtDriverDeviceAdd;
- an INF or installable SYS;
- firmware file acquisition from Windows;
- physical Lenovo execution;
- SSP1/MAX98357A programming;
- SSP2/DA7219 programming;
- speaker/headphone/microphone endpoints;
- WaveRT/ACX stream playback.

No audio output is authorized by H5.


## Verified CI evidence (2026-09-21)

Final H5 source commit:
`be18501b8843c698dc7eebe18e77f90f22b6bf57`.
Tree: `e17bede4ce3db6ac2ff13abd886499d4ace8d80b`.

The preceding H5 commit `0262e02b` passed Windows/Linux host tests but failed
the real WDK compile because including the standard `<new>` header pulled
exception declarations that are invalid under `/kernel`. H5 was corrected to
use a minimal kernel-safe placement-new overload while retaining
`WdfMemoryCreate` as the only allocation source. No lifecycle logic changed in
that correction.

- WDK/KMDF run `35614629909`, job `106382055406`: real WDK
  compilation passes and all 10 selected host tests pass.
- PnP remains `SOF_PNP_RESOURCES_TESTS=516 PASS;
  lifecycle_hooks=ORDERED_FAIL_CLOSED; hardware=NOT_TOUCHED`.
- Integrated boot/IRQ/repeated-D0 model reports
  `SOF_GLK_BOOT_TESTS=327105 PASS; windows_api=SIMULATED; hardware=NONE`.
- Windows/Linux run `35614629792`: Windows job `106382054937`
  passes all 14 tests and Linux job `106382054580` passes all 12 tests.
- Windows official firmware identity checks remain
  `SOF_CNG_PIN_TESTS=10 PASS` and
  `SOF_PINNED_REFERENCE_TESTS=13 PASS`.
- F4 read-only collector guards remain green:
  `IRQ_CAPTURE_SELFTEST=PASS`,
  `IRQ_CAPTURE_STATIC_TESTS=PASS`,
  `cfgmgr_readonly=YES`,
  `mutation_commands=REJECTED`.
- Development artifact `PHASER360_M0615H5_FRESH_PER_D0`: ID
  `10646360670`, 401,353 bytes. GitHub reports archive SHA-256
  `b3c5e3675dd9619e1c57c748a307ed4d8682cf9bd63b29c55c27d7c5a3659493`.

H5 remains non-installable and was not executed on the physical Lenovo.
No codec/amplifier programming, stream playback or Windows audio endpoint is
present.
