# 360 / PHASER360 Open Audio

Open-source Windows audio enablement project for Lenovo 300e Chromebook 2nd Gen / PHASER360 (Intel Gemini Lake).

## Current continuation status

- **Adaptive entry point 1.1.1:** [PHASER360_AUDIO_AUTO](docs/M051_AUDIO_AUTO.md).
  RUN_AUDIO.cmd verifies the current package and signing state. For the exact
  reviewed Intel bytes it performs an [instance-scoped handoff](docs/M051_HANDOFF.md)
  to the compiled read-only probe. Intel files stay in DriverStore; the successful
  final controller state is deliberately unbound. Output: one RESULT_AUDIO_*.zip.
- **M0.5.1:** [fresh-Windows probe](docs/M051_FRESH_WINDOWS.md), with its original
  unbound-controller guard unchanged. The separate handoff uses the same kernel.
  Neither route implements sound.
  **Latest evidence (2026-09-16 21:12:27):** the first real MMIO snapshot succeeded:
  GCAP `0x6701`, ADSPCS `0x001D003C`. Cleanup then failed because version 1.1
  required Code 28, while Windows reported Problem 0 with empty Service/INF.
  [Repair 1.1.1](docs/M051_HARDWARE_20260916.md) finishes the reviewed transaction
  through REPAIR_AUDIO.cmd without repeating the probe. The September 17 report
  confirms physical cleanup, retained Intel package/service checks and the
  unchanged snapshot. Final controller binding is UNBOUND.
- **M0.5:** the read-only MMIO probe built and packaged successfully in
  [CI run 34754361785](https://github.com/onish12/360/actions/runs/34754361785).
  This is build/package evidence, not a successful test on the reinstalled Lenovo.
  **Do not run the installer from that historical artifact.** Its script assumed
  the retired Intel SST 9.22.0.4883 / oem0.inf baseline and automatically tried to
  restore it. Both runtime entry points in new builds are now explicitly blocked
  before system operations. Do not reinstall 4883 or change signing settings for those retired packages.
- **M0.6:** [offline SOF firmware parser](docs/M06_FIRMWARE.md), malformed-input
  tests and a SHA-256-pinned official APL/GLK regression image. This component
  does not load firmware, install a driver or produce sound.
- **M0.6.1:** [cold-boot coordination and IPC3 ready-header validation](docs/M061_BOOT_PROTOCOL.md).
  Compiled core logic tested with injected failures. Hardware MMIO/IRQ and
  extended IPC windows are still missing; this is not an installable loader.
- **M0.6.2:** [Windows DMA memory component](docs/M062_WINDOWS_DMA.md).
  KMDF common-buffer allocation, HDA descriptors and guarded buffer ownership.
  Built as a separate WDK static library; not wired to the controller or M0.5.1.
  Fault-injection tests use a WDF shim, not real hardware.
- **M0.6.3:** [HDA stream transport](docs/M063_HDA_TRANSPORT.md).
  Capability discovery, GLK format sequencing, bounded RUN/reset polling, SPIB,
  register readback and DMA-owner integration. Separate WDK library with real
  register APIs; not connected to a PnP driver or tested on the Lenovo.
- **M0.6.4:** [GLK DSP power and ROM handshake](docs/M064_GLK_ROM.md).
  Core power/reset/stall, fresh ROM acknowledgement, firmware-entry polling and
  Windows HDA/DSP sequencing. Static library with simulated failure tests; no
  hardware execution or installable driver yet.
- **M0.6.5:** [FW_READY reception and manifest IPC windows](docs/M065_IPC_READY.md).
  Validated XMan windows, stale-message clearing before boot, bounded fresh
  FW_READY reception and host acknowledgment after confirmed DMA release.
  WDK compilation and Windows/Linux tests pass; no hardware execution yet.
- **M0.6.6:** [IPC3 command/reply transport](docs/M066_IPC_COMMAND.md).
  Bounded mailbox requests, DONE-based reply capture, signed firmware errors,
  deadline handling and no reuse after uncertain completion. Integrated with
  the Windows boot owner; notification dispatch and hardware validation remain.
- **M0.6.7:** [IPC3 notification capture](docs/M067_IPC_NOTIFICATIONS.md).
  Bounded FIFO for stream position, XRUN and trace position; captures notifications
  during command transactions and idle polling, preserving uncertain ACK results.
  Endpoint delivery remains unimplemented; the interrupt bridge is added below.
- **M0.6.8:** [KMDF IPC interrupt bridge](docs/M068_KMDF_INTERRUPTS.md).
  DIRQL ISR masks the source and queues PASSIVE_LEVEL notification processing.
  Commands, deferred work and terminal shutdown share explicit synchronization.
  Real WDK compilation and Windows/Linux modeled tests pass; no Lenovo IRQ
  execution yet. This remains a static library requiring a PnP/power owner.
- **M0.6.9:** [Cold-start power coordination](docs/M069_COLD_POWER.md).
  Connects boot, post-enable arming and pre-disable shutdown for one D0 session.
  Failed early startup cleans up before returning; unconfirmed cleanup blocks
  the mapping-release gate. PnP callback registration and full power/removal
  recovery remain outstanding. WDK compilation and Windows/Linux tests pass.
- **M0.6.10:** [Guarded next-D0 rebinding](docs/M0610_D0_REBIND.md).
  Reuses the interrupt with a fresh boot owner only after confirmed shutdown,
  framework Disable and completion of old queued work. Two-session simulation
  includes successful notifications and failed second boot cleanup. Full
  Windows sleep/resume and PnP integration are still outstanding. WDK build
  and Windows/Linux tests pass.
- **M0.6.11:** [Explicit deferred-work drain](docs/M0611_DEFERRED_DRAIN.md).
  Owns a DPC and work item; confirmed stop is followed by DPC cancellation/wait
  and work-item flush before DSP shutdown. This supplies the orderly drain
  required before next-D0 rebinding. WDK and Windows/Linux tests pass.
  Full PnP/removal policy remains unfinished.
- **M0.6.12:** [Owned firmware identity gate](docs/M0612_PINNED_FIRMWARE.md).
  Copies the complete reference image into owned memory and verifies its fixed
  SHA-256 with Windows CNG before passing the bound XMan/payload to ColdPower.
  WDK compilation, Windows/Linux tests and real Windows CNG fixture checks pass.
  File identity is not hardware deployment approval or RSA-chain verification.
- **M0.6.13:** [PnP resource callbacks](docs/M0613_PNP_RESOURCES.md).
  Registers PrepareHardware/ReleaseHardware and validates/maps assigned HDA/DSP
  resources, with partial-failure cleanup and repeat-start tests. No mapping
  pointers are exposed; DSP boot and DMA/IRQ consumers are not connected.
  WDK compilation and Windows/Linux tests pass; hardware remains untested.
- **M0.6.14:** [IRQ exit failure cleanup](docs/M0614_IRQ_EXIT.md).
  Fixes queued-worker admission after a failed interrupt stop. Adds guarded
  D0Exit cleanup after framework disconnection, including failed Enable with
  omitted Disable. Queue drain remains separate from hardware/DMA quiescence.
  WDK and Windows/Linux verification pass; surprise-removal recovery and a
  composed booting PnP driver remain unfinished.
- **M0.6.15A:** [hardware-access fence](docs/M0615_ACCESS_FENCE.md).
  Adds one cached nonpaged atomic gate shared by HDA/DSP/IRQ consumers. A terminal
  surprise-removal transition blocks new MMIO and interrupt synchronization in
  the modeled paths. This is a prerequisite for, not a substitute for, the
  composed PnP/power owner; DMA-parent teardown on concurrent removal remains
  deliberately unresolved. Final source `1c028cda` passes real WDK compilation,
  10 WDK host tests, 14 Windows tests and 12 Linux ASan/UBSan tests; hardware
  remains untested. No codec, amplifier or playback path is added.
- **M0.6.15B:** [paired PnP resource lifetime](docs/M0615_PNP_RESOURCE_BUNDLE.md).
  Validates raw/translated resource pairing, maps HDA/DSP with the shared gate
  closed, records a bounded set of interrupt descriptor pairs without guessing
  a vector, then opens the gate only after full preparation. Normal Release closes
  access before unmapping; terminal Removed never reopens. Still resource-only:
  no DSP boot, DMA consumer, codec, amplifier or playback. Source `66055d64`
  passes real WDK compilation, 10 WDK host tests, 14 Windows tests and 12 Linux
  ASan/UBSan tests; PnP resource validation reports 218 passing assertions.
- **M0.6.15C:** [framework surprise-removal fence](docs/M0615_SURPRISE_CALLBACK.md).
  Registers the real KMDF EvtDeviceSurpriseRemoval callback to make the shared
  hardware-access gate terminal without MMIO or teardown work in that
  unsynchronized callback. Prepare/Release transitions are made race-safe for
  a concurrent terminal gate, with deterministic removal-during-map tests.
  Still no DSP boot, selected IRQ, DMA consumer, codec, amplifier or playback.
  Source `0ca3bca8` passes real WDK compilation, 10 WDK host tests, 14 Windows
  tests and 12 Linux ASan/UBSan tests; PnP lifecycle validation reports 232
  passing assertions.
- **M0.6.15D:** [KMDF D0 callback skeleton](docs/M0615_POWER_SKELETON.md).
  Registers D0Entry, post-interrupt-enable, pre-interrupt-disable and D0Exit as
  a software-only lifecycle state machine. ReleaseHardware refuses to unmap while
  the owner still claims D0, while surprise-removal unwind remains possible with
  the terminal gate closed. No ColdPower call, DSP boot, selected IRQ, DMA
  consumer, codec, amplifier or playback is connected. Source `376c9a97`
  passes real WDK compilation, 10 WDK host tests, 14 Windows tests and 12 Linux
  ASan/UBSan tests; the PnP/power skeleton reports 313 passing assertions.
- **M0.6.15E:** [interrupt resource inventory](docs/M0615_IRQ_INVENTORY.md).
  Decodes line-based and message-signaled raw/translated interrupt descriptors
  using the Windows-defined union selected by CM_RESOURCE_INTERRUPT_MESSAGE.
  Vector, IRQL, affinity, sharing and message count are inventory only; no
  descriptor is selected and WdfInterruptCreate is not called. Live Lenovo IRQ
  shape remains a required read-only evidence step before interrupt binding.
  Source `e8678d81` passes real WDK compilation, 10 WDK host tests, 14 Windows
  tests and 12 Linux ASan/UBSan tests; IRQ inventory validation reports 360
  passing assertions.
- **M0.6.15F:** [read-only live IRQ evidence](docs/M0615_LIVE_IRQ_EVIDENCE.md).
  Adds a Windows 11 x64 collector that targets only the present
  `PCI\\VEN_8086&DEV_3198` instance and permits PnPUtil only through
  `/enum-devices` with read-only enumeration switches. It records raw
  `/resources` output, full PnP/device properties, before/after binding state
  and SHA-256 sums. No driver install/bind, restart, MMIO, DSP boot,
  WdfInterruptCreate, IRQ selection or playback is performed. Source
  `905ea62a` passes real WDK compilation, 10/10 selected host tests and the
  collector's read-only/self-test guards. The Lenovo was then confirmed to run
  Windows 10 21H2 build 19044, so the original PnPUtil /resources path correctly
  refused to run.
- **M0.6.15F2:** Windows 10/11 dual read-only IRQ capture extends F without
  weakening it. Build 19044+ below Windows 11 22H2 reads only
  `SPDRP_ALLOC_CONFIG` through SetupAPI and records the returned
  `CM_RESOURCE_LIST`; Windows 11 22H2+ retains the PnPUtil /resources route.
  MESSAGE vs LINE is classified only from `CM_RESOURCE_INTERRUPT_MESSAGE`.
  IRQ selection, WdfInterruptCreate, DSP boot and playback remain disabled.
  The initial F2 artifact from `fa7e9c0d` is **withdrawn before target use**:
  a later independent ABI audit found its Win10 parser assumed a 32-byte
  `CM_PARTIAL_RESOURCE_DESCRIPTOR`, while WDK defines the structure under
  pack(4) and the x64 size is 20 bytes.
- **M0.6.15F3:** corrected the Win10 allocated-resource parser and passed WDK
  ABI checks, but the first target execution showed SPDRP_ALLOC_CONFIG is not
  exposed for this DEV_3198 devnode on Windows 10 21H2. It failed read-only
  before capture and is withdrawn for this target.
- **M0.6.15F4:** uses Configuration Manager ALLOC_LOG_CONF on Windows 10 instead:
  CM_Locate_DevNodeW, CM_Get_First_Log_Conf, CM_Get_Next_Res_Des and the two
  read-only data getters. Returned descriptors are preserved as raw bytes.
  ResType_IRQ is evidence of an allocated IRQ resource only; signaling type is
  deliberately UNDETERMINED, so IRQ selection and WdfInterruptCreate remain
  blocked pending sufficient evidence. Source `5d36b8ac` passes real WDK
  compilation, 10/10 selected host tests and all CfgMgr read-only/static guards.
  The live F4 capture is recorded in
  [M0615F4_LIVE_CAPTURE_20260921.md](docs/M0615F4_LIVE_CAPTURE_20260921.md).
- **M0.6.15G:** [single IRQ admission](docs/M0615_SINGLE_IRQ_ADMISSION.md).
  Accepts exactly one PnP interrupt pair. LINE is allowed; MESSAGE is allowed
  only when MessageCount=1, matching the live DEV_3198 PCI capability. Multiple
  pairs and multi-message assignments fail closed. This still does not call
  WdfInterruptCreate, touch MMIO, boot the DSP or produce audio.
- **Working Windows audio:** not yet implemented. Integration of the pinned image into the device driver,
  PnP/power ownership and platform IRQ routing, remaining notification types, machine/codec integration, stream DMA
  and WaveRT remain separate milestones.

The older bootstrap description below is retained for source history. Do not
interpret either milestone's CI success as permission to enable hardware writes.

## Verified hardware target

- Intel Gemini Lake HD Audio / AudioDSP controller: `PCI\\VEN_8086&DEV_3198`
- ADSP child created by `sklhdaudbus`: `CSAUDIO\\ADSP&CTLR_VEN_8086&CTLR_DEV_3198`
- Headset codec: `ACPI\\DLGS7219` (DA7219)
- Speaker amplifier: `ACPI\\MX98357A` (MAX98357A)
- Firmware topology/NHLT already verified on the target machine.

## Why this repository exists

The generic Intel SST 9.22.0.4883 stack did not create usable internal audio endpoints on the target. A boot-loop was reported after earlier installations; the supplied evidence does not establish its precise cause. The fresh-Windows continuation uses the PCI controller directly and develops a free/open-source SOF/WaveRT backend. The earlier `sklhdaudbus` interface probes below are historical work, not a dependency to reinstall.

## Milestone 0.1 — original bootstrap

`src/probe/phaser360_adsp_probe` is deliberately **read-only**. It binds only to the ADSP child and does exactly one hardware-stack operation: query `GUID_ADSP_BUS_INTERFACE` version 1. It validates that the returned controller ID is `0x3198` and logs the result.

It does **not**:

- call `SetDSPPowerState`;
- call `GetResources`;
- touch BAR/MMIO registers;
- register interrupts;
- allocate DMA streams;
- trigger SSP/PDM;
- load SOF firmware;
- enable MAX98357A;
- program DA7219;
- produce audio.

This is intentional. First prove that the open bus contract can be consumed by our own driver, then add one capability at a time.

## Build

The repository is configured for GitHub Actions using Microsoft's current WDK NuGet flow. Push the contents of this bootstrap to the repository and run **Build ADSP probe**.

Expected artifact: an **unsigned** `phaser360_adsp_probe.sys`. Do not install it yet. The first CI goal is compile/link only.

## Roadmap

1. **M0.1** — ADSP interface query only (this bootstrap).
2. **M0.2** — read-only `GetResources`: validate HDA BAR, ADSP BAR, PP capability pointer, NHLT pointer/size and PCI config interface. No MMIO writes.
3. **M0.3** — SOF firmware parser/loader design + emulator/unit tests; still no speaker output.
4. **M1** — controlled SOF DSP boot + IPC handshake.
5. **M2** — single safe speaker path at 48 kHz stereo, SSP1 -> MAX98357A, with hard volume/amp safety gates.
6. **M3** — DA7219 headphone/headset path on SSP2.
7. **M4** — internal PDM microphone.
8. **M5** — suspend/resume, jack detection, recovery and stress testing.

## Safety rule

No experimental kernel driver is installed on the PHASER360 machine until CI has produced a clean build and the source for that milestone has been audited. Every hardware-writing milestone must have a WinRE recovery procedure before installation.
