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


## M0.6.15F2 — Windows 10 21H2 compatibility

The Lenovo currently runs Windows 10 21H2 build 19044. The original F live
collector intentionally refused that build because Microsoft's PnPUtil
`/enum-devices /resources` switch is available only beginning with Windows 11
22H2. F2 preserves the Windows 11 path and adds a Windows 10 read-only path.

For builds 19044 through 22620, the collector uses SetupAPI only to read
`SPDRP_ALLOC_CONFIG` for the exact present DEV_3198 instance. That property is
returned as a `CM_RESOURCE_LIST`. The collector stores the original binary and
a parsed JSON inventory. It does not call a SetupAPI set/install/class-installer
operation and does not write the registry.

The first F2 candidate treated each `CM_PARTIAL_RESOURCE_DESCRIPTOR` as
32 bytes. A second independent ABI audit caught that error before the collector
was run on the target: WDK defines this structure under `pshpack4.h`, and on
the x64 WDK ABI its size is 20 bytes, with the union beginning at byte 4.
That F2 parser and artifact are withdrawn. F3 derives the layout through
`StructLayout(Pack=4)` and requires the real WDK build to compile matching
`static_assert` checks.

The Windows-defined `CM_RESOURCE_INTERRUPT_MESSAGE` bit (0x0002) remains the
sole IRQ-kind discriminator:

- flag clear -> LINE;
- flag set -> MESSAGE.

F2 deliberately does not call a one-descriptor MESSAGE result "MSI" or "MSI-X".
MSI can use one descriptor for multiple messages while MSI-X normally supplies
one descriptor per message, so the user-mode allocated list is evidence for the
message-vs-line shape, not authorization to choose a WDF interrupt object.

The Windows 10 path records:
- `setupapi_alloc_config.bin`: untouched `CM_RESOURCE_LIST` bytes;
- `setupapi_alloc_config.json`: bounded parse, descriptor flags and IRQ kinds;
- the same before/after device binding/problem state and SHA-256 manifest as F.

The self-test compiles the read-only SetupAPI interop type but does not enumerate
a real device. It parses a synthetic x64 resource list containing one LINE and
one MESSAGE interrupt and requires both classifications to match. Static tests
also reject known mutating PnP/SetupAPI/ConfigMgr commands.

Primary contracts rechecked:
- PnPUtil `/resources` requires Windows 11 22H2 or newer.
- `CM_RESOURCE_LIST` is the Windows structure containing assigned resources.
- `CM_PARTIAL_RESOURCE_DESCRIPTOR.Flags & CM_RESOURCE_INTERRUPT_MESSAGE`
  distinguishes message-signaled from line-based interrupt resources.
- No IRQ descriptor is selected and `WdfInterruptCreate` remains prohibited.

No driver install/bind, device restart, MMIO, DSP boot, codec/amplifier action
or playback is authorized by F2.


## F2 verified CI evidence (2026-09-21)

Implementation commit: `fa7e9c0da683d021aee5974f1e0f4499dc7a6f77`.
Tree: `b9cb0165790522c28fcd8a56e7472832470436f3`.

- Workflow run `35576054605`, WDK job `106258057260`: real WDK/KMDF
  compilation passes.
- All 10 selected kernel/host regression tests pass. PnP remains
  `SOF_PNP_RESOURCES_TESTS=360 PASS; irq_inventory=LINE_AND_MESSAGE;
  irq_selection=DEFERRED; power_skeleton=REGISTERED;
  surprise_callback=REGISTERED; paired_raw_translated=YES;
  hardware=NOT_TOUCHED`.
- The dual-path collector self-test reports
  `IRQ_CAPTURE_SELFTEST=PASS; readonly_setupapi=YES; readonly_pnputil=YES;
  line_and_message=PASS; mutation_commands=REJECTED`.
- Static checks report
  `IRQ_CAPTURE_STATIC_TESTS=PASS; syntax=PASS; launcher_pause=YES;
  pnputil_wrapped=YES; setupapi_readonly=YES; win10_path=YES`.
- Development artifact
  `PHASER360_M0615F2_WIN10_WIN11_READONLY_IRQ_CAPTURE`: ID
  `10627996546`, 291,836 bytes. GitHub reports archive SHA-256
  `f4cbbc520bf68cadec4a29d7c71c06a803faf27d40497db530a176ff9f2b1e2f`.

Despite the green F2 CI run, the artifact above is **withdrawn and must not be
used** because its synthetic test repeated the same incorrect 32-byte layout
assumption as its parser. It was not run on the Lenovo.

## M0.6.15F3 — WDK-verified pack(4) resource ABI

F3 keeps the same read-only SetupAPI/PnPUtil split, but corrects the Windows 10
parser to the WDK packing contract. The production WDK static-library build now
contains compile-time assertions that require:

- `sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR) == 20`;
- `FIELD_OFFSET(CM_PARTIAL_RESOURCE_DESCRIPTOR, u) == 4`;
- `FIELD_OFFSET(CM_RESOURCE_LIST, List) == 4`;
- the first partial descriptor is 16 bytes after the start of a
  `CM_FULL_RESOURCE_DESCRIPTOR`;
- `CM_RESOURCE_INTERRUPT_MESSAGE == 0x0002`.

The PowerShell interop independently derives the same values from managed
`StructLayout(Pack=4)` mirrors. Its self-test requires descriptor=20,
unionOffset=4, firstFull=4 and fullHeader=16 before parsing a 60-byte synthetic
resource list with one LINE and one MESSAGE interrupt.

F3 no longer interprets vector/affinity fields from the allocated-config union.
For the current evidence goal it copies each complete 20-byte descriptor,
preserves the 16-byte union as hex, and classifies an interrupt only from Type,
ShareDisposition, Flags and the MESSAGE bit. This intentionally minimizes ABI
assumptions.

F3 remains read-only and still does not select an IRQ, create a WDF interrupt,
write MMIO, boot the DSP, program either codec/amplifier or produce audio.


## F3 verified CI evidence (2026-09-21)

Implementation commit: `49d44ab3dcf3825f97f33cb071378595ac03b478`.
Tree: `f03d1caab753432643d1fcd927fdd696aa676aed`.

- Workflow run `35576846207`, WDK job `106260501932`: real WDK/KMDF
  compilation passes with the compile-time CM resource ABI assertions enabled.
- All 10 selected kernel/host regression tests pass. PnP remains
  `SOF_PNP_RESOURCES_TESTS=360 PASS; irq_inventory=LINE_AND_MESSAGE;
  irq_selection=DEFERRED; power_skeleton=REGISTERED;
  surprise_callback=REGISTERED; paired_raw_translated=YES;
  hardware=NOT_TOUCHED`.
- Collector self-test reports
  `IRQ_CAPTURE_SELFTEST=PASS; cm_pack4=PASS; descriptor20=PASS;
  readonly_setupapi=YES; readonly_pnputil=YES; line_and_message=PASS;
  mutation_commands=REJECTED`.
- Static guards report
  `IRQ_CAPTURE_STATIC_TESTS=PASS; syntax=PASS; launcher_pause=YES;
  pnputil_wrapped=YES; setupapi_readonly=YES; cm_pack4_guard=YES;
  win10_path=YES`.
- Development artifact
  `PHASER360_M0615F3_PACK4_VERIFIED_READONLY_IRQ_CAPTURE`: ID
  `10628423518`, 297,160 bytes. GitHub reports archive SHA-256
  `6d676b3eec7db5b1ecbed3712a753280347777383c92e796256b17473d1b62e7`.

F3 is cleared only for the read-only live evidence capture on the Windows 10
21H2 target. It still does not authorize IRQ binding, WdfInterruptCreate, DSP
boot, codec/amplifier programming or playback.


## M0.6.15F4 — Configuration Manager allocated-resource fallback

The first live F3 execution on the Windows 10 21H2 target failed before any
capture because SetupDiGetDeviceRegistryProperty(SPDRP_ALLOC_CONFIG) did not
return that property for the present DEV_3198 devnode. Microsoft documents
ERROR_INVALID_DATA when the requested property does not exist or its data is
not valid. F3 performed no mutation and is therefore withdrawn for this target.

F4 removes SPDRP_ALLOC_CONFIG from the Windows 10 path. It resolves the exact
DEV_3198 devnode with CM_Locate_DevNodeW, obtains its ALLOC_LOG_CONF through
CM_Get_First_Log_Conf, enumerates resource descriptors with
CM_Get_Next_Res_Des, and copies each descriptor through
CM_Get_Res_Des_Data_Size plus CM_Get_Res_Des_Data.

Only handle-release functions CM_Free_Res_Des_Handle and
CM_Free_Log_Conf_Handle are used. Mutating APIs such as CM_Add_Res_Des,
CM_Modify_Res_Des, CM_Free_Res_Des and CM_Free_Log_Conf are forbidden by the
static test.

The collector stores every returned resource descriptor as a separate binary
file and writes cfgmgr_alloc_resources.json with its ResourceId, byte count and
SHA-256. ResType_IRQ is inventoried as
IRQ_RESOURCE_SIGNALING_UNDETERMINED. Configuration Manager's IRQ_RESOURCE
format describes the allocated IRQ resource but does not carry the
CM_RESOURCE_INTERRUPT_MESSAGE discriminator used by the kernel
CM_PARTIAL_RESOURCE_DESCRIPTOR. F4 therefore does not infer LINE, MSI or MSI-X.

A successful F4 capture can establish that an allocated IRQ resource exists and
preserve its exact Configuration Manager bytes. If signaling type remains
undetermined, IRQ selection remains deferred and a later separately-reviewed
read-only kernel resource probe is required before WdfInterruptCreate.

No driver install/bind, restart, MMIO, DSP boot, codec/amplifier programming or
playback is authorized by F4.


## F4 verified CI evidence (2026-09-21)

Implementation commit: `5d36b8ac460ba01d1919236d4d7aa1da14872afd`.
Tree: `fddec39eeda21890a0e8145bb763af5af75608b7`.

- Workflow run `35578706182`, WDK job `106266326048`: real WDK/KMDF
  compilation passes.
- All 10 selected kernel/host regression tests pass. PnP remains
  `SOF_PNP_RESOURCES_TESTS=360 PASS; irq_inventory=LINE_AND_MESSAGE;
  irq_selection=DEFERRED; power_skeleton=REGISTERED;
  surprise_callback=REGISTERED; paired_raw_translated=YES;
  hardware=NOT_TOUCHED`.
- Collector self-test reports
  `IRQ_CAPTURE_SELFTEST=PASS; cfgmgr_alloc_log_conf=YES;
  cfgmgr_readonly=YES; irq_signal_type=UNDETERMINED;
  mutation_commands=REJECTED`.
- Static guards report
  `IRQ_CAPTURE_STATIC_TESTS=PASS; syntax=PASS; cfgmgr_readonly=YES;
  alloc_log_conf=YES; mutation_api_guard=YES; win10_path=YES`.
- Development artifact `PHASER360_M0615F4_CFGMGR_READONLY_IRQ_CAPTURE`:
  ID `10628862002`, 296,028 bytes. GitHub reports archive SHA-256
  `c586a5ae8750ecf3a2c4c5293dcf18b1f8efb4ec3d1b5c8a23da725ab86b41ae`.

F4 is cleared only for the documented read-only allocated-resource capture on
the Windows 10 21H2 target. Signaling type and IRQ selection remain deferred.
