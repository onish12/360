# M0.6.15H8 link-only KMDF boot driver

M0.6.15H8 crosses one build boundary only: CI links the composed H1-H7 source
into a real KMDF `.sys` image with the exact H7 generated embedded firmware
provider.

The linked SYS is deliberately destroyed on the CI runner before artifact
staging. H8 provides no INF, installer, signing package or downloadable driver.

No physical Lenovo execution is authorized.

## Why link a real SYS now

The previous WDK project compiled all kernel components as a static library.
That validates WDK headers/ABI and source compilation, but cannot expose every
final-link issue such as:

- unresolved KMDF/kernel imports;
- missing production translation units;
- invalid DriverEntry linkage;
- unexpected C++ runtime dependencies;
- generated firmware provider resolution;
- final PE driver generation.

H8 adds a separate WDK project for this final-link test while preserving the
existing static-library project and all host regressions.

## Real driver project

`m062/driver/phaser360_m1_boot.vcxproj` is a separate x64 Release KMDF project:

- `ConfigurationType=Driver`;
- `DriverType=KMDF`;
- Windows kernel-mode driver toolset;
- KMDF 1.33;
- `SignMode=Off`;
- warnings treated as errors;
- C++17 with exceptions and RTTI disabled;
- no INF item.

The project directly compiles the production boot path rather than linking only
the prior static component library. It includes:

- DriverEntry / EvtDriverDeviceAdd;
- DeviceOwner;
- H7 firmware source contract and generated provider;
- PinnedFirmware/CNG pin;
- PnP resource/lifecycle ownership;
- repeated per-D0 boot ownership;
- interrupt bridge;
- ColdPower;
- GLK boot/HDA transport/DMA;
- IPC3 and HDA support code.

The generated firmware source is supplied through the MSBuild
`GeneratedFirmwareSource` property and exists only in the CI build directory.

## H8 DeviceAdd connection

H7 deliberately left the provider un-staged.

H8 changes `DeviceOwner::Initialize` to the following fail-closed order:

1. attach PnpResources;
2. install the repeated-D0 lifecycle;
3. `StageEmbeddedFirmware()`;
4. require successful owned-copy CNG authorization;
5. create the dormant interrupt shell;
6. return DeviceAdd success.

This still performs no MMIO and no DSP boot from DeviceAdd.

If provider generation, staging, allocation or the runtime CNG pin fails,
DeviceAdd fails before the framework can later reach the reviewed
PrepareHardware/D0 boot path.

There is still no arbitrary firmware-buffer API and no runtime firmware file
I/O.

## CI-only generated firmware

Before linking the H8 project, the WDK workflow:

1. builds the existing offline inspector;
2. obtains the fixture through the existing pinned-reference verifier;
3. verifies its exact size/SHA-256;
4. generates the H7 embedded provider into the build directory;
5. passes that generated C++ file to the H8 WDK project.

The firmware remains:

- absent from Git;
- absent from the H8 development artifact as `.ri`;
- absent as generated C++ source from the artifact.

## Linked SYS destruction rule

After MSBuild succeeds, CI requires:

- the expected `phaser360_m1_boot.sys` exists;
- it is at least a minimal non-empty driver image;
- it begins with the PE/DOS `MZ` signature;
- SHA-256 and byte size are recorded.

CI then writes only `H8_DRIVER_LINK_REPORT.txt` and metadata reports.

Before artifact staging it must execute:

`Remove-Item -LiteralPath $sys -Force`

and prove the SYS no longer exists.

The generated provider and local firmware fixture are also deleted.

The artifact staging step recursively rejects any `.sys` or `.ri` file.

Therefore H8 can prove a real final driver link without distributing an
installable binary.

## Static H8 safety guard

`tests/m0615_h8_link_only_tests.ps1` requires:

- a real KMDF Driver project;
- no INF item;
- generated firmware source supplied explicitly;
- lifecycle install -> embedded firmware stage -> IRQ shell ordering;
- no kernel runtime firmware file I/O;
- no MAX98357A/DA7219 or Windows audio stack path in DriverEntry/DeviceOwner;
- workflow link marker;
- explicit SYS deletion;
- `SYS_UPLOADED=FALSE`;
- `INF=ABSENT`;
- `AUDIO_PLAYBACK=NOT_IMPLEMENTED`;
- no workflow command copying the linked SYS into the development artifact.

## Still not a physical M1 package

H8 does not provide:

- a downloadable `.sys`;
- INF hardware binding;
- catalog/signing package;
- test certificate;
- pnputil/devcon install command;
- controller bind/restart;
- WinRE recovery package;
- target hardware execution.

It also contains no:

- SSP1 programming;
- MAX98357A enable;
- SSP2/DA7219 programming;
- PDM endpoint;
- WaveRT/ACX/PortCls audio endpoint;
- PCM playback.

The next stage must audit the real link result and create a separately gated
physical M1 boot package with explicit recovery and target-identity checks.
Even that package is limited to DSP boot -> FW_READY/IPC -> clean shutdown.
Audio output remains prohibited.


## Verified CI evidence (2026-09-21)

Final H8 source commit:
`1afbbd956db09b92704d7536834a75393fd8ff84`.
Tree: `b9b83d501c0160c3f2d8082c21664e71bcaf6c43`.

The first H8 link attempt at `1b6f4c80` reached the real KMDF linker and
failed only because the Driver project did not explicitly link `cng.lib`.
The six unresolved symbols were the expected BCrypt SHA-256 functions.
H8 retained the production CNG pin and fixed the real driver project by adding
`cng.lib`; no firmware-authentication logic was weakened or replaced.

Final verification:

- WDK/KMDF run `35627846131`, job `106426485103`: real WDK static
  library compilation passes and all 10 selected host regressions pass.
- PnP remains `SOF_PNP_RESOURCES_TESTS=516 PASS`.
- Repeated-D0 model remains `SOF_GLK_BOOT_TESTS=327105 PASS`.
- Pinned owner remains `SOF_PINNED_OWNER_TESTS=34 PASS`.
- H6 and H7 static safety guards both pass.
- The H8 real KMDF Driver project links successfully with the generated,
  hash-pinned embedded provider.
- Temporary linked driver:
  - bytes: `338944`;
  - SHA-256:
    `8dd3fa9cc0e0fb2e5c100c364808bcf16aef7200b11d011a688df7f1642f83b9`;
  - `SYS_UPLOADED=FALSE`;
  - `INF=ABSENT`;
  - `HARDWARE_EXECUTION=NONE`;
  - `AUDIO_PLAYBACK=NOT_IMPLEMENTED`.
- H8 static guard reports
  `H8_LINK_ONLY_STATIC_TESTS=PASS; real_kmdf_project=YES;
  embedded_stage_before_irq=YES; runtime_file_io=NO; inf=ABSENT;
  sys_upload=NO; audio_path=NO; playback=NO`.
- Windows/Linux offline run `35627846154`: Windows job
  `106426485152` passes all 14 existing tests plus
  `H7_EMBEDDED_FIRMWARE_TESTS=19 PASS`; Linux job
  `106426485342` passes all 12 tests.
- Official Windows fixture validation remains
  `SOF_CNG_PIN_TESTS=10 PASS` and
  `SOF_PINNED_REFERENCE_TESTS=13 PASS`.
- H8 report-only artifact
  `PHASER360_M0615H8_LINK_ONLY_REPORT`: ID `10653120057`,
  475,659 bytes; GitHub archive SHA-256
  `e42222cbf6dc3f14d85cf8bb52072d22525539176298f560853753327b688390`.

The linked SYS, generated provider and local `.ri` fixture are deleted before
artifact staging. H8 still distributes no installable driver and authorizes no
physical Lenovo execution.
