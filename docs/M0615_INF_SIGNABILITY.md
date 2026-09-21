# M0.6.15H13 exact-target INF signability and baseline backup

H13 validates the first M1 PnP package metadata without distributing or
installing a driver package.

It adds:

- an exact-target INF for the reviewed DEV_3198/SUBSYS_00000000/REV_06 device;
- KMDF 1.31 service metadata for Windows 10 build 19044;
- temporary CI package assembly and Inf2Cat validation for 10_VB_X64;
- a baseline Intel-driver export script that performs no install/uninstall or
  device restart.

Physical execution remains unauthorized.

## INF scope

The H13 INF binds only:

`PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06`.

There is no broad DEV_3198-only hardware match and no compatible-ID fallback.

The package uses the vendor-available MEDIA setup class and a demand-start
kernel service:

- Class = MEDIA;
- ClassGuid = {4d36e96c-e325-11ce-bfc1-08002be10318};
- ServiceType = 1;
- StartType = 3;
- ErrorControl = 1;
- ServiceBinary = %13%\phaser360_m1_boot.sys;
- KmdfLibraryVersion = 1.31.

Microsoft documents MEDIA as the multimedia/audio device setup class, kernel
function-driver services as ServiceType 1 / StartType 3 / ErrorControl 1, and
KMDF INF binding through KmdfService plus KmdfLibraryVersion.

The INF adds no upper/lower filters, audio interfaces, endpoint registration,
codec/amplifier configuration or WaveRT/ACX/PortCls integration.

## CI package validation

The workflow rebuilds the same pinned embedded-firmware driver with KMDF 1.31,
then creates a temporary directory containing only:

- phaser360_m1_boot.sys;
- phaser360_m1_boot.inf.

It locates the x64 WDK Inf2Cat tool and runs:

`Inf2Cat /driver:<temp-package> /os:10_VB_X64 /uselocaltime /verbose`.

Microsoft defines 10_VB_X64 as the signability target covering Windows 10
versions 2004, 20H2, 21H1, 21H2 and 22H2 x64.

The expected catalog is generated and INF/CAT/SYS hashes are recorded in
`H13_PACKAGE_REPORT.txt`.

H13 does not sign that catalog. Test-signing and target trust policy are a later
milestone.

After validation, CI deletes:

- the complete temporary package directory;
- the temporary linked SYS;
- generated firmware provider;
- downloaded firmware fixture.

The downloadable development artifact rejects SYS, INF, CAT, firmware,
certificate and private-key payloads.

## Baseline Intel driver preservation

`Backup-BaselineDriver.ps1` is intended for a later target-side safety step
before any experimental install.

It requires:

- administrator PowerShell;
- x64 process;
- Windows build exactly 19044;
- exactly one present DEV_3198 controller;
- exact reviewed hardware ID;
- ProblemCode 0 / Status OK;
- a non-empty currently bound service and published INF;
- experimental Phaser360M1 service not already bound.

It then executes only:

`pnputil /export-driver <current-oem-inf> <backup-directory>`.

The script hashes the exported package and records the current Intel service,
INF, version and provider.

It explicitly contains no add-driver, delete-driver, install/uninstall,
enable/disable, restart-device, BCD, registry, DISM removal, MMIO, DSP boot or
playback action.

## Safety boundary

H13 validates package shape only. It does not:

- upload a SYS/INF/CAT package;
- test-sign a package;
- trust a certificate on the target;
- add a driver to DriverStore;
- bind/unbind the controller;
- restart the device;
- reboot Windows;
- execute MMIO on the Lenovo;
- boot the physical DSP;
- program SSP1/SSP2/PDM;
- touch MAX98357A or DA7219;
- expose an audio endpoint;
- play PCM.

The next milestone must resolve test-signing/trust and produce a separately
reviewed deployment package with explicit normal and WinRE rollback gates before
any target installation can be authorized.


## Verified CI evidence (2026-09-21)

Final H13 source/cleanup commit:
`3b6f1de63c1fb5ca9b56da338507fd943b34508d`.
Tree: `5ad90bb6e6c9a00726270e02739a5fe35db30e4d`.

The preceding commit `1808a12b` already proved INF signability: Inf2Cat
reported no errors or warnings and generated the catalog successfully. That run
failed only after the PASS marker because a PowerShell cleanup expression used
`Test-Path ... -or Test-Path ...` without grouping each call. The final commit
changes only that cleanup expression.

Final verification:

- WDK/KMDF real compilation: PASS.
- All 10 selected host regressions: PASS.
- PnP remains
  `SOF_PNP_RESOURCES_TESTS=516 PASS; hardware=NOT_TOUCHED`.
- Integrated GLK/IRQ/repeated-D0 model remains
  `SOF_GLK_BOOT_TESTS=327112 PASS; hardware=NONE`.
- H8 temporary real KMDF link: PASS; 342,016-byte SYS, SHA-256
  `8f69789c237a7f997320c17c8297ce860bebb8fb698de084ca3334c22bc0d8b4`;
  SYS not uploaded.
- H9 PE/import audit: PASS; x64 Native image, production CNG imports present,
  user-mode/audio-stack imports absent.
- H10 reproducible build: PASS; both KMDF 1.31 links are bit-identical.
- H11 read-only telemetry static guard: PASS.
- H12 exact-target/WinRE preflight self-test: PASS.
- H12.1 Windows 10 / KMDF 1.31 compatibility guard: PASS.
- Inf2Cat path used:
  `Microsoft.Windows.WDK.x64.10.0.28000.2526\c\bin\10.0.28000.0\x86\Inf2Cat.exe`.
- Inf2Cat target: `10_VB_X64`.
- Inf2Cat signability result: 0 errors, 0 warnings; catalog generated.
- H13 runtime marker:
  `H13_INF2CAT=PASS; exact_hwid=YES; kmdf=1.31;
  package_uploaded=FALSE; install_executed=FALSE; playback=NO`.
- H13 static guard:
  `H13_PACKAGE_STATIC_TESTS=PASS; exact_hwid=YES; kmdf=1.31;
  service_start=DEMAND; filters=NONE; endpoints=NONE;
  baseline_export=READ_ONLY_SYSTEM; install=NO; package_upload=NO; playback=NO`.
- The temporary package directory and linked SYS are deleted before artifact
  staging.
- Development artifact
  `PHASER360_M0615H13_INF_SIGNABILITY_BASELINE_BACKUP`: ID
  `10658631955`, 531,432 bytes; GitHub archive SHA-256
  `6eeeba3015e98f90ecef9bc0dafe12a99593e38cd25746d2f63784c96ebef67c`.
- The development artifact rejects SYS, INF, CAT, firmware, certificate and
  private-key payloads.

H13 therefore establishes exact-target INF/catalog signability and the
read-only baseline-export mechanism only. It does not authorize target
installation or physical DSP execution.
