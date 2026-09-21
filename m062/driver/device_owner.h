// SPDX-License-Identifier: MIT
#pragma once
#include "pnp_resources.h"
#include "repeated_device_lifecycle.h"
#include "pinned_firmware.h"
#include "firmware_source.h"

namespace phaser360 { namespace windows {

// H6 device-lifetime composition. Storage is supplied by an additional
// WDFDEVICE context, so it outlives child interrupt/DPC/work-item cleanup.
class DeviceOwner final {
public:
    DeviceOwner(WDFDEVICE device) noexcept
        : device_(device),pnp_(gate_),lifecycle_(irq_,firmware_,gate_) {}
    DeviceOwner(const DeviceOwner&)=delete;
    DeviceOwner& operator=(const DeviceOwner&)=delete;

    NTSTATUS Initialize() noexcept;
    // H7 accepts only the generated embedded provider. No arbitrary runtime
    // caller buffer or filesystem path is exposed by the device owner.
    NTSTATUS StageEmbeddedFirmware() noexcept;
    bool FirmwareReady() const noexcept { return firmware_.Loaded(); }

    static NTSTATUS CreateInDeviceContext(WDFDEVICE) noexcept;
    static DeviceOwner* FromDevice(WDFDEVICE) noexcept;

private:
    WDFDEVICE device_=nullptr;
    HardwareAccessGate gate_;
    PnpResources pnp_;
    IpcInterrupt irq_;
    PinnedFirmware firmware_;
    RepeatedDeviceLifecycle lifecycle_;
};

struct DeviceOwnerContext {
    alignas(DeviceOwner) UCHAR storage[sizeof(DeviceOwner)];
    BOOLEAN constructed;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(DeviceOwnerContext,GetDeviceOwnerContext)

} }
