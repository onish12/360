// SPDX-License-Identifier: MIT
#include "h15e_live_filter.h"

using namespace phaser360::windows;

const GUID phaser360::windows::kH15eLiveInterfaceGuid={
    0x8c1b3150,0x6d0e,0x4c88,{0x9d,0x36,0x15,0xe0,0x00,0x31,0x98,0x01}
};

namespace {
bool InRange(ULONG length,ULONG offset,ULONG bytes) noexcept {
    return offset<=length && bytes<=length-offset;
}
}

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject,PUNICODE_STRING registryPath) {
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config,H15eLiveEvtDeviceAdd);
    return WdfDriverCreate(
        driverObject,registryPath,WDF_NO_OBJECT_ATTRIBUTES,&config,WDF_NO_HANDLE);
}

NTSTATUS phaser360::windows::H15eLiveEvtDeviceAdd(
    WDFDRIVER driver,PWDFDEVICE_INIT deviceInit) {
    UNREFERENCED_PARAMETER(driver);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !deviceInit)
        return STATUS_INVALID_DEVICE_STATE;

    WdfFdoInitSetFilter(deviceInit);
    WdfDeviceInitSetDeviceType(deviceInit,kH15eLiveDeviceType);

    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware=H15eLiveEvtPrepareHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit,&pnp);

    WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,H15eLiveEvtDeviceFileCreate,
        WDF_NO_EVENT_CALLBACK,WDF_NO_EVENT_CALLBACK);
    WdfDeviceInitSetFileObjectConfig(
        deviceInit,&fileConfig,WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,H15eLiveDeviceContext);
    WDFDEVICE device=nullptr;
    auto status=WdfDeviceCreate(&deviceInit,&attributes,&device);
    if(!NT_SUCCESS(status)) return status;

    auto* context=H15eLiveGetContext(device);
    if(!context) return STATUS_INVALID_DEVICE_STATE;

    WDF_OBJECT_ATTRIBUTES lockAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject=device;
    status=WdfSpinLockCreate(&lockAttributes,&context->snapshotLock);
    if(!NT_SUCCESS(status)) return status;

    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,WdfIoQueueDispatchParallel);
    queueConfig.PowerManaged=WdfFalse;
    queueConfig.EvtIoDeviceControl=H15eLiveEvtIoDeviceControl;
    WDFQUEUE queue=nullptr;
    status=WdfIoQueueCreate(
        device,&queueConfig,WDF_NO_OBJECT_ATTRIBUTES,&queue);
    if(!NT_SUCCESS(status)) return status;
    status=WdfDeviceConfigureRequestDispatching(
        device,queue,WdfRequestTypeDeviceControl);
    if(!NT_SUCCESS(status)) return status;

    UNICODE_STRING referenceString;
    RtlInitUnicodeString(&referenceString,L"h15e");
    return WdfDeviceCreateDeviceInterface(
        device,&kH15eLiveInterfaceGuid,&referenceString);
}

void phaser360::windows::H15eLiveEvtDeviceFileCreate(
    WDFDEVICE device,WDFREQUEST request,WDFFILEOBJECT fileObject) {
    if(!device || !request || !fileObject) {
        if(request) WdfRequestComplete(request,STATUS_INVALID_PARAMETER);
        return;
    }
    const auto* fileName=WdfFileObjectGetFileName(fileObject);
    UNICODE_STRING withSlash,bare;
    RtlInitUnicodeString(&withSlash,L"\\h15e");
    RtlInitUnicodeString(&bare,L"h15e");
    if(fileName &&
       (RtlEqualUnicodeString(fileName,&withSlash,TRUE) ||
        RtlEqualUnicodeString(fileName,&bare,TRUE))) {
        WdfRequestComplete(request,STATUS_SUCCESS);
        return;
    }
    WDF_REQUEST_SEND_OPTIONS options;
    WDF_REQUEST_SEND_OPTIONS_INIT(
        &options,WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
    if(!WdfRequestSend(request,WdfDeviceGetIoTarget(device),&options))
        WdfRequestComplete(request,WdfRequestGetStatus(request));
}

NTSTATUS phaser360::windows::H15eLiveEvtPrepareHardware(
    WDFDEVICE device,WDFCMRESLIST raw,WDFCMRESLIST translated) {
    UNREFERENCED_PARAMETER(raw);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device || !translated)
        return STATUS_SUCCESS;

    auto* context=H15eLiveGetContext(device);
    if(!context || !context->snapshotLock) return STATUS_SUCCESS;

    InterlockedExchange(&context->ready,0);

    H15eLiveSnapshotV1 snapshot{};
    snapshot.generation=static_cast<ULONG>(
        InterlockedIncrement(&context->generation));

    PciConfigAttestation attestation;
    const auto pciStatus=attestation.Capture(device);
    snapshot.captureStatus=pciStatus;
    if(!NT_SUCCESS(pciStatus) || !attestation.Valid()) {
        WdfSpinLockAcquire(context->snapshotLock);
        context->snapshot=snapshot;
        InterlockedExchange(&context->ready,1);
        WdfSpinLockRelease(context->snapshotLock);
        return STATUS_SUCCESS;
    }

    const auto& pci=attestation.Snapshot();
    snapshot.flags|=H15eAttestationValid;
    snapshot.vendorId=pci.vendorId;
    snapshot.deviceId=pci.deviceId;
    snapshot.headerType=pci.headerType;
    snapshot.firstCapability=pci.firstCapability;
    snapshot.capabilityCount=pci.capabilityCount;
    snapshot.pgctl=pci.pgctl;
    snapshot.cgctl=pci.cgctl;

    PHYSICAL_ADDRESS addresses[2]{};
    ULONG lengths[2]{};
    ULONG memoryCount=0;
    const ULONG count=WdfCmResourceListGetCount(translated);
    for(ULONG i=0;i<count;++i) {
        auto* descriptor=WdfCmResourceListGetDescriptor(translated,i);
        if(!descriptor || descriptor->Type!=CmResourceTypeMemory) continue;
        if(memoryCount>=2 || descriptor->u.Memory.Start.QuadPart<=0 ||
           (descriptor->Flags&CM_RESOURCE_MEMORY_READ_ONLY)!=0 ||
           (descriptor->Flags&CM_RESOURCE_MEMORY_WRITE_ONLY)!=0) {
            snapshot.captureStatus=STATUS_DEVICE_CONFIGURATION_ERROR;
            memoryCount=3;
            break;
        }
        addresses[memoryCount]=descriptor->u.Memory.Start;
        lengths[memoryCount]=descriptor->u.Memory.Length;
        ++memoryCount;
    }

    if(memoryCount==2 && lengths[0]==kH15eHdaBytes && lengths[1]==kH15eDspBytes) {
        snapshot.flags|=H15eResourcesExact;
        snapshot.hdaPhysical=static_cast<ULONGLONG>(addresses[0].QuadPart);
        snapshot.dspPhysical=static_cast<ULONGLONG>(addresses[1].QuadPart);
        snapshot.hdaLength=lengths[0];
        snapshot.dspLength=lengths[1];

        UCHAR* hda=static_cast<UCHAR*>(
            MmMapIoSpaceEx(addresses[0],lengths[0],PAGE_READONLY|PAGE_NOCACHE));
        if(hda) {
            snapshot.flags|=H15eHdaMapped;
            UCHAR* dsp=static_cast<UCHAR*>(
                MmMapIoSpaceEx(addresses[1],lengths[1],PAGE_READONLY|PAGE_NOCACHE));
            if(dsp) {
                snapshot.flags|=H15eDspMapped;

                const bool hdaRange=
                    InRange(lengths[0],kH15eHdaGcap,sizeof(USHORT)) &&
                    InRange(lengths[0],kH15eHdaVmin,sizeof(UCHAR)) &&
                    InRange(lengths[0],kH15eHdaVmaj,sizeof(UCHAR)) &&
                    InRange(lengths[0],kH15eHdaGctl,sizeof(ULONG)) &&
                    InRange(lengths[0],kH15eIntelEm2,sizeof(ULONG));
                const bool dspRange=
                    InRange(lengths[1],kH15eDspAdspcs,sizeof(ULONG)) &&
                    InRange(lengths[1],kH15eDspAdspis,sizeof(ULONG)) &&
                    InRange(lengths[1],kH15eDspHipci,sizeof(ULONG)) &&
                    InRange(lengths[1],kH15eDspHipcie,sizeof(ULONG)) &&
                    InRange(lengths[1],kH15eDspRomStatus,sizeof(ULONG));

                if(hdaRange && dspRange) {
                    snapshot.hdaGcap=READ_REGISTER_USHORT(
                        reinterpret_cast<volatile USHORT*>(hda+kH15eHdaGcap));
                    snapshot.hdaVmin=READ_REGISTER_UCHAR(
                        reinterpret_cast<volatile UCHAR*>(hda+kH15eHdaVmin));
                    snapshot.hdaVmaj=READ_REGISTER_UCHAR(
                        reinterpret_cast<volatile UCHAR*>(hda+kH15eHdaVmaj));
                    snapshot.hdaGctl=READ_REGISTER_ULONG(
                        reinterpret_cast<volatile ULONG*>(hda+kH15eHdaGctl));
                    snapshot.hdaIntelEm2=READ_REGISTER_ULONG(
                        reinterpret_cast<volatile ULONG*>(hda+kH15eIntelEm2));

                    snapshot.dspAdspcs=READ_REGISTER_ULONG(
                        reinterpret_cast<volatile ULONG*>(dsp+kH15eDspAdspcs));
                    snapshot.dspAdspis=READ_REGISTER_ULONG(
                        reinterpret_cast<volatile ULONG*>(dsp+kH15eDspAdspis));
                    snapshot.dspHipci=READ_REGISTER_ULONG(
                        reinterpret_cast<volatile ULONG*>(dsp+kH15eDspHipci));
                    snapshot.dspHipcie=READ_REGISTER_ULONG(
                        reinterpret_cast<volatile ULONG*>(dsp+kH15eDspHipcie));
                    snapshot.dspRomStatus=READ_REGISTER_ULONG(
                        reinterpret_cast<volatile ULONG*>(dsp+kH15eDspRomStatus));

                    if(snapshot.hdaGcap!=0xffffu &&
                       snapshot.hdaGctl!=0xffffffffu &&
                       snapshot.hdaIntelEm2!=0xffffffffu)
                        snapshot.flags|=H15eHdaRegistersValid;

                    if(snapshot.dspAdspcs!=0xffffffffu &&
                       snapshot.dspAdspis!=0xffffffffu)
                        snapshot.flags|=H15eDspRegistersValid;
                }

                MmUnmapIoSpace(dsp,lengths[1]);
            }
            MmUnmapIoSpace(hda,lengths[0]);
            snapshot.flags|=H15eMappingsReleased;
        }
    } else if(NT_SUCCESS(snapshot.captureStatus)) {
        snapshot.captureStatus=STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if((snapshot.flags&kH15eRequiredSuccessFlags)==kH15eRequiredSuccessFlags)
        snapshot.captureStatus=STATUS_SUCCESS;
    else if(NT_SUCCESS(snapshot.captureStatus))
        snapshot.captureStatus=STATUS_DEVICE_CONFIGURATION_ERROR;

    WdfSpinLockAcquire(context->snapshotLock);
    context->snapshot=snapshot;
    InterlockedExchange(&context->ready,1);
    WdfSpinLockRelease(context->snapshotLock);

    // Diagnostic filter must not block the Intel function driver.
    return STATUS_SUCCESS;
}

void phaser360::windows::H15eLiveEvtIoDeviceControl(
    WDFQUEUE queue,WDFREQUEST request,SIZE_T outputBufferLength,
    SIZE_T inputBufferLength,ULONG ioControlCode) {
    UNREFERENCED_PARAMETER(outputBufferLength);
    if(!queue || !request) return;
    const auto device=WdfIoQueueGetDevice(queue);

    if(ioControlCode!=IOCTL_PHASER360_H15E_LIVE_SNAPSHOT) {
        WDF_REQUEST_SEND_OPTIONS options;
        WDF_REQUEST_SEND_OPTIONS_INIT(
            &options,WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
        WdfRequestFormatRequestUsingCurrentType(request);
        if(!WdfRequestSend(request,WdfDeviceGetIoTarget(device),&options))
            WdfRequestComplete(request,WdfRequestGetStatus(request));
        return;
    }

    if(inputBufferLength!=0) {
        WdfRequestComplete(request,STATUS_INVALID_PARAMETER);
        return;
    }
    auto* context=H15eLiveGetContext(device);
    if(!context || !context->snapshotLock ||
       InterlockedCompareExchange(&context->ready,0,0)==0) {
        WdfRequestComplete(request,STATUS_DEVICE_NOT_READY);
        return;
    }

    H15eLiveSnapshotV1* out=nullptr;
    SIZE_T available=0;
    const auto status=WdfRequestRetrieveOutputBuffer(
        request,sizeof(H15eLiveSnapshotV1),
        reinterpret_cast<void**>(&out),&available);
    if(!NT_SUCCESS(status)) {
        WdfRequestComplete(request,status);
        return;
    }

    WdfSpinLockAcquire(context->snapshotLock);
    *out=context->snapshot;
    WdfSpinLockRelease(context->snapshotLock);
    WdfRequestCompleteWithInformation(
        request,STATUS_SUCCESS,sizeof(H15eLiveSnapshotV1));
}
