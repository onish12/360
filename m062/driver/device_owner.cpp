// SPDX-License-Identifier: MIT
#include "device_owner.h"

// Kernel-safe placement construction. No CRT allocation or exceptions.
inline void* operator new(SIZE_T,void* place) noexcept { return place; }
inline void operator delete(void*,void*) noexcept {}

namespace phaser360 { namespace windows {

static void DeviceOwnerContextCleanup(WDFOBJECT object) {
    auto* context=GetDeviceOwnerContext(object);
    if(!context || !context->constructed) return;
    auto* owner=reinterpret_cast<DeviceOwner*>(context->storage);
    owner->~DeviceOwner();
    context->constructed=FALSE;
}

DeviceOwner* DeviceOwner::FromDevice(WDFDEVICE device) noexcept {
    if(!device) return nullptr;
    auto* context=GetDeviceOwnerContext(device);
    if(!context || !context->constructed) return nullptr;
    return reinterpret_cast<DeviceOwner*>(context->storage);
}

NTSTATUS DeviceOwner::CreateInDeviceContext(WDFDEVICE device) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device)
        return STATUS_INVALID_DEVICE_STATE;

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,DeviceOwnerContext);
    attributes.EvtCleanupCallback=DeviceOwnerContextCleanup;

    DeviceOwnerContext* context=nullptr;
    const auto status=WdfObjectAllocateContext(
        device,&attributes,reinterpret_cast<void**>(&context));
    if(!NT_SUCCESS(status)) return status;
    if(!context || context->constructed) return STATUS_INVALID_DEVICE_STATE;

    auto* owner=::new(context->storage) DeviceOwner(device);
    context->constructed=TRUE;

    const auto initStatus=owner->Initialize();
    // On failure EvtDriverDeviceAdd returns the status. KMDF owns deletion of
    // the WDFDEVICE; its context cleanup destroys this owner after child
    // framework objects have been cleaned up.
    return initStatus;
}

NTSTATUS DeviceOwner::Initialize() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device_)
        return STATUS_INVALID_DEVICE_STATE;

    auto status=pnp_.Attach(device_);
    if(!NT_SUCCESS(status)) return status;

    if(!pnp_.InstallLifecycle(lifecycle_.Ops()))
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    // H8 connects only the H7 build-time embedded provider. This performs no
    // runtime file I/O and PinnedFirmware hashes its owned copy before the
    // framework can ever reach PrepareHardware/D0Entry.
    status=StageEmbeddedFirmware();
    if(!NT_SUCCESS(status)) return status;

    status=lifecycle_.CreateInterruptShell(device_);
    if(!NT_SUCCESS(status)) return status;

    return STATUS_SUCCESS;
}

NTSTATUS DeviceOwner::StageEmbeddedFirmware() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device_ || firmware_.Loaded())
        return STATUS_INVALID_DEVICE_STATE;
    return StagePinnedFirmwareFromSource(
        firmware_,device_,GetEmbeddedFirmwareSource);
}

} }
