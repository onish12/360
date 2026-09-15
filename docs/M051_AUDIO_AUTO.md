# PHASER360 AUDIO AUTO

Run RUN_AUDIO.cmd as administrator. The package includes the compiled x64
M0.5.1 probe (kernel contract 0.5.1.0) and coordinator 1.0. No compiler or WDK
is needed on the laptop. The coordinator chooses reviewed actions; it does
not generate arbitrary kernel code from a diagnostic report.

| Current state | Automatic action |
| --- | --- |
| Exact target, code 28, no service/INF, signing permits test | Run compiled probe, verify snapshot and cleanup, combine results |
| Unbound target, signing blocked or unknown | Report the signing prerequisite |
| Bound Intel SST 9.22.0.4883 | Export actual OEM package and verify files; capture service, bindings and recovery evidence |
| Intel package marked Boot Critical | Report PREPARED_INTEL_BOOT_CRITICAL after verified export; no Intel deletion |
| Missing or untyped BootCritical | Keep it unknown; never coerce a string to a Boolean |
| Other resolved OEM driver | Preserve actual package and report its identity |
| Prior experimental driver | Report its presence; recovery needs an ownership journal |
| Wrong target, changed binding, failed export, absent child result | Report failure without claiming a successful probe |

The live DEVPKEY_Device_DriverInfPath selects the OEM package dynamically.
Get-WindowsDriver supplies its path, version, provider and typed BootCritical.
Source and exported files are compared by relative path, size and SHA-256.
Missing, additional and changed files prevent a verified export. Reparse
entries are rejected. Source files and live binding are checked again.

The report includes PnP stack/relations, service registry values, WMI-reported
package bindings, audio devices and REAgentC output. WMI bindings are not proof
of exclusive package use. Localized output is retained, never interpreted as
authorization to remove a driver. BitLocker reads select status fields only;
recovery keys are not collected.

Results are under PHASER360_AUDIO on the Windows volume. Complete DriverBackup
stays outside RESULT_AUDIO_*.zip; the report contains metadata, file hashes and
the INF as text. Nothing is uploaded automatically.

The coordinator rechecks state before launching the probe. M0.5.1 retains its
own preflight, resource checks, ownership validation and cleanup. Separate
mutexes avoid blocking the child. Success requires exit zero, a snapshot and
confirmed cleanup; missing child output stays unconfirmed.

Existing Intel/other-driver removal is not implemented. Export is not a full
Windows backup. REAgentC /info does not prove recovery can boot or access the
Windows volume. RECOVER_WINRE.cmd disables only the experimental service.

CI runs PowerShell 5.1 failure/filesystem tests, existing transaction and C++
resource tests, and WDK compilation. SYS is test-signed before CAT generation
and signing. CI downloads the artifact and verifies its manifest again.
This does not establish hardware compatibility or working audio.
DSP loading, IPC, codecs and WaveRT remain unimplemented.

## Primary references checked

- [PnPUtil](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax)
- [Get-WindowsDriver](https://learn.microsoft.com/en-us/powershell/module/dism/get-windowsdriver)
- [Typed PnP properties](https://learn.microsoft.com/en-us/powershell/module/pnpdevice/get-pnpdeviceproperty)
- [REAgentC](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/reagentc-command-line-options)
- [DISM boot-critical removal warning](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/dism-driver-servicing-command-line-options-s14)
- [Device and package uninstallation](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/how-devices-and-driver-packages-are-uninstalled)
- [Win32_PnPSignedDriver](https://learn.microsoft.com/en-us/previous-versions/windows/desktop/whqlprov/win32-pnpsigneddriver)
- [Win32_SystemDriver](https://learn.microsoft.com/en-us/windows/win32/cimwin32prov/win32-systemdriver)
