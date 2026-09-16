# Controlled Intel-to-probe handoff, coordinator 1.1

This implements the next operation for the 2026-09-16 report, not audio playback.
The M0.5.1 kernel stays read-only. DSP boot, firmware transport, IPC, codec
integration and WaveRT are still absent. There is no claim of laptop validation
until a successful hardware report is returned.

## Evidence and scope

The supplied report identifies Intel SST 9.22.0.4883, running on the exact
Phaser360 controller, with service Start=3. DISM reports BootCritical=true.
These are different properties: demand start does not override the package
flag or establish that package deletion is safe. This implementation does
not delete the Intel package or change its start policy.

It accepts the package only when the INF, SYS and CAT hashes and lengths match
the supplied report. The live system32 SYS is checked too. OEM numbers are
resolved dynamically. A verified export is repeated in the current run and
its bytes are checked again before any device change. Old exports alone are
not an authorization to act on a changed machine.

The report confirms both ACPI codecs and the DSP child are present. Their
presence does not establish correct drivers or working sound. Intel display
audio is a child of the same controller: switching its parent may interrupt
HDMI/display audio and remove/recreate child devnodes. Close audio applications
before the test.

## Transaction

1. Verify the distributed manifest, current controller/package, Intel service,
   export, signing state and absence of an earlier probe.
2. Copy recovery files and the compiled helper to a local recovery directory.
   Flush the ownership journal before certificate, staging and binding actions.
3. Import only the package's public test certificate where absent; record
   ownership. Stage only the probe package and identify its actual OEM name
   using its INF hash and the pre-run DriverStore inventory.
4. Recheck the Intel binding, service, package bytes and signing state.
5. Use SetupDiOpenDeviceInfoW for the exact instance and DI_ENUMSINGLEINF for
   the verified staged probe. Require one compatible model, exact provider
   and version. DiInstallDevice receives that driver explicitly. No global
   hardware-ID update, forced package removal or Windows Update search is used.
6. Disable only the probe service for subsequent boots after checking its
   ImagePath. Read the existing M0.5.1 snapshot contract.
7. If the probe owns the device, use DiInstallDevice with
   DIIDFLAG_INSTALLNULLDRIVER. Require code 28 and no service/INF afterward.
   Delete only the now-unused probe package, without /uninstall or /force.
8. Verify Intel's package files and service policy remain intact; remove only
   certificates introduced by this run and report the final binding.

If binding never changed, cleanup leaves Intel bound. If an unexpected driver
is found, cleanup refuses to replace it. Failure/reboot requests remain explicit;
they never become a successful snapshot just because an API returned success.
No automatic reboot is requested. The Windows installer can leave pending work;
a reported reboot requirement remains a recovery condition, not success.
The probe service is disabled and its package is retained when binding requests
a reboot. The recovery cleanup requires a different Windows boot before trying
again. The native result preserves both Win32 error and reboot state even when
DiInstallDevice returns failure. No installer is retried concurrently.

The final successful state is deliberately **UNBOUND**, not restoration of the
initial Intel binding. CLEAN means probe package/certificate cleanup and a
verified final state; it does not mean sound works or the baseline was restored.
Intel files remain available to Windows. This code does not guarantee that
Windows will never select them again after later servicing, reboot or a rescan.
It does not block updates or modify driver-ranking policy. A rerun on the
unbound target with retained Intel stops instead of invoking legacy cleanup.

## Signing and recovery

The reported CodeIntegrityOptions=5 does not allow the test-signed probe. The
coordinator stops before trust/staging/binding in that session. START_AICI.txt
describes Windows Startup Settings option 7/F7. No BCD, Secure Boot, Memory
Integrity or encryption settings are changed by the script.

Recovery files are written before mutation under
`\PHASER360_M051_RECOVERY\<run>`. From the Windows Recovery Command Prompt,
identify the Windows volume and run that folder's RECOVER_WINRE.cmd. It
disables only the probe service in the offline SYSTEM hive. After Windows
boots, run that folder's CLEANUP_AUDIO.cmd as administrator. The journal's
Mode chooses this handoff cleanup, so it never falls back to package-wide
uninstall that could select Intel again. Recovery refuses changed ownership.

WinRE /info reports configuration only. Recovery access is not automatically
boot-tested, and the package export is not a Windows image backup. The offline
helper cannot recover arbitrary Windows failures. If the volume is encrypted,
its recovery key must be available. No keys are collected.

## Verification

CI exercises partial staging, binding refusal/partial binding, concurrent
binding changes, failed reads, detach failure, reboot requests, deletion
failure, journal failure and changed Intel state. It compiles the helper for
.NET Framework x64, checks managed structure sizes against C++ assertions on
the Windows SDK, loads the built DLL on Windows PowerShell 5.1, builds the WDK
driver, signs the package and verifies the downloaded artifact. CI does not
perform device installation or claim physical recovery validation.

## Official references

- [DiInstallDevice: explicit device/driver, null binding and reboot result](https://learn.microsoft.com/en-us/windows/win32/api/newdev/nf-newdev-diinstalldevice)
- [SetupDiOpenDeviceInfoW: selecting one instance](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdiopendeviceinfow)
- [SP_DEVINSTALL_PARAMS: single-INF enumeration](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/ns-setupapi-sp_devinstall_params_w)
- [SetupDiBuildDriverInfoList](https://learn.microsoft.com/en-us/windows/win32/api/setupapi/nf-setupapi-setupdibuilddriverinfolist)
- [PnPUtil: staging, export and deletion scopes](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax)
- [INF AddService: StartType=3](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/inf-addservice-directive)
- [Code Integrity option bits](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation)
- [Windows Startup Settings](https://support.microsoft.com/en-us/windows/experience/startup-boot/windows-startup-settings)
