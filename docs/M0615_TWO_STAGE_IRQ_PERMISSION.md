# M0.6.15H3 two-stage dormant IRQ permission

M0.6.15H3 separates permission to start the DSP from permission for the KMDF
interrupt callbacks to touch interrupt-source registers. This removes the unsafe
single-boolean window identified after H2.

The milestone is still a static/non-installable model. No code in H3 is run on
the Lenovo.

## Two independent grants

A DeviceAdd shell starts with both permissions false.

### 1. GrantBootStart

GrantBootStart is PASSIVE_LEVEL and software-only. It succeeds only for a
currently bound DeviceAdd shell with:

- a fresh GlkBoot;
- the same live HardwareAccessGate established by H2;
- no framework Enable yet;
- no previous arm/fault/stop/admission closure.

It sets bootStartAllowed_ only.

CanStartBeforeEnable uses this permission, so ColdPower::Enter can now perform
the already-reviewed boot sequence. Sync and EvtInterruptEnable still do not
have MMIO permission at this point.

### 2. GrantFrameworkEnableAfterBoot

GrantFrameworkEnableAfterBoot is a separate PASSIVE_LEVEL software transition.
It requires:

- boot-start permission already granted;
- framework Enable not yet seen;
- live hardware gate and bound DSP mapping;
- GlkBoot::CommandUsable() == true, which is reached only after the existing
  boot path has validated firmware entry, DMA release, FW_READY and command
  readiness.

Only then is hardwareEnableAllowed_ set.

EvtInterruptEnable and Sync remain gated by hardwareEnableAllowed_. Therefore a
framework interrupt connection cannot legitimately perform DSP interrupt-source
MMIO merely because PnP resources were mapped or because boot-start permission
was granted.

Both grant calls themselves perform zero MMIO and zero WdfInterruptSynchronize.

## Closed-session reset

The DeviceAdd WDF interrupt object outlives each D0/resource session. H3 adds
ResetDormantClosedSession so a completed or failed D0 session cannot leak its
GlkBoot pointer or permission flags into a later mapping lifetime.

Reset is software-only and requires:

- the shell is a DeviceAdd lifetime shell;
- framework is disconnected, or Enable was never reached;
- interrupt-side state is confirmed closed/stopped/drained;
- no pending deferred work;
- DSP pointer was already detached by the existing Stop/Cancel path;
- the old GlkBoot is no longer command-usable.

It clears both grants, the old GlkBoot pointer, and per-session interrupt state,
while preserving the WDF interrupt shell itself.

The legacy explicit-descriptor Create/RebindStopped test path retains both
permissions by construction. RebindStopped is explicitly refused for a
DeviceAdd lifetime shell so it cannot bypass the H2/H3 PnP and grant contracts.

## Deterministic successful-D0 test

The wrapper test executes:

1. CreateDormant;
2. BindDormant;
3. prove framework-enable grant is refused before boot;
4. grant boot start with MMIO forbidden;
5. prove CanStartBeforeEnable becomes true;
6. ColdPower::Enter performs the simulated GLK boot;
7. require commandReady;
8. grant framework-enable permission with MMIO forbidden;
9. framework EvtInterruptEnable performs the existing mask sequence;
10. ColdPower post-enable arm succeeds;
11. one notification takes the ISR/deferred path;
12. pre-disable Stop/drain/shutdown succeeds;
13. reset is refused while framework is still connected;
14. framework Disable runs;
15. ResetDormantClosedSession succeeds with MMIO/synchronization forbidden;
16. a fresh GlkBoot can be bound to the same device-lifetime shell.

## Deterministic failed-D0Entry test

A separate test grants boot start and forces the existing HDA preparation
failure before framework Enable. ColdPower's early cleanup must leave the
interrupt side closed/drained and the boot owner shut down. H3 then requires
ResetDormantClosedSession to succeed without MMIO or synchronization.

This is the path required because KMDF does not call D0Exit after a failed
D0Entry.

## Still not composed into PnP callbacks

H3 proves the permission state machine, but PnpResources::D0Entry does not yet
invoke BindDormant, the two grants, PinnedFirmware or ColdPower. That composite
owner remains the next milestone.

No physical MMIO, driver install, codec/amplifier operation or playback is
authorized by H3.


## Verified CI evidence (2026-09-21)

Implementation commit: `0f7313ddce4b54193808db0f36a44aade54a0f63`.
Tree: `e757dcde64292b13ddf8440ec9a1d670c18607ee`.

- WDK/KMDF run `35609234071`, job `106363895582`: real WDK
  compilation passes and all 10 selected host tests pass.
- Integrated boot/IRQ regression reports
  `SOF_GLK_BOOT_TESTS=267115 PASS; windows_api=SIMULATED; hardware=NONE`.
- PnP/H2 remains `SOF_PNP_RESOURCES_TESTS=420 PASS`.
- Windows/Linux run `35609233951`: Windows job `106363894811`
  passes all 14 tests and Linux job `106363895239` passes all 12 tests.
- Windows official firmware identity checks remain
  `SOF_CNG_PIN_TESTS=10 PASS` and
  `SOF_PINNED_REFERENCE_TESTS=13 PASS`.
- F4 collector self/static guards remain green.
- Development artifact `PHASER360_M0615H3_TWO_STAGE_IRQ_PERMISSION`:
  ID `10643401362`, 317,825 bytes. GitHub reports archive SHA-256
  `bce045656868398de3d5c0d73fbecda45b7451b95fa6dca7699c01d425511eea`.

H3 was not executed on the physical Lenovo. No installable driver, codec,
amplifier or playback path is added.
