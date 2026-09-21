# M0.6.15H12.1 Windows 10 21H2 / KMDF 1.31 compatibility correction

H12.1 corrects the framework target used by the real WDK builds before any
installable INF/package work begins.

The physical Lenovo target currently runs Windows 10 21H2 build 19044.

Microsoft's KMDF version history states:

- KMDF 1.31 is included starting with Windows 10 version 2004 and drivers using
  1.31 run on Windows 10 version 2004 and later;
- KMDF 1.33 is included with Windows 11 version 21H2 / Windows Server 2022 and
  drivers using 1.33 require those releases or later.

The H8-H12 CI had still been explicitly overriding KMDF_VERSION_MINOR=33 even
though H12's target contract was build 19044. H12.1 fixes that incompatibility.

## Changes

Both WDK projects are retargeted to:

KMDF_VERSION_MAJOR=1
KMDF_VERSION_MINOR=31

The active workflow also passes /p:KMDF_VERSION_MINOR=31 to:

1. the real WDK static-library rebuild;
2. the first real temporary KMDF SYS link;
3. the second clean reproducibility rebuild.

KMDF 1.33 is forbidden by the H12.1 static guard in both project files and all
active driver build commands.

## Verification scope

H12.1 reruns the complete safety chain under KMDF 1.31:

- real WDK component compile;
- PnP/resource/lifecycle tests;
- telemetry mirror tests;
- repeated D0 boot/IRQ model;
- H6 DeviceAdd ownership guards;
- H7 embedded firmware source/CNG guards;
- H8 real temporary KMDF SYS link;
- H9 x64 Native/import audit;
- H10 two-link reproducibility and IMAGE_DEBUG_TYPE_REPRO audit;
- H11 read-only telemetry guards;
- H12 exact-target/WinRE preflight guards.

The temporary SYS remains CI-only and is deleted before upload.

## Target remains exact

H12.1 does not widen H12:

- Windows build remains exactly 19044;
- hardware ID remains
  PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06;
- H12 WinRE preflight remains a hard gate;
- no INF/package is created.

## Safety boundary

H12.1 changes only framework compatibility/build targeting.

It authorizes no:

- driver installation;
- controller bind/unbind;
- restart/reboot;
- registry/BCD/WinRE write;
- physical MMIO;
- DSP boot on the Lenovo;
- MAX98357A/DA7219 operation;
- SSP/PDM programming;
- WaveRT/ACX/PortCls endpoint;
- playback.

INF/package design may begin only after the H8-H12 chain is green when linked
against KMDF 1.31.


## Verified CI evidence (2026-09-21)

Final H12.1 source commit:
`220bc5306d410fa093424722402052916d35090f`.
Tree: `01a3a1441e69365e8d0231831dc9f0282882bfd8`.

- H12.1 WDK/KMDF run `35637458760`, job `106458266308`: PASS.
- Real WDK component library compile with KMDF 1.31:
  `WDK_DMA_LIBRARY=PASS`.
- PnP/resource regression remains
  `SOF_PNP_RESOURCES_TESTS=516 PASS; hardware=NOT_TOUCHED`.
- H11 telemetry mirror remains
  `H11_TELEMETRY_STATE_TESTS=14 PASS; hardware=NONE`.
- Integrated repeated-D0/IRQ model remains
  `SOF_GLK_BOOT_TESTS=327112 PASS; hardware=NONE`.
- H8 real temporary driver linked with KMDF 1.31:
  342,016 bytes, SHA-256
  `8f69789c237a7f997320c17c8297ce860bebb8fb698de084ca3334c22bc0d8b4`.
  The SYS was deleted before artifact upload.
- H9 x64/Native PE/import audit: PASS; CNG imports present, user-mode/audio
  imports absent.
- H10 reproducibility audit: PASS; both clean KMDF 1.31 rebuilds are
  bit-identical with SHA-256
  `8f69789c237a7f997320c17c8297ce860bebb8fb698de084ca3334c22bc0d8b4`
  and contain IMAGE_DEBUG_TYPE_REPRO.
- H11 read-only telemetry guard: PASS.
- H12 exact-target/WinRE preflight self/static guards: PASS.
- H12.1 compatibility guard reports
  `H12_1_WIN10_KMDF_COMPAT=PASS; target_build=19044;
  kmdf_target=1.31; kmdf_1_33=FORBIDDEN;
  real_wdk_links=ALL_1_31; sys_upload=NO; inf=ABSENT;
  physical_execution=NO; playback=NO`.
- Windows/Linux offline run `35637467388`: both jobs PASS.
- Development artifact
  `PHASER360_M0615H12_1_WIN10_KMDF31_COMPAT`: ID
  `10656329773`, 527,154 bytes; GitHub archive SHA-256
  `9fa25387d060b87575cce82606952ce83f18ab100e420da9f7b441e52c49dc32`.

H12.1 distributes no SYS/INF/CAT/firmware/certificate package and authorizes
no physical hardware execution or playback.
