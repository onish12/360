# M0.6.15H15C-LIVE read-only PCI capture filter

H15C-LIVE is a device-specific KMDF Extension upper filter for the exact Intel
SST target. It exists only to expose the H15C read-only PCI configuration
attestation while IntcAudioBus remains the function driver.

## Filter behavior

At EvtDevicePrepareHardware the filter constructs a fresh
PciConfigAttestation and performs one 256-byte BUS_INTERFACE_STANDARD
GetBusData capture. Capture failure is recorded but the filter returns success
so diagnostic failure does not intentionally block the Intel function driver.

The filter has no SetBusData path, BAR mapping, MMIO, DMA, IRQ object, DSP boot,
firmware execution, codec access or audio programming.

A private FILE_READ_ACCESS/METHOD_BUFFERED IOCTL returns the fixed snapshot.
All other DeviceControl requests are forwarded to the next-lower target.
Snapshot publication is protected by a WDF spin lock.

## INF boundary

The INF is Class=Extension with a fixed ExtensionId, targets only
PCI\VEN_8086&DEV_3198&SUBSYS_00000000&REV_06 and only Windows build 19044.
The intentionally empty 19045 section blocks later builds. AddService does not
associate the filter as the function service; AddFilter registers it in the
Upper position.

## R2 physical-use boundary

The normal development artifact remains source-only. A separate manual
workflow can produce the signed R2 test package. That package is never
auto-installed by CI.

Physical use is allowed only through the R2 checker, read-only preflight and
transaction. The transaction owns temporary trust for the package's exact
public certificate, captures read-only PCI evidence, removes the extension
filter, restores the Intel baseline and then removes the exact temporary trust.

FILTER_AUTO_INSTALL=FALSE
SYSTEM_REBOOT=FALSE
BCD_WRITE=FALSE
TRUST_CHANGE=TEMPORARY_EXACT_CERTIFICATE_DURING_R2_TRANSACTION
PCI_CONFIG_WRITE=FALSE
SETBUSDATA=FALSE
MMIO=FALSE
DSP_BOOT=FALSE
AUDIO_PLAYBACK=FALSE

## Primary references

- Microsoft Learn: Accessing PCI Device Configuration Space.
- Microsoft Learn: Install a Filter Driver.
- Microsoft Learn: INF AddFilter Directive.
- Microsoft Learn: Device Filter Driver Ordering.
- Microsoft Learn: Using an Extension INF File.
- Microsoft Learn: Test-Signing Driver Packages.
