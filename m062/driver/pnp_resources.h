// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
namespace phaser360 { namespace windows {
// Resource-only PnP adapter. Not a boot driver: no register access, DMA, IRQ
// creation, D0 callbacks or externally accessible mapping pointers. Construct
// the owner normally; keep it alive until after WDF device destruction. Calls
// must be serialized at PASSIVE_LEVEL. ReleaseHardware performs explicit cleanup.
class PnpResources final {
public:
    PnpResources() noexcept = default;
    PnpResources(const PnpResources&)=delete;
    PnpResources& operator=(const PnpResources&)=delete;
    // Before WdfDeviceCreate. Resets attributes and reserves primary context.
    // Do not replace the callbacks or context type after this call.
    static NTSTATUS Configure(PWDFDEVICE_INIT,WDF_OBJECT_ATTRIBUTES*) noexcept;
    // After WdfDeviceCreate using those attributes, before DeviceAdd returns.
    NTSTATUS Attach(WDFDEVICE) noexcept;
    bool Prepared() const noexcept { return hda_!=nullptr && dsp_!=nullptr; }
private:
    WDFDEVICE device_=nullptr;
    void* hda_=nullptr;
    void* dsp_=nullptr;
    NTSTATUS Prepare(WDFCMRESLIST) noexcept;
    void Release() noexcept;
    static NTSTATUS PrepareHardware(WDFDEVICE,WDFCMRESLIST,WDFCMRESLIST);
    static NTSTATUS ReleaseHardware(WDFDEVICE,WDFCMRESLIST);
};
}}
