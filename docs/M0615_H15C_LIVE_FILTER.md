# M0.6.15H15C-LIVE read-only PCI capture filter

This milestone prepares a device-specific KMDF upper filter for the exact
Intel SST target. CI builds and validates it, but does not publish an
installable package and does not authorize target installation.

## Why a filter

Windows user mode does not expose the raw PCI configuration-space bytes needed
for the 0x44/0x48 attestation. Replacing IntcAudioBus merely to read those bytes
would enlarge risk unnecessarily.

The H15C-LIVE driver is therefore an Extension-INF device-specific upper filter.
It calls WdfFdoInitSetFilter and leaves the existing Intel function driver in
place. Windows 10 1903 and later support declarative device filters via
DDInstall.Filters/AddFilter; the target build is Windows 10 19044.

## Hardware behavior

At EvtDevicePrepareHardware the filter constructs a fresh
PciConfigAttestation and performs the existing single 256-byte GetBusData
capture. Capture failure is stored for diagnostics but the filter returns
STATUS_SUCCESS so it cannot intentionally prevent the Intel function driver
from starting.

There is no SetBusData path, no BAR mapping, no MMIO, no DMA, no IRQ object,
no DSP boot, no firmware access and no codec/audio programming.

## IOCTL behavior

The filter exposes one read-only interface and one METHOD_BUFFERED,
FILE_READ_ACCESS IOCTL. It returns a fixed 292-byte snapshot containing:

- capture NTSTATUS and generation;
- target/vendor/header/capability summary;
- values at config offsets 0x44 and 0x48;
- all 256 conventional PCI configuration bytes.

The queue is non-power-managed. Unknown IOCTLs are formatted using their
current type and forwarded to the existing target, preserving the Intel
function-driver IOCTL surface.

## INF boundary

The INF is Class=Extension and uses an ExtensionId. It targets only
PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06 and only Windows build 19044;
the intentionally empty 19045 section blocks later builds. AddService does not
use SPSVCINST_ASSOCSERVICE. AddFilter registers the service as an upper filter.

## Current authorization

CI may compile the SYS and validate a temporary INF/CAT package, then must
delete all SYS/INF/CAT payloads. The development artifact may contain source,
documentation and the reader script only.

FILTER_INSTALL_AUTHORIZED=FALSE
DEVICE_RESTART_AUTHORIZED=FALSE
TARGET_TRUST_CHANGE_AUTHORIZED=FALSE
PCI_CONFIG_WRITE=FALSE
SETBUSDATA=FALSE
MMIO=FALSE
DSP_BOOT=FALSE
AUDIO_PLAYBACK=FALSE

Before physical installation, the existing H12 recovery/baseline controls and
a dedicated H15C-LIVE install/uninstall rollback gate must be completed.

## Primary references

- Microsoft Learn: Accessing PCI Device Configuration Space.
- Microsoft Learn: Install a Filter Driver.
- Microsoft Learn: INF AddFilter Directive.
- Microsoft Learn: Device Filter Driver Ordering.
- Microsoft Learn: Using an Extension INF File.
- Microsoft Windows-driver-samples: KMDF filter examples.
