// SPDX-License-Identifier: MIT
#include "h15g_function_mmio.h"

using namespace phaser360::windows;

const GUID phaser360::windows::kH15gInterfaceGuid={
    0x8c1b3150,0x6d10,0x4f88,{0x9d,0x36,0x15,0xf1,0x00,0x31,0x98,0x01}
};

namespace {
bool RangeOk(ULONG length,ULONG offset,ULONG bytes) noexcept {
    return offset<=length && bytes<=length-offset;
}
}

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject,PUNICODE_STRING registryPath) {
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config,H15gEvtDeviceAdd);
    return WdfDriverCreate(
        driverObject,registryPath,WDF_NO_OBJECT_ATTRIBUTES,&config,WDF_NO_HANDLE);
}

NTSTATUS phaser360::windows::H15gEvtDeviceAdd(
    WDFDRIVER driver,PWDFDEVICE_INIT deviceInit) {
    UNREFERENCED_PARAMETER(driver);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !deviceInit)
        return STATUS_INVALID_DEVICE_STATE;

    WdfDeviceInitSetDeviceType(deviceInit,kH15gDeviceType);

    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware=H15gEvtPrepareHardware;
    pnp.EvtDeviceReleaseHardware=H15gEvtReleaseHardware;
    pnp.EvtDeviceD0Entry=H15gEvtD0Entry;
    pnp.EvtDeviceD0Exit=H15gEvtD0Exit;
    pnp.EvtDeviceSurpriseRemoval=H15gEvtSurpriseRemoval;
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit,&pnp);

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,H15gDeviceContext);
    WDFDEVICE device=nullptr;
    auto status=WdfDeviceCreate(&deviceInit,&attributes,&device);
    if(!NT_SUCCESS(status)) return status;

    auto* context=H15gGetContext(device);
    if(!context) return STATUS_INVALID_DEVICE_STATE;
    RtlZeroMemory(context,sizeof(*context));

    WDF_OBJECT_ATTRIBUTES lockAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject=device;
    status=WdfSpinLockCreate(&lockAttributes,&context->lock);
    if(!NT_SUCCESS(status)) return status;

    WdfSpinLockAcquire(context->lock);
    context->snapshot.version=1u;
    context->snapshot.size=sizeof(H15gSnapshotV1);
    context->snapshot.flags=
        H15gDeviceAdded|
        H15gNoMmioWrite|
        H15gNoPciWrite|
        H15gNoDma|
        H15gNoIrqOwnership|
        H15gNoFirmware|
        H15gNoPlayback|
        H15gMappingsReadOnly;
    context->snapshot.lastStatus=STATUS_SUCCESS;
    context->snapshot.generation=1u;
    WdfSpinLockRelease(context->lock);

    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(
        &queueConfig,WdfIoQueueDispatchParallel);
    queueConfig.PowerManaged=WdfFalse;
    queueConfig.EvtIoDeviceControl=H15gEvtIoDeviceControl;
    status=WdfIoQueueCreate(
        device,&queueConfig,WDF_NO_OBJECT_ATTRIBUTES,WDF_NO_HANDLE);
    if(!NT_SUCCESS(status)) return status;

    return WdfDeviceCreateDeviceInterface(
        device,&kH15gInterfaceGuid,nullptr);
}

NTSTATUS phaser360::windows::H15gEvtPrepareHardware(
    WDFDEVICE device,WDFCMRESLIST raw,WDFCMRESLIST translated) {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device || !raw || !translated)
        return STATUS_INVALID_DEVICE_STATE;

    auto* context=H15gGetContext(device);
    if(!context || !context->lock || context->hda || context->dsp)
        return STATUS_INVALID_DEVICE_STATE;
    if(InterlockedCompareExchange(&context->removed,0,0)!=0)
        return STATUS_DELETE_PENDING;

    const ULONG rawCount=WdfCmResourceListGetCount(raw);
    const ULONG translatedCount=WdfCmResourceListGetCount(translated);
    if(rawCount==0 || rawCount!=translatedCount || rawCount>64)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    PHYSICAL_ADDRESS addresses[2]{};
    ULONG lengths[2]{};
    ULONG memoryCount=0;
    ULONG interruptCount=0;

    for(ULONG i=0;i<translatedCount;++i) {
        auto* rd=WdfCmResourceListGetDescriptor(raw,i);
        auto* td=WdfCmResourceListGetDescriptor(translated,i);
        if(!rd || !td || rd->Type!=td->Type)
            return STATUS_DEVICE_CONFIGURATION_ERROR;

        if(td->Type==CmResourceTypeMemory) {
            if(memoryCount>=2 || td->u.Memory.Start.QuadPart<=0)
                return STATUS_DEVICE_CONFIGURATION_ERROR;
            addresses[memoryCount]=td->u.Memory.Start;
            lengths[memoryCount]=td->u.Memory.Length;
            ++memoryCount;
        } else if(td->Type==CmResourceTypeInterrupt) {
            ++interruptCount;
        } else if(td->Type==CmResourceTypeMemoryLarge) {
            return STATUS_DEVICE_CONFIGURATION_ERROR;
        }
    }

    if(memoryCount!=2 || interruptCount!=1 ||
       lengths[0]!=kH15gHdaBytes || lengths[1]!=kH15gDspBytes)
        return STATUS_DEVICE_CONFIGURATION_ERROR;

    UCHAR* hda=static_cast<UCHAR*>(
        MmMapIoSpaceEx(addresses[0],lengths[0],PAGE_READONLY|PAGE_NOCACHE));
    if(!hda) return STATUS_INSUFFICIENT_RESOURCES;

    if(InterlockedCompareExchange(&context->removed,0,0)!=0) {
        MmUnmapIoSpace(hda,lengths[0]);
        return STATUS_DELETE_PENDING;
    }

    UCHAR* dsp=static_cast<UCHAR*>(
        MmMapIoSpaceEx(addresses[1],lengths[1],PAGE_READONLY|PAGE_NOCACHE));
    if(!dsp) {
        MmUnmapIoSpace(hda,lengths[0]);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if(InterlockedCompareExchange(&context->removed,0,0)!=0) {
        MmUnmapIoSpace(dsp,lengths[1]);
        MmUnmapIoSpace(hda,lengths[0]);
        return STATUS_DELETE_PENDING;
    }

    context->hda=hda;
    context->dsp=dsp;
    context->hdaLength=lengths[0];
    context->dspLength=lengths[1];

    WdfSpinLockAcquire(context->lock);
    ++context->snapshot.generation;
    ++context->snapshot.prepareCount;
    context->snapshot.rawResourceCount=rawCount;
    context->snapshot.translatedResourceCount=translatedCount;
    context->snapshot.memoryCount=memoryCount;
    context->snapshot.interruptCount=interruptCount;
    context->snapshot.hdaPhysical=
        static_cast<ULONGLONG>(addresses[0].QuadPart);
    context->snapshot.dspPhysical=
        static_cast<ULONGLONG>(addresses[1].QuadPart);
    context->snapshot.hdaLength=lengths[0];
    context->snapshot.dspLength=lengths[1];
    context->snapshot.flags|=
        H15gPrepared|H15gResourcesExact|H15gMappingsLive;
    context->snapshot.lastStatus=STATUS_SUCCESS;
    WdfSpinLockRelease(context->lock);
    return STATUS_SUCCESS;
}

NTSTATUS phaser360::windows::H15gEvtD0Entry(
    WDFDEVICE device,WDF_POWER_DEVICE_STATE previousState) {
    UNREFERENCED_PARAMETER(previousState);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device)
        return STATUS_INVALID_DEVICE_STATE;

    auto* context=H15gGetContext(device);
    if(!context || !context->lock || !context->hda || !context->dsp ||
       context->hdaLength!=kH15gHdaBytes ||
       context->dspLength!=kH15gDspBytes)
        return STATUS_INVALID_DEVICE_STATE;
    if(InterlockedCompareExchange(&context->removed,0,0)!=0)
        return STATUS_DELETE_PENDING;

    const bool ranges=
        RangeOk(context->hdaLength,kH15gHdaGcap,sizeof(USHORT)) &&
        RangeOk(context->hdaLength,kH15gHdaVmin,sizeof(UCHAR)) &&
        RangeOk(context->hdaLength,kH15gHdaVmaj,sizeof(UCHAR)) &&
        RangeOk(context->hdaLength,kH15gHdaGctl,sizeof(ULONG)) &&
        RangeOk(context->hdaLength,kH15gIntelEm2,sizeof(ULONG)) &&
        RangeOk(context->dspLength,kH15gDspAdspcs,sizeof(ULONG)) &&
        RangeOk(context->dspLength,kH15gDspAdspis,sizeof(ULONG)) &&
        RangeOk(context->dspLength,kH15gDspHipci,sizeof(ULONG)) &&
        RangeOk(context->dspLength,kH15gDspHipcie,sizeof(ULONG)) &&
        RangeOk(context->dspLength,kH15gDspRomStatus,sizeof(ULONG));
    if(!ranges) return STATUS_DEVICE_CONFIGURATION_ERROR;

    H15gSnapshotV1 next{};
    WdfSpinLockAcquire(context->lock);
    next=context->snapshot;
    WdfSpinLockRelease(context->lock);

    next.hdaGcap=READ_REGISTER_USHORT(
        reinterpret_cast<volatile USHORT*>(context->hda+kH15gHdaGcap));
    next.hdaVmin=READ_REGISTER_UCHAR(
        reinterpret_cast<volatile UCHAR*>(context->hda+kH15gHdaVmin));
    next.hdaVmaj=READ_REGISTER_UCHAR(
        reinterpret_cast<volatile UCHAR*>(context->hda+kH15gHdaVmaj));
    next.hdaGctl=READ_REGISTER_ULONG(
        reinterpret_cast<volatile ULONG*>(context->hda+kH15gHdaGctl));
    next.hdaIntelEm2=READ_REGISTER_ULONG(
        reinterpret_cast<volatile ULONG*>(context->hda+kH15gIntelEm2));

    next.dspAdspcs=READ_REGISTER_ULONG(
        reinterpret_cast<volatile ULONG*>(context->dsp+kH15gDspAdspcs));
    next.dspAdspis=READ_REGISTER_ULONG(
        reinterpret_cast<volatile ULONG*>(context->dsp+kH15gDspAdspis));
    next.dspHipci=READ_REGISTER_ULONG(
        reinterpret_cast<volatile ULONG*>(context->dsp+kH15gDspHipci));
    next.dspHipcie=READ_REGISTER_ULONG(
        reinterpret_cast<volatile ULONG*>(context->dsp+kH15gDspHipcie));
    next.dspRomStatus=READ_REGISTER_ULONG(
        reinterpret_cast<volatile ULONG*>(context->dsp+kH15gDspRomStatus));

    if(InterlockedCompareExchange(&context->removed,0,0)!=0)
        return STATUS_DELETE_PENDING;

    ++next.d0EntryCount;
    next.flags|=H15gD0Entered;
    const bool valid=
        next.hdaGcap!=0xffffu &&
        next.hdaGctl!=0xffffffffu &&
        next.hdaIntelEm2!=0xffffffffu &&
        next.dspAdspcs!=0xffffffffu &&
        next.dspAdspis!=0xffffffffu;
    if(valid) {
        next.flags|=H15gSnapshotValid;
        next.lastStatus=STATUS_SUCCESS;
    } else {
        next.lastStatus=STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    WdfSpinLockAcquire(context->lock);
    context->snapshot=next;
    WdfSpinLockRelease(context->lock);
    return valid?STATUS_SUCCESS:STATUS_DEVICE_CONFIGURATION_ERROR;
}

NTSTATUS phaser360::windows::H15gEvtD0Exit(
    WDFDEVICE device,WDF_POWER_DEVICE_STATE targetState) {
    UNREFERENCED_PARAMETER(targetState);
    if(!device) return STATUS_INVALID_DEVICE_STATE;
    auto* context=H15gGetContext(device);
    if(!context || !context->lock) return STATUS_INVALID_DEVICE_STATE;
    WdfSpinLockAcquire(context->lock);
    ++context->snapshot.d0ExitCount;
    context->snapshot.flags&=~H15gD0Entered;
    context->snapshot.lastStatus=STATUS_SUCCESS;
    WdfSpinLockRelease(context->lock);
    return STATUS_SUCCESS;
}

void phaser360::windows::H15gEvtSurpriseRemoval(WDFDEVICE device) {
    if(!device) return;
    auto* context=H15gGetContext(device);
    if(context) InterlockedExchange(&context->removed,1);
}

NTSTATUS phaser360::windows::H15gEvtReleaseHardware(
    WDFDEVICE device,WDFCMRESLIST translated) {
    UNREFERENCED_PARAMETER(translated);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device)
        return STATUS_INVALID_DEVICE_STATE;
    auto* context=H15gGetContext(device);
    if(!context || !context->lock) return STATUS_INVALID_DEVICE_STATE;

    auto* dsp=context->dsp;
    const ULONG dspLength=context->dspLength;
    auto* hda=context->hda;
    const ULONG hdaLength=context->hdaLength;

    context->dsp=nullptr;
    context->hda=nullptr;
    context->dspLength=0;
    context->hdaLength=0;

    if(dsp) MmUnmapIoSpace(dsp,dspLength);
    if(hda) MmUnmapIoSpace(hda,hdaLength);

    WdfSpinLockAcquire(context->lock);
    ++context->snapshot.releaseCount;
    context->snapshot.flags&=
        ~(H15gPrepared|H15gResourcesExact|H15gMappingsLive|H15gD0Entered);
    context->snapshot.lastStatus=STATUS_SUCCESS;
    WdfSpinLockRelease(context->lock);
    return STATUS_SUCCESS;
}

void phaser360::windows::H15gEvtIoDeviceControl(
    WDFQUEUE queue,WDFREQUEST request,SIZE_T outputBufferLength,
    SIZE_T inputBufferLength,ULONG ioControlCode) {
    UNREFERENCED_PARAMETER(outputBufferLength);
    if(!queue || !request) return;
    if(ioControlCode!=IOCTL_PHASER360_H15G_SNAPSHOT) {
        WdfRequestComplete(request,STATUS_INVALID_DEVICE_REQUEST);
        return;
    }
    if(inputBufferLength!=0) {
        WdfRequestComplete(request,STATUS_INVALID_PARAMETER);
        return;
    }

    H15gSnapshotV1* out=nullptr;
    SIZE_T available=0;
    auto status=WdfRequestRetrieveOutputBuffer(
        request,sizeof(H15gSnapshotV1),
        reinterpret_cast<void**>(&out),&available);
    if(!NT_SUCCESS(status)) {
        WdfRequestComplete(request,status);
        return;
    }

    auto* context=H15gGetContext(WdfIoQueueGetDevice(queue));
    if(!context || !context->lock) {
        WdfRequestComplete(request,STATUS_DEVICE_NOT_READY);
        return;
    }
    WdfSpinLockAcquire(context->lock);
    *out=context->snapshot;
    WdfSpinLockRelease(context->lock);
    WdfRequestCompleteWithInformation(
        request,STATUS_SUCCESS,sizeof(H15gSnapshotV1));
}
