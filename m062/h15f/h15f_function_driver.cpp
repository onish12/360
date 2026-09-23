// SPDX-License-Identifier: MIT
#include "h15f_function_driver.h"

using namespace phaser360::windows;

const GUID phaser360::windows::kH15fInterfaceGuid={
    0x8c1b3150,0x6d0f,0x4f88,{0x9d,0x36,0x15,0xf0,0x00,0x31,0x98,0x01}
};

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject,PUNICODE_STRING registryPath) {
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config,H15fEvtDeviceAdd);
    return WdfDriverCreate(
        driverObject,registryPath,WDF_NO_OBJECT_ATTRIBUTES,&config,WDF_NO_HANDLE);
}

NTSTATUS phaser360::windows::H15fEvtDeviceAdd(
    WDFDRIVER driver,PWDFDEVICE_INIT deviceInit) {
    UNREFERENCED_PARAMETER(driver);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !deviceInit)
        return STATUS_INVALID_DEVICE_STATE;

    WdfDeviceInitSetDeviceType(deviceInit,kH15fDeviceType);

    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware=H15fEvtPrepareHardware;
    pnp.EvtDeviceReleaseHardware=H15fEvtReleaseHardware;
    pnp.EvtDeviceD0Entry=H15fEvtD0Entry;
    pnp.EvtDeviceD0Exit=H15fEvtD0Exit;
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit,&pnp);

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,H15fDeviceContext);
    WDFDEVICE device=nullptr;
    auto status=WdfDeviceCreate(&deviceInit,&attributes,&device);
    if(!NT_SUCCESS(status)) return status;

    auto* context=H15fGetContext(device);
    if(!context) return STATUS_INVALID_DEVICE_STATE;

    WDF_OBJECT_ATTRIBUTES lockAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject=device;
    status=WdfSpinLockCreate(&lockAttributes,&context->lock);
    if(!NT_SUCCESS(status)) return status;

    WdfSpinLockAcquire(context->lock);
    context->snapshot.flags|=H15fDeviceAdded;
    context->snapshot.lastStatus=STATUS_SUCCESS;
    context->snapshot.generation=1;
    WdfSpinLockRelease(context->lock);

    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,WdfIoQueueDispatchParallel);
    queueConfig.PowerManaged=WdfFalse;
    queueConfig.EvtIoDeviceControl=H15fEvtIoDeviceControl;
    status=WdfIoQueueCreate(
        device,&queueConfig,WDF_NO_OBJECT_ATTRIBUTES,WDF_NO_HANDLE);
    if(!NT_SUCCESS(status)) return status;

    return WdfDeviceCreateDeviceInterface(
        device,&kH15fInterfaceGuid,nullptr);
}

NTSTATUS phaser360::windows::H15fEvtPrepareHardware(
    WDFDEVICE device,WDFCMRESLIST raw,WDFCMRESLIST translated) {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device || !raw || !translated)
        return STATUS_INVALID_DEVICE_STATE;

    auto* context=H15fGetContext(device);
    if(!context || !context->lock) return STATUS_INVALID_DEVICE_STATE;

    H15fSnapshotV1 next{};
    WdfSpinLockAcquire(context->lock);
    next=context->snapshot;
    WdfSpinLockRelease(context->lock);

    ++next.generation;
    ++next.prepareCount;
    next.rawResourceCount=WdfCmResourceListGetCount(raw);
    next.translatedResourceCount=WdfCmResourceListGetCount(translated);
    next.memoryCount=0;
    next.interruptCount=0;
    next.hdaPhysical=0;
    next.dspPhysical=0;
    next.hdaLength=0;
    next.dspLength=0;
    next.interruptFlags=0;
    next.flags&=~(H15fPrepared|H15fResourcesExact|H15fD0Entered);
    next.lastStatus=STATUS_DEVICE_CONFIGURATION_ERROR;

    for(ULONG i=0;i<next.translatedResourceCount;++i) {
        auto* d=WdfCmResourceListGetDescriptor(translated,i);
        if(!d) continue;
        if(d->Type==CmResourceTypeMemory) {
            if(next.memoryCount==0) {
                next.hdaPhysical=static_cast<ULONGLONG>(d->u.Memory.Start.QuadPart);
                next.hdaLength=d->u.Memory.Length;
            } else if(next.memoryCount==1) {
                next.dspPhysical=static_cast<ULONGLONG>(d->u.Memory.Start.QuadPart);
                next.dspLength=d->u.Memory.Length;
            }
            ++next.memoryCount;
        } else if(d->Type==CmResourceTypeInterrupt) {
            if(next.interruptCount==0)
                next.interruptFlags=d->Flags;
            ++next.interruptCount;
        }
    }

    next.flags|=H15fPrepared;
    const bool exact=
        next.memoryCount==2 &&
        next.interruptCount==1 &&
        next.hdaPhysical!=0 &&
        next.dspPhysical!=0 &&
        next.hdaPhysical!=next.dspPhysical &&
        next.hdaLength==kH15fHdaBytes &&
        next.dspLength==kH15fDspBytes;
    if(exact) {
        next.flags|=H15fResourcesExact;
        next.lastStatus=STATUS_SUCCESS;
    }

    WdfSpinLockAcquire(context->lock);
    context->snapshot=next;
    WdfSpinLockRelease(context->lock);

    return exact?STATUS_SUCCESS:STATUS_DEVICE_CONFIGURATION_ERROR;
}

NTSTATUS phaser360::windows::H15fEvtD0Entry(
    WDFDEVICE device,WDF_POWER_DEVICE_STATE previousState) {
    UNREFERENCED_PARAMETER(previousState);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device)
        return STATUS_INVALID_DEVICE_STATE;
    auto* context=H15fGetContext(device);
    if(!context || !context->lock) return STATUS_INVALID_DEVICE_STATE;

    WdfSpinLockAcquire(context->lock);
    ++context->snapshot.d0EntryCount;
    if((context->snapshot.flags&(H15fPrepared|H15fResourcesExact))==
       (H15fPrepared|H15fResourcesExact)) {
        context->snapshot.flags|=H15fD0Entered;
        context->snapshot.lastStatus=STATUS_SUCCESS;
    } else {
        context->snapshot.lastStatus=STATUS_INVALID_DEVICE_STATE;
    }
    const auto status=context->snapshot.lastStatus;
    WdfSpinLockRelease(context->lock);
    return status;
}

NTSTATUS phaser360::windows::H15fEvtD0Exit(
    WDFDEVICE device,WDF_POWER_DEVICE_STATE targetState) {
    UNREFERENCED_PARAMETER(targetState);
    if(!device) return STATUS_INVALID_DEVICE_STATE;
    auto* context=H15fGetContext(device);
    if(!context || !context->lock) return STATUS_INVALID_DEVICE_STATE;
    WdfSpinLockAcquire(context->lock);
    ++context->snapshot.d0ExitCount;
    context->snapshot.flags&=~H15fD0Entered;
    context->snapshot.lastStatus=STATUS_SUCCESS;
    WdfSpinLockRelease(context->lock);
    return STATUS_SUCCESS;
}

NTSTATUS phaser360::windows::H15fEvtReleaseHardware(
    WDFDEVICE device,WDFCMRESLIST translated) {
    UNREFERENCED_PARAMETER(translated);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device)
        return STATUS_INVALID_DEVICE_STATE;
    auto* context=H15fGetContext(device);
    if(!context || !context->lock) return STATUS_INVALID_DEVICE_STATE;
    WdfSpinLockAcquire(context->lock);
    ++context->snapshot.releaseCount;
    context->snapshot.flags&=~(H15fPrepared|H15fResourcesExact|H15fD0Entered);
    context->snapshot.lastStatus=STATUS_SUCCESS;
    WdfSpinLockRelease(context->lock);
    return STATUS_SUCCESS;
}

void phaser360::windows::H15fEvtIoDeviceControl(
    WDFQUEUE queue,WDFREQUEST request,SIZE_T outputBufferLength,
    SIZE_T inputBufferLength,ULONG ioControlCode) {
    UNREFERENCED_PARAMETER(outputBufferLength);
    if(!queue || !request) return;

    if(ioControlCode!=IOCTL_PHASER360_H15F_SNAPSHOT) {
        WdfRequestComplete(request,STATUS_INVALID_DEVICE_REQUEST);
        return;
    }
    if(inputBufferLength!=0) {
        WdfRequestComplete(request,STATUS_INVALID_PARAMETER);
        return;
    }

    H15fSnapshotV1* out=nullptr;
    SIZE_T available=0;
    auto status=WdfRequestRetrieveOutputBuffer(
        request,sizeof(H15fSnapshotV1),
        reinterpret_cast<void**>(&out),&available);
    if(!NT_SUCCESS(status)) {
        WdfRequestComplete(request,status);
        return;
    }

    auto* context=H15fGetContext(WdfIoQueueGetDevice(queue));
    if(!context || !context->lock) {
        WdfRequestComplete(request,STATUS_DEVICE_NOT_READY);
        return;
    }

    WdfSpinLockAcquire(context->lock);
    *out=context->snapshot;
    WdfSpinLockRelease(context->lock);
    WdfRequestCompleteWithInformation(
        request,STATUS_SUCCESS,sizeof(H15fSnapshotV1));
}
