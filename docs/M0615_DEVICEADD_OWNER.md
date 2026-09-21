# M0.6.15H6 KMDF DriverEntry / DeviceAdd owner

M0.6.15H6 introduces the real KMDF driver-entry and per-device ownership shell
while deliberately keeping the build as a static library and keeping firmware
source, INF installation and all audio output disconnected.

It is not an installable driver milestone.

## DriverEntry

DriverEntry now performs only framework-driver creation:

1. initialize WDF_DRIVER_CONFIG with Phaser360EvtDeviceAdd;
2. call WdfDriverCreate;
3. allocate no hardware state;
4. open no files;
5. perform no MMIO;
6. load no firmware.

No WPP, queues, interfaces, codec operations or audio endpoints are introduced
in this milestone.

## EvtDriverDeviceAdd ordering

Phaser360EvtDeviceAdd performs the reviewed KMDF order:

1. PnpResources::Configure(DeviceInit, attributes);
2. WdfDeviceCreate;
3. DeviceOwner::CreateInDeviceContext(device).

PnpResources::Configure registers the already-tested Prepare/Release,
SurpriseRemoval, D0Entry, post-interrupt-enable, pre-interrupt-disable and
D0Exit callbacks before WdfDeviceCreate.

Only after WdfDeviceCreate succeeds does H6 construct the composite owner and
create the dormant framework interrupt shell.

## Device context lifetime

PnpResources already owns the primary WDFDEVICE context used by its callbacks.

H6 allocates a second typed context on the same WDFDEVICE with
WdfObjectAllocateContext. KMDF permits more than one context type on a framework
object.

The H6 context contains aligned raw storage for DeviceOwner and a constructed
flag. DeviceOwner is placement-constructed in that storage.

The context has its own EvtCleanupCallback. Device child framework objects are
cleaned before the parent WDFDEVICE cleanup callback, so the C++ owner remains
alive while the interrupt/DPC/work-item child callbacks can still reference
IpcInterrupt::this.

No separate sibling WDFMEMORY object is used for the device-lifetime owner,
avoiding an undefined deletion order between owner storage and the interrupt
objects.

## DeviceOwner composition

DeviceOwner contains, in construction order:

- HardwareAccessGate;
- PnpResources bound to that gate;
- IpcInterrupt;
- PinnedFirmware;
- RepeatedDeviceLifecycle using the same IRQ, firmware owner and gate.

Initialize performs:

1. PnpResources::Attach(device);
2. PnpResources::InstallLifecycle(lifecycle.Ops());
3. RepeatedDeviceLifecycle::CreateInterruptShell(device).

No PrepareHardware, D0 entry or DSP access occurs from DeviceAdd.

## Firmware remains fail-closed

H6 intentionally does not locate, open or read a firmware file.

DeviceOwner exposes StageFirmware(kernelBytes, bytes) for a later reviewed
firmware-source milestone. DeviceAdd never calls it.

PinnedFirmware now exposes a read-only Loaded() state. Until a later source
successfully calls PinnedFirmware::Load, RepeatedDeviceLifecycle may be invoked
by KMDF but PinnedFirmware::Enter fails with STATUS_INVALID_DEVICE_STATE before
ColdPower boot is entered.

Therefore compiling DriverEntry/DeviceAdd is not permission to boot the DSP.

## Teardown

DeviceOwner's additional device context registers EvtCleanupCallback.

The owner destructor itself performs no hardware access and does not attempt to
delete child framework objects. Normal PnP/power teardown remains the
responsibility of the H5 lifecycle before device deletion.

KMDF then destroys child framework objects and invokes device-context cleanup;
the cleanup callback runs the DeviceOwner C++ destructor in the still-valid
device context.

## H6 static guards

A dedicated PowerShell guard requires:

- WDF_DRIVER_CONFIG_INIT and WdfDriverCreate;
- PnpResources::Configure before WdfDeviceCreate;
- DeviceOwner construction only after WdfDeviceCreate;
- WdfObjectAllocateContext with a context cleanup callback;
- Attach -> InstallLifecycle -> CreateInterruptShell ordering;
- StageFirmware absent from DeviceAdd;
- no Zw/Nt/Io/WDF target file-read path in H6 owner/entry code;
- no MMIO register calls in DriverEntry;
- no MAX98357A, DA7219, WaveRT, ACX, speaker or playback path;
- the WDK project remains ConfigurationType=StaticLibrary.

Real WDK compilation remains mandatory in addition to these structural guards.

## Still absent

H6 does not provide:

- INF hardware binding;
- SYS link/package/signing;
- controlled firmware acquisition;
- physical Lenovo installation;
- DSP boot on the Lenovo;
- SSP1/MAX98357A;
- SSP2/DA7219;
- microphone path;
- WaveRT/ACX endpoint or stream;
- any audio output.

The next milestone must solve the controlled firmware source and its deployment
contract before any physical DSP boot package is produced.
