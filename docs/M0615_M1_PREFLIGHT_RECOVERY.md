# M0.6.15H12 M1 preflight and recovery contract

H12 is a safety/readiness milestone for the first physical M1 DSP-boot test.
It does not build or distribute an installable package and it does not run on
the Lenovo in CI.

The H12 artifact contains only a read-only target preflight, the exact reviewed
target contract, and recovery instructions.

## Exact first-M1 target contract

The first physical M1 package is intentionally narrower than the generic source support.

- Windows build exactly 19044 (Windows 10 21H2);
- controller hardware ID: PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06;
- HDA resource length: 0x4000;
- DSP resource length: 0x100000;
- PCI InterruptSupport: 3;
- PCI InterruptMessageMaximum: 1;
- NHLT: 3,684 bytes, SHA-256 4764aba0316e9a039a127285cc4ff9e97e22c75bfddb6d865d9f77b59dd2a6b9;
- SOF firmware: 287,488 bytes, SHA-256 40029b5a05665f19a492ef00b8c0a24c42e90d7c00fc57146e07947fd1407d5c.

The currently observed BAR bases are retained as evidence only. Future PnP mapping
still uses the resources assigned by Windows and never hard-codes those addresses.

Any OS-build or hardware-identity change requires a new review instead of widening
the first-M1 contract silently.

## Read-only preflight

m062/m1/Collect-M1Preflight.ps1 performs observation plus writes only its own result files.
It requires elevated PowerShell, a 64-bit process, Windows build 19044, exactly one
present DEV_3198 controller, the exact reviewed hardware ID, ProblemCode 0, Status OK,
the reviewed PCI interrupt capability values, a non-empty bound service/INF, and
Windows RE detected as enabled.

It records the currently bound driver service, published INF, version and provider.

It does not add/delete/export/install a driver, bind/unbind/restart/enable/disable a
device, write registry or BCD, perform MMIO, boot the DSP, issue audio commands, or reboot.

A mismatch returns M1_PREFLIGHT_BLOCKED and exit code 2. Unknown/localized WinRE status
fails closed.

## Why WinRE is a hard gate

The project has a historical boot-loop incident after earlier experimental audio work.
A physical M1 package is therefore not allowed until an offline recovery path exists.
Microsoft documents reagentc /info as the command that reports Windows RE status.

## Future normal rollback

The physical package milestone must record the custom package published oem*.inf
immediately after installation and must not automatically reboot. H12 does not perform
or script that mutation.

## Offline WinRE rollback contract

If Windows cannot boot after a future M1 installation:

1. Boot Windows Recovery Environment and open Command Prompt.
2. Identify the offline Windows volume by checking for its Windows directory; do not
   assume it is C: in WinRE.
3. List third-party drivers:
   dism /Image:<WINDOWS_VOLUME>:\ /Get-Drivers /Format:Table
4. Locate the package whose original INF is phaser360_m1_boot.inf and confirm the
   published oem*.inf matches the value recorded by the physical package.
5. Remove only that published experimental package:
   dism /Image:<WINDOWS_VOLUME>:\ /Remove-Driver /Driver:oemNN.inf
6. Reboot only after DISM reports successful removal.

Microsoft documents offline driver removal by published Oem*.inf. It also warns that
removing a driver required for boot can make an image unbootable. The future package
must therefore record the exact published package before any reboot. H12 itself never
executes DISM.

## Baseline driver preservation requirement

Before a future physical install, the next package milestone must additionally capture
and preserve the currently bound Intel package identified by H12. That backup/export is
deliberately not hidden inside this read-only preflight.

No M1 installation is authorized merely because H12 preflight returns READY.

## Hardware-output safety hold

The target contract keeps speaker amplifier enable, codec programming, audio playback,
and automatic reboot explicitly false. The H12 static guard fails if any becomes true.

The first physical M1 remains strictly:
DSP boot -> FW_READY/IPC -> clean shutdown.

No SSP1/SSP2/PDM endpoint, MAX98357A/DA7219 action or PCM playback is part of H12.

## CI validation

H12 CI requires the deterministic self-test, exact target/firmware/NHLT contract, a
WinRE parser that fails closed, no system-mutation command in the preflight script, all
H8-H11 regressions still green, and no SYS/RI/INF/CAT/certificate/private-key payload
in the development artifact.

The next milestone may design the package/INF and baseline-driver backup, but it must
preserve these H12 gates before any physical install is produced.
