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

Microsoft's PnPUtil guidance uses an elevated command prompt. The live collector
therefore refuses the real capture unless the current PowerShell token is an
Administrator. Its `-SelfTest` path runs before that guard so CI can verify the
allow-list without touching a device.

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


## Verified CI evidence (2026-09-21)

Final source commit: `905ea62a67e13d6824623920ddab1de92af01f7d`.
Tree: `52e38488f9932a3068451fa1e52d22e122899f53`.

- Workflow run `35575050669`, WDK job `106254900340`: real WDK/KMDF
  compilation passes.
- The selected kernel/host regression set passes 10/10 tests. PnP remains
  `SOF_PNP_RESOURCES_TESTS=360 PASS; irq_inventory=LINE_AND_MESSAGE;
  irq_selection=DEFERRED; power_skeleton=REGISTERED;
  surprise_callback=REGISTERED; paired_raw_translated=YES;
  hardware=NOT_TOUCHED`.
- Collector self-test reports
  `IRQ_CAPTURE_SELFTEST=PASS; readonly_enum_only=YES;
  mutation_commands=REJECTED`.
- Static collector checks report
  `IRQ_CAPTURE_STATIC_TESTS=PASS; syntax=PASS; launcher_pause=YES;
  pnputil_wrapped=YES`.
- Development artifact `PHASER360_M0615F_READONLY_IRQ_CAPTURE`: ID
  `10627489354`, 286,389 bytes. GitHub reports archive SHA-256
  `a471a37a6b5fe37e941368274193cdaedde4ab96d977ff9efea554e686ddda9f`.
  This is service-reported archive metadata, not an independently recomputed
  hash of a locally downloaded artifact.

Two CI-harness defects were found and corrected before closure. The first static
test looked for a direct `& $pnp` invocation even though PnPUtil was correctly
centralized in `Invoke-PnpReadOnly` as `& $PnPUtil`. The corrected test now
requires exactly one wrapped invocation and zero direct main-flow invocations.
The second harness checked a stale `$LASTEXITCODE` after a PowerShell script
that had already reported PASS; the workflow now uses PowerShell success state
and terminating errors for that script step.

M0.6.15F is therefore cleared only for the documented read-only evidence
capture. It does not authorize IRQ binding, WdfInterruptCreate, DSP boot,
codec/amplifier programming or playback.
