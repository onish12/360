# M0.5.1: fresh-Windows read-only probe

The current user entry point is [RUN_AUDIO.cmd](M051_AUDIO_AUTO.md), which handles
an existing driver by preserving its package and gathering transition evidence.
It invokes this compiled probe only when its prerequisites are met.

This continuation replaces the retired M0.5 **installer**, not the installed Intel
stack. It accepts only the unique present exact PCI target with problem code 28,
no function-driver service and no INF binding. The hardware identity and NHLT
audit are already established; this is not another hardware discovery request.

The current installation has since been confirmed to bind Intel SST 9.22.0.4883
as `IntcAudioBus` / `oem32.inf`, with DISM reporting `Boot Critical: Yes`.
The resulting `BASELINE_NOT_CLEAN` stop is expected; no current-installation
MMIO snapshot has been obtained. Follow the
[installed-Intel baseline notes](M051_INTEL_BASELINE.md) for package preservation
and the separate recovery requirement. `CLEAN=True` refers to this probe's own
changes, not to the absence of other Windows drivers. Its WinRE script disables
only the experimental service and does not recover an Intel package removal.

The driver has a new service/file name and interface GUID. It maps the two PnP
translated memory resources read-only/uncached. Resource order and exact lengths
identify BAR0/HDA and BAR4/DSP; addresses are no longer hard-coded to the previous
Windows installation. Alignment, signed physical-address bounds and disjointness
are checked before mapping. The only reads are HDA GCAP/VMIN/VMAJ and DSP ADSPCS/ADSPIS.
There is no firmware, IPC, DMA, interrupt handler, device-register write or audio endpoint.
Windows PnP/power machinery still participates in starting the device; read-only
MMIO does not make installing a kernel driver risk-free.

The PowerShell transaction records ownership before mutations, stages without
forcing driver selection, and discovers the new OEM INF using structured DISM
objects plus the exact INF SHA-256. It rechecks the unbound target before binding.
It disables its own service for subsequent boots once bound, reads one snapshot,
then removes only its package. Cleanup repeats ownership validation; ambiguous,
preexisting or changed packages are never deleted. Certificate cleanup removes
only the exact certificate/store combinations added by this invocation. Normal
preflight failure never triggers Intel restoration or a false rollback alarm.

Before binding, a durable journal and WinRE service-disable script exist on the
Windows volume. Recovery also includes a Windows cleanup script that checks current
package ownership again. No automatic reboot, forced driver replacement, Intel
restoration, BCD change or automatic security-setting change is implemented.
Native PnP calls are synchronous: cleanup never runs alongside an unfinished
installer. Abrupt termination/kernel failure cannot execute a PowerShell finally
block; use the recovery copy in that case. Windows Update or another installer can
change binding concurrently; the script detects changed baseline/ownership and
reports it instead of modifying the other driver.

## Build evidence versus hardware evidence

CI compiles x64 KMDF with WDK 10.0.28000.2526, validates the INF, embeds a test
signature in SYS, then regenerates CAT over those exact bytes and signs CAT.
The public certificate is shipped; the private key is not exported. Cryptographic
CMS verification is done without importing the certificate into the CI runner's
Root/TrustedPublisher stores. Local trust, Microsoft production signing and
hardware compatibility are separate properties. Runtime checks active code
integrity before any certificate or driver install; normal production enforcement
can stop this test package. See the package's START_AICI.txt.

Pure resource-contract tests and injected transaction-failure tests run offline.
CI does not install the driver, access the target hardware, or establish HVCI,
suspend/resume, firmware compatibility, or working sound. M0.6 remains a separate
offline SOF container parser. DSP boot, IPC, codecs and WaveRT still follow.

## Primary API references

- [PnP resource-list order](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/mapping-bus-relative-addresses-to-virtual-addresses)
- [MmMapIoSpaceEx](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-mmmapiospaceex)
- [PnPUtil syntax](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/pnputil-command-syntax)
- [Get-WindowsDriver](https://learn.microsoft.com/en-us/powershell/module/dism/get-windowsdriver)
- [Active code-integrity options](https://learn.microsoft.com/en-us/windows/win32/api/winternl/nf-winternl-ntquerysysteminformation)
- [Device interfaces restricted by instance ID](https://learn.microsoft.com/en-us/windows/win32/api/cfgmgr32/nf-cfgmgr32-cm_get_device_interface_listw)
- [Inf2Cat](https://learn.microsoft.com/en-us/windows-hardware/drivers/devtest/inf2cat)
- [Test signing](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/the-testsigning-boot-configuration-option)
- [Temporary startup settings and BitLocker recovery](https://support.microsoft.com/en-us/windows/experience/startup-boot/windows-startup-settings)
