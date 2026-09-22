// SPDX-License-Identifier: MIT
#include "h15c_live_filter.h"

using namespace phaser360::windows;

const GUID phaser360::windows::kH15cLiveInterfaceGuid={
    0x8c1b3150,0x6d0c,0x4c88,{0x9d,0x36,0x15,0xc0,0x00,0x31,0x98,0x01}
};

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject,PUNICODE_STRING registryPath) {
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config,H15cLiveEvtDeviceAdd);
    return WdfDriverCreate(
        driverObject,registryPath,WDF_NO_OBJECT_ATTRIBUTES,&config,WDF_NO_HANDLE);
}

NTSTATUS phaser360::windows::H15cLiveEvtDeviceAdd(
    WDFDRIVER driver,PWDFDEVICE_INIT deviceInit) {
    UNREFERENCED_PARAMETER(driver);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !deviceInit)
        return STATUS_INVALID_DEVICE_STATE;

    // This binary is a device-specific upper filter only. It never replaces
    // IntcAudioBus as the function driver.
    WdfFdoInitSetFilter(deviceInit);

    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware=H15cLiveEvtPrepareHardware;
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit,&pnp);

    WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_FILEOBJECT_CONFIG_INIT(
        &fileConfig,H15cLiveEvtDeviceFileCreate,
        WDF_NO_EVENT_CALLBACK,WDF_NO_EVENT_CALLBACK);
    WdfDeviceInitSetFileObjectConfig(
        deviceInit,&fileConfig,WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,H15cLiveDeviceContext);

    WDFDEVICE device=nullptr;
    auto status=WdfDeviceCreate(&deviceInit,&attributes,&device);
    if(!NT_SUCCESS(status)) return status;

    auto* context=H15cLiveGetContext(device);
    if(!context) return STATUS_INVALID_DEVICE_STATE;

    WDF_OBJECT_ATTRIBUTES lockAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&lockAttributes);
    lockAttributes.ParentObject=device;
    status=WdfSpinLockCreate(&lockAttributes,&context->snapshotLock);
    if(!NT_SUCCESS(status)) return status;

    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,WdfIoQueueDispatchParallel);
    queueConfig.PowerManaged=WdfFalse;
    queueConfig.EvtIoDeviceControl=H15cLiveEvtIoDeviceControl;

    WDFQUEUE queue=nullptr;
    status=WdfIoQueueCreate(
        device,&queueConfig,WDF_NO_OBJECT_ATTRIBUTES,&queue);
    if(!NT_SUCCESS(status)) return status;

    // Route only DeviceControl to the filter queue. Other request types retain
    // KMDF filter pass-through behavior.
    status=WdfDeviceConfigureRequestDispatching(
        device,queue,WdfRequestTypeDeviceControl);
    if(!NT_SUCCESS(status)) return status;

    UNICODE_STRING referenceString;
    RtlInitUnicodeString(&referenceString,L"h15c");
    return WdfDeviceCreateDeviceInterface(
        device,&kH15cLiveInterfaceGuid,&referenceString);
}

void phaser360::windows::H15cLiveEvtDeviceFileCreate(
    WDFDEVICE device,WDFREQUEST request,WDFFILEOBJECT fileObject) {
    if(!device || !request || !fileObject) {
        if(request) WdfRequestComplete(request,STATUS_INVALID_PARAMETER);
        return;
    }

    const auto* fileName=WdfFileObjectGetFileName(fileObject);
    UNICODE_STRING expectedWithSlash;
    UNICODE_STRING expectedBare;
    RtlInitUnicodeString(&expectedWithSlash,L"\\h15c");
    RtlInitUnicodeString(&expectedBare,L"h15c");

    if(fileName &&
       (RtlEqualUnicodeString(fileName,&expectedWithSlash,TRUE) ||
        RtlEqualUnicodeString(fileName,&expectedBare,TRUE))) {
        // This exact reference-string endpoint is owned by the diagnostic
        // filter. Completing CREATE locally prevents the Intel function
        // driver from rejecting an otherwise valid filter-only handle.
        WdfRequestComplete(request,STATUS_SUCCESS);
        return;
    }

    // Preserve all unrelated CREATE semantics of the Intel function stack.
    WDF_REQUEST_SEND_OPTIONS options;
    WDF_REQUEST_SEND_OPTIONS_INIT(
        &options,WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
    if(!WdfRequestSend(
        request,WdfDeviceGetIoTarget(device),&options)) {
        WdfRequestComplete(request,WdfRequestGetStatus(request));
    }
}

NTSTATUS phaser360::windows::H15cLiveEvtPrepareHardware(
    WDFDEVICE device,WDFCMRESLIST raw,WDFCMRESLIST translated) {
    UNREFERENCED_PARAMETER(raw);
    UNREFERENCED_PARAMETER(translated);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device)
        return STATUS_INVALID_DEVICE_STATE;

    auto* context=H15cLiveGetContext(device);
    if(!context) return STATUS_INVALID_DEVICE_STATE;

    WdfSpinLockAcquire(context->snapshotLock);
    InterlockedExchange(&context->ready,0);
    WdfSpinLockRelease(context->snapshotLock);

    H15cLiveSnapshotV1 snapshot{};
    snapshot.flags=H15cLiveCaptureAttempted|
                   H15cLiveGetBusDataOnly|
                   H15cLiveNoPciWrite;
    snapshot.generation=static_cast<ULONG>(
        InterlockedIncrement(&context->generation));

    PciConfigAttestation attestation;
    const auto capture=attestation.Capture(device);
    snapshot.captureStatus=capture;
    if(NT_SUCCESS(capture) && attestation.Valid()) {
        const auto& pci=attestation.Snapshot();
        snapshot.flags|=H15cLiveCaptureValid;
        snapshot.vendorId=pci.vendorId;
        snapshot.deviceId=pci.deviceId;
        snapshot.headerType=pci.headerType;
        snapshot.firstCapability=pci.firstCapability;
        snapshot.capabilityCount=pci.capabilityCount;
        snapshot.pgctl=pci.pgctl;
        snapshot.cgctl=pci.cgctl;
        RtlCopyMemory(
            snapshot.config,pci.config,kPciConfigSnapshotBytes);
    }

    WdfSpinLockAcquire(context->snapshotLock);
    context->snapshot=snapshot;
    InterlockedExchange(&context->ready,1);
    WdfSpinLockRelease(context->snapshotLock);

    // A read-only diagnostic filter must never prevent the Intel function
    // driver from starting merely because evidence capture failed.
    return STATUS_SUCCESS;
}

void phaser360::windows::H15cLiveEvtIoDeviceControl(
    WDFQUEUE queue,WDFREQUEST request,SIZE_T outputBufferLength,
    SIZE_T inputBufferLength,ULONG ioControlCode) {
    UNREFERENCED_PARAMETER(outputBufferLength);
    if(!queue || !request) return;

    const auto device=WdfIoQueueGetDevice(queue);
    if(ioControlCode!=IOCTL_PHASER360_H15C_LIVE_SNAPSHOT) {
        // Preserve the function driver's IOCTL surface exactly.
        WDF_REQUEST_SEND_OPTIONS options;
        WDF_REQUEST_SEND_OPTIONS_INIT(
            &options,WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
        WdfRequestFormatRequestUsingCurrentType(request);
        if(!WdfRequestSend(
            request,WdfDeviceGetIoTarget(device),&options)) {
            WdfRequestComplete(request,WdfRequestGetStatus(request));
        }
        return;
    }

    if(inputBufferLength!=0) {
        WdfRequestComplete(request,STATUS_INVALID_PARAMETER);
        return;
    }

    auto* context=H15cLiveGetContext(device);
    if(!context || !context->snapshotLock) {
        WdfRequestComplete(request,STATUS_DEVICE_NOT_READY);
        return;
    }

    H15cLiveSnapshotV1* out=nullptr;
    SIZE_T available=0;
    const auto status=WdfRequestRetrieveOutputBuffer(
        request,sizeof(H15cLiveSnapshotV1),
        reinterpret_cast<void**>(&out),&available);
    if(!NT_SUCCESS(status)) {
        WdfRequestComplete(request,status);
        return;
    }

    WdfSpinLockAcquire(context->snapshotLock);
    if(InterlockedCompareExchange(&context->ready,0,0)==0) {
        WdfSpinLockRelease(context->snapshotLock);
        WdfRequestComplete(request,STATUS_DEVICE_NOT_READY);
        return;
    }
    *out=context->snapshot;
    WdfSpinLockRelease(context->snapshotLock);
    WdfRequestCompleteWithInformation(
        request,STATUS_SUCCESS,sizeof(H15cLiveSnapshotV1));
}
