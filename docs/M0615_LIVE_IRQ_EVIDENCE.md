# M0.6.15F read-only live IRQ evidence

This milestone does not install or bind a driver. It exists only to capture the
Windows resource view for the already-identified Gemini Lake controller
`PCI\VEN_8086&DEV_3198`.

Microsoft documents `pnputil /enum-devices /instanceid <ID> /resources` for
displaying device resources; the `/resources` switch is available beginning
with Windows 11 version 22H2. The target machine's previously verified build is
22621, so this is the first live evidence path.

## Safety contract

`COLLECT_IRQ_EVIDENCE.cmd` invokes only `Collect-IrqEvidence.ps1`.
The PowerShell collector has a runtime allow-list that permits PnPUtil only when
the first verb is `/enum-devices`, requires an exact DEV_3198 instance, and
allows only read-only enumeration switches used by this package.

The collector does not call add-driver, install, delete-driver, restart-device,
disable-device, enable-device, remove-device, scan-devices, SetupAPI binding,
registry writes, MMIO, WdfInterruptCreate, DSP boot or audio playback.

The launcher deliberately ends with `pause` so failures remain visible.

## Captured files

- `target_before.json`: selected identity/binding/problem fields before capture.
- `device_properties.json`: all Get-PnpDeviceProperty values for the exact
  present DEV_3198 instance.
- `pnputil_resources.txt`: raw PnPUtil resource enumeration plus exact arguments
  and exit code.
- `pnputil_full.txt`: the same exact instance with device IDs, services, stack,
  drivers, properties and resources.
- `target_after.json`: selected fields after capture.
- `RESULT.txt`: safety/status markers.
- `SHA256SUMS.txt`: SHA-256 for every evidence file.

The script compares InstanceId, ProblemCode, Service, DriverInfPath,
DriverVersion and DriverProvider before and after. A difference is reported as
`STATE_CHANGED_DURING_CAPTURE`; the script does not claim it caused that
change.

## Interpretation limit

PnPUtil output is captured verbatim and is not parsed into a line/MSI/MSI-X
decision by the collector. M0.6.15E defines the kernel descriptor contract, but
this user-mode evidence is accepted only if it contains enough information to
establish the assigned interrupt shape. If it does not, IRQ selection remains
deferred and a separate read-only kernel resource probe must be designed.

No value in this evidence package authorizes DSP boot, interrupt binding,
MAX98357A enable, codec programming or playback.
