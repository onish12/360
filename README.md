# 360 / PHASER360 Open Audio

Open-source Windows audio enablement project for Lenovo 300e Chromebook 2nd Gen / PHASER360 (Intel Gemini Lake).

## Verified hardware target

- Intel Gemini Lake HD Audio / AudioDSP controller: `PCI\\VEN_8086&DEV_3198`
- ADSP child created by `sklhdaudbus`: `CSAUDIO\\ADSP&CTLR_VEN_8086&CTLR_DEV_3198`
- Headset codec: `ACPI\\DLGS7219` (DA7219)
- Speaker amplifier: `ACPI\\MX98357A` (MAX98357A)
- Firmware topology/NHLT already verified on the target machine.

## Why this repository exists

The generic Intel SST 9.22.0.4883 stack is not the solution for this Chromebook: it did not create usable internal audio endpoints on the target and caused a boot-loop when its kernel stack was active. The project therefore uses the public `sklhdaudbus` ADSP interface as the boundary and will implement a free/open-source SOF/WaveRT backend.

## Milestone 0.1 — current bootstrap

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
