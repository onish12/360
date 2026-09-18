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
- **Working Windows audio:** not yet implemented. Authenticated image ownership,
  PnP/power integration, operational IPC, machine/codec integration and WaveRT
  remain separate milestones.

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
