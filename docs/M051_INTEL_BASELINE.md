# M0.5.1 stopped with Intel SST already bound

## Confirmed target state

The target journal and the subsequent DISM output supplied on 2026-09-14 establish:

| Property | Observed value |
| --- | --- |
| Controller | PCI vendor 8086, device 3198, subsystem 00000000, revision 06 |
| PnP problem code | 0 |
| Bound service | IntcAudioBus |
| Published INF on this installation | oem32.inf |
| Original INF | intcaudiobus.inf |
| Provider | Intel(R) Corporation |
| Driver version | 9.22.0.4883 |
| DISM Boot Critical field | Yes |

This is the retired Intel SST version already discussed in the project. These
observations do not establish who installed it or how it returned. OEM INF
numbers are assigned by Windows and must not be reused across installations.

The 5A98, 0A98 and 1A98 entries in Get-DriverInfo describe other models supported
by the package; they are not an inventory of controllers present in this laptop.
Problem code 0 does not establish working audio endpoints or sound.

## Why the probe stopped

M0.5.1 requires the target to have problem code 28 and no service/INF binding.
The installed Intel driver does not satisfy that condition. The supplied
transaction stopped before trust, staging or binding: all three attempt flags
are false, and Snapshot is null.

STOPPED_CLEAN and CLEAN=True describe the probe's own changes and cleanup.
They do not mean Windows is globally free of drivers. No further hardware
inventory is needed to explain this rejection. Changing signing settings or
repeating the same probe cannot remove the existing binding.

The baseline guard must remain in place. A transition away from the installed
Intel package is a separate operation, outside the M0.5.1 transaction.

## Next action: preserve the package and check recovery access

Run the following in an administrator PowerShell window on the same Windows
installation. It exports the observed package into a new directory and displays
Windows RE configuration. It does not uninstall or disable a device, change
boot settings, or restart Windows.

~~~powershell
& {
    $phaserBackup = Join-Path $env:SystemDrive (
        'PHASER360_INTEL_BACKUP_' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $phaserBackup -ErrorAction Stop | Out-Null
    & "$env:SystemRoot\System32\pnputil.exe" /export-driver oem32.inf $phaserBackup
    if ($LASTEXITCODE -ne 0) { throw 'Driver export failed; keep the command output.' }
    Write-Host "Package export directory: $phaserBackup"
    & "$env:SystemRoot\System32\reagentc.exe" /info
    if ($LASTEXITCODE -ne 0) { throw 'Windows RE query failed; keep the command output.' }
}
~~~

Keep the export and copy it to external storage. Export preserves package files;
it is not a full Windows backup or a tested rollback. It is not an instruction
to reinstall the retired Intel stack. A Windows RE Enabled result reports
configuration; it does not prove that recovery can boot or access the Windows
volume.

Before a package-removal procedure is finalized, establish an accessible Windows
recovery/installation USB or working Windows RE, and an appropriate recoverable
backup. If the Windows volume is encrypted, recovery must be able to unlock it.
The transition also needs the exact current service state and the devices using
that package; this defines the scope of removal, not a repeat of the hardware
identification audit.

## Recovery boundary

DISM marks this package Boot Critical: Yes. That is a reported flag, not proof
that uninstalling this audio package will necessarily prevent boot. Microsoft
explicitly warns that removing boot-critical packages from an offline image
can make that image unbootable. Combined with the project's recorded prior
boot-loop, it rules out treating deletion as routine probe cleanup.

The existing RECOVER_WINRE.cmd disables only phaser360_m051_mmio_ro. It does not
recover from removing Intel SST, restore its device stack, or repair arbitrary
Windows boot failures. Do not relabel that script as Intel recovery.

PnPUtil package uninstall applies to every device using the selected package.
Removing only the devnode leaves the package available for reinstallation.
Do not force deletion, manually erase DriverStore files, or blindly remove an
OEM number copied from another Windows installation. No Intel removal or
restoration is implemented by this document.

## What remains for sound

A successful M0.5.1 run would provide register readings only. No MMIO snapshot
has been obtained from the current installation. The existing M0.6 firmware
parser runs offline; DSP boot/authentication, IPC, codec integration and WaveRT
remain unimplemented. Exporting or removing Intel SST does not supply them.

## Primary references checked

- [Microsoft: PnPUtil syntax, export and package-wide uninstall](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax)
- [Microsoft: device and driver-package uninstallation](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/how-devices-and-driver-packages-are-uninstalled)
- [Microsoft: DISM driver servicing and the boot-critical removal warning](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/dism-driver-servicing-command-line-options-s14?view=windows-11)
- [Microsoft: REAgentC and recovery configuration queries](https://learn.microsoft.com/en-us/windows-hardware/manufacture/desktop/reagentc-command-line-options?view=windows-11)
- [Microsoft: system/critical-volume backup options](https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/wbadmin-start-backup)

The last reference describes backup options; no backup is created or validated
by the export commands above.
