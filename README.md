# 360 / PHASER360 Open Audio

Open-source Windows audio enablement project for Lenovo 300e Chromebook 2nd Gen / PHASER360 (Intel Gemini Lake).

## Current continuation status

- **M0.5.1:** [fresh-Windows probe](docs/M051_FRESH_WINDOWS.md), with a new
  driver/service identity, PnP-assigned addresses, unbound-controller preflight,
  owned-package cleanup and recovery. Build artifact: `PHASER360_M051_FRESH_WINDOWS`.
  This is a temporary diagnostic driver, not working audio. CI evidence does not
  replace a snapshot from the reinstalled Lenovo.
- **M0.5:** the read-only MMIO probe built and packaged successfully in
  [CI run 34754361785](https://github.com/onish12/360/actions/runs/34754361785).
  This is build/package evidence, not a successful test on the reinstalled Lenovo.
  **Do not run the installer from that historical artifact.** Its script assumed
  the retired Intel SST 9.22.0.4883 / oem0.inf baseline and automatically tried to
  restore it. Both runtime entry points in new builds are now explicitly blocked
  before system operations. Do not reinstall 4883 or disable signature checks.
- **M0.6:** [offline SOF firmware parser](docs/M06_FIRMWARE.md), malformed-input
  tests and a SHA-256-pinned official APL/GLK regression image. This component
  does not load firmware, install a driver or produce sound.
- **Working Windows audio:** not yet implemented. DSP boot/authentication, IPC,
  machine/codec integration and WaveRT remain separate milestones.

The older bootstrap description below is retained for source history. Do not
interpret either milestone's CI success as permission to enable hardware writes.

## Verified hardware target

- Intel Gemini Lake HD Audio / AudioDSP controller: `PCI\\VEN_8086&DEV_3198`
- ADSP child created by `sklhdaudbus`: `CSAUDIO\\ADSP&CTLR_VEN_8086&CTLR_DEV_3198`
- Headset codec: `ACPI\\DLGS7219` (DA7219)
- Speaker amplifier: `ACPI\\MX98357A` (MAX98357A)
- Firmware topology/NHLT already verified on the target machine.

## Why this repository exists

The generic Intel SST 9.22.0.4883 stack did not create usable internal audio endpoints on the target and caused a boot-loop when its kernel stack was active. The fresh-Windows continuation uses the PCI controller directly and develops a free/open-source SOF/WaveRT backend. The earlier `sklhdaudbus` interface probes below are historical work, not a dependency to reinstall.

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
