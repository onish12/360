// SPDX-License-Identifier: MIT
#include "h15d_live_filter.h"
#include <wdmguid.h>

// Kernel-safe placement construction. No CRT allocation or exceptions.
inline void* operator new(SIZE_T,void* place) noexcept { return place; }
inline void operator delete(void*,void*) noexcept {}

using namespace phaser360::windows;

const GUID phaser360::windows::kH15dLiveInterfaceGuid={
    0x8c1b3150,0x6d0c,0x4c88,{0x9d,0x36,0x15,0xd0,0x00,0x31,0x98,0x01}
};

namespace {
HardwareAccessGate* LiveGate(H15dLiveDeviceContext* context) noexcept {
    return context && context->gateConstructed
        ? reinterpret_cast<HardwareAccessGate*>(context->gateStorage) : nullptr;
}
void Store32(UCHAR* p,ULONG value) noexcept {
    p[0]=static_cast<UCHAR>(value);
    p[1]=static_cast<UCHAR>(value>>8);
    p[2]=static_cast<UCHAR>(value>>16);
    p[3]=static_cast<UCHAR>(value>>24);
}
}

extern "C" NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject,PUNICODE_STRING registryPath) {
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config,H15dLiveEvtDeviceAdd);
    return WdfDriverCreate(driverObject,registryPath,WDF_NO_OBJECT_ATTRIBUTES,&config,WDF_NO_HANDLE);
}

NTSTATUS phaser360::windows::H15dLiveEvtDeviceAdd(WDFDRIVER driver,PWDFDEVICE_INIT deviceInit) {
    UNREFERENCED_PARAMETER(driver);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !deviceInit) return STATUS_INVALID_DEVICE_STATE;
    WdfFdoInitSetFilter(deviceInit);
    WdfDeviceInitSetDeviceType(deviceInit,kH15dLiveDeviceType);

    WDF_PNPPOWER_EVENT_CALLBACKS pnp;
    WDF_PNPPOWER_EVENT_CALLBACKS_INIT(&pnp);
    pnp.EvtDevicePrepareHardware=H15dLiveEvtPrepareHardware;
    pnp.EvtDeviceReleaseHardware=H15dLiveEvtReleaseHardware;
    pnp.EvtDeviceSurpriseRemoval=H15dLiveEvtSurpriseRemoval;
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit,&pnp);

    WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_FILEOBJECT_CONFIG_INIT(&fileConfig,H15dLiveEvtDeviceFileCreate,
                               WDF_NO_EVENT_CALLBACK,WDF_NO_EVENT_CALLBACK);
    WdfDeviceInitSetFileObjectConfig(deviceInit,&fileConfig,WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,H15dLiveDeviceContext);
    attributes.EvtCleanupCallback=H15dLiveEvtContextCleanup;
    WDFDEVICE device=nullptr;
    auto status=WdfDeviceCreate(&deviceInit,&attributes,&device);
    if(!NT_SUCCESS(status)) return status;

    auto* context=H15dLiveGetContext(device);
    if(!context || context->gateConstructed) return STATUS_INVALID_DEVICE_STATE;
    (void)::new(context->gateStorage) HardwareAccessGate();
    context->gateConstructed=TRUE;

    WDF_IO_QUEUE_CONFIG queueConfig;
    WDF_IO_QUEUE_CONFIG_INIT(&queueConfig,WdfIoQueueDispatchSequential);
    queueConfig.PowerManaged=WdfFalse;
    queueConfig.EvtIoDeviceControl=H15dLiveEvtIoDeviceControl;
    WDF_OBJECT_ATTRIBUTES queueAttributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&queueAttributes);
    queueAttributes.ExecutionLevel=WdfExecutionLevelPassive;
    WDFQUEUE queue=nullptr;
    status=WdfIoQueueCreate(device,&queueConfig,&queueAttributes,&queue);
    if(!NT_SUCCESS(status)) return status;
    status=WdfDeviceConfigureRequestDispatching(device,queue,WdfRequestTypeDeviceControl);
    if(!NT_SUCCESS(status)) return status;

    UNICODE_STRING referenceString;
    RtlInitUnicodeString(&referenceString,L"h15d");
    return WdfDeviceCreateDeviceInterface(device,&kH15dLiveInterfaceGuid,&referenceString);
}

void phaser360::windows::H15dLiveEvtDeviceFileCreate(
    WDFDEVICE device,WDFREQUEST request,WDFFILEOBJECT fileObject) {
    if(!device || !request || !fileObject) {
        if(request) WdfRequestComplete(request,STATUS_INVALID_PARAMETER);
        return;
    }
    const auto* fileName=WdfFileObjectGetFileName(fileObject);
    UNICODE_STRING withSlash,bare;
    RtlInitUnicodeString(&withSlash,L"\\h15d");
    RtlInitUnicodeString(&bare,L"h15d");
    if(fileName && (RtlEqualUnicodeString(fileName,&withSlash,TRUE) ||
                    RtlEqualUnicodeString(fileName,&bare,TRUE))) {
        WdfRequestComplete(request,STATUS_SUCCESS);
        return;
    }
    WDF_REQUEST_SEND_OPTIONS options;
    WDF_REQUEST_SEND_OPTIONS_INIT(&options,WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
    if(!WdfRequestSend(request,WdfDeviceGetIoTarget(device),&options))
        WdfRequestComplete(request,WdfRequestGetStatus(request));
}

void phaser360::windows::H15dLiveEvtContextCleanup(WDFOBJECT object) {
    auto* context=H15dLiveGetContext(object);
    auto* gate=LiveGate(context);
    if(!gate) return;
    gate->~HardwareAccessGate();
    context->gateConstructed=FALSE;
}

NTSTATUS phaser360::windows::H15dLiveEvtPrepareHardware(
    WDFDEVICE device,WDFCMRESLIST raw,WDFCMRESLIST translated) {
    UNREFERENCED_PARAMETER(raw);
    UNREFERENCED_PARAMETER(translated);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device) return STATUS_INVALID_DEVICE_STATE;
    auto* context=H15dLiveGetContext(device);
    auto* gate=LiveGate(context);
    if(!context || !gate) return STATUS_INVALID_DEVICE_STATE;
    InterlockedExchange(&context->ready,0);
    if(gate->Removed() || !gate->OpenForPrepare())
        return STATUS_SUCCESS;
    InterlockedIncrement(&context->generation);
    InterlockedExchange(&context->ready,1);
    return STATUS_SUCCESS;
}

NTSTATUS phaser360::windows::H15dLiveEvtReleaseHardware(
    WDFDEVICE device,WDFCMRESLIST translated) {
    UNREFERENCED_PARAMETER(translated);
    if(!device) return STATUS_INVALID_PARAMETER;
    auto* context=H15dLiveGetContext(device);
    auto* gate=LiveGate(context);
    if(context) InterlockedExchange(&context->ready,0);
    if(gate) (void)gate->CloseForRelease();
    return STATUS_SUCCESS;
}

void phaser360::windows::H15dLiveEvtSurpriseRemoval(WDFDEVICE device) {
    if(!device) return;
    auto* context=H15dLiveGetContext(device);
    auto* gate=LiveGate(context);
    if(context) InterlockedExchange(&context->ready,0);
    if(gate) gate->SurpriseRemove();
}

void phaser360::windows::H15dLiveEvtIoDeviceControl(
    WDFQUEUE queue,WDFREQUEST request,SIZE_T outputBufferLength,
    SIZE_T inputBufferLength,ULONG ioControlCode) {
    UNREFERENCED_PARAMETER(outputBufferLength);
    if(!queue || !request) return;
    const auto device=WdfIoQueueGetDevice(queue);

    if(ioControlCode!=IOCTL_PHASER360_H15D_LIVE_TRANSACTION) {
        WDF_REQUEST_SEND_OPTIONS options;
        WDF_REQUEST_SEND_OPTIONS_INIT(&options,WDF_REQUEST_SEND_OPTION_SEND_AND_FORGET);
        WdfRequestFormatRequestUsingCurrentType(request);
        if(!WdfRequestSend(request,WdfDeviceGetIoTarget(device),&options))
            WdfRequestComplete(request,WdfRequestGetStatus(request));
        return;
    }

    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || inputBufferLength!=sizeof(H15dLiveRequestV1)) {
        WdfRequestComplete(request,STATUS_INVALID_PARAMETER);
        return;
    }
    auto* context=H15dLiveGetContext(device);
    auto* gate=LiveGate(context);
    if(!context || !gate || InterlockedCompareExchange(&context->ready,0,0)==0 ||
       !gate->Allowed() || gate->Removed()) {
        WdfRequestComplete(request,STATUS_DEVICE_NOT_READY);
        return;
    }

    H15dLiveRequestV1* in=nullptr;
    H15dLiveResultV1* out=nullptr;
    SIZE_T available=0;
    auto status=WdfRequestRetrieveInputBuffer(
        request,sizeof(H15dLiveRequestV1),reinterpret_cast<void**>(&in),&available);
    if(!NT_SUCCESS(status)) { WdfRequestComplete(request,status); return; }
    status=WdfRequestRetrieveOutputBuffer(
        request,sizeof(H15dLiveResultV1),reinterpret_cast<void**>(&out),&available);
    if(!NT_SUCCESS(status)) { WdfRequestComplete(request,status); return; }

    H15dLiveResultV1 result{};
    result.generation=static_cast<ULONG>(InterlockedCompareExchange(&context->generation,0,0));
    if(in->version!=1u || in->size!=sizeof(H15dLiveRequestV1) ||
       in->expectedPgctl!=kH15dExpectedPgctl || in->expectedCgctl!=kH15dExpectedCgctl) {
        result.transactionStatus=STATUS_INVALID_PARAMETER;
        *out=result;
        WdfRequestCompleteWithInformation(request,STATUS_SUCCESS,sizeof(result));
        return;
    }
    if(InterlockedCompareExchange(&context->consumed,1,0)!=0) {
        WdfRequestComplete(request,STATUS_INVALID_DEVICE_STATE);
        return;
    }
    if(!gate->Allowed() || gate->Removed()) {
        result.transactionStatus=STATUS_DELETE_PENDING;
        *out=result;
        WdfRequestCompleteWithInformation(request,STATUS_SUCCESS,sizeof(result));
        return;
    }

    PciConfigAttestation before;
    status=before.Capture(device);
    result.transactionStatus=status;
    if(NT_SUCCESS(status) && before.Valid()) {
        const auto& pci=before.Snapshot();
        result.flags|=H15dAttestationValid;
        result.vendorId=pci.vendorId; result.deviceId=pci.deviceId;
        result.headerType=pci.headerType; result.firstCapability=pci.firstCapability;
        result.capabilityCount=pci.capabilityCount;
        result.pgctlBefore=pci.pgctl; result.cgctlBefore=pci.cgctl;

        if(pci.pgctl==kH15dExpectedPgctl && pci.cgctl==kH15dExpectedCgctl) {
            result.flags|=H15dExpectedBaselineMatch;
            PciConfigBootPolicy policy;
            const auto apply=policy.Apply(device,pci,*gate);
            if(NT_SUCCESS(apply)) {
                result.flags|=H15dApplySucceeded;
                if(gate->Allowed() && !gate->Removed()) {
                    PciConfigAttestation applied;
                    const auto appliedStatus=applied.Capture(device);
                    if(NT_SUCCESS(appliedStatus) && applied.Valid()) {
                        result.pgctlApplied=applied.Snapshot().pgctl;
                        result.cgctlApplied=applied.Snapshot().cgctl;
                        UCHAR expected[kPciConfigSnapshotBytes]={};
                        RtlCopyMemory(expected,before.Snapshot().config,kPciConfigSnapshotBytes);
                        Store32(expected+PciConfigBootPolicy::CgctlOffset(),kH15dAppliedCgctl);
                        Store32(expected+PciConfigBootPolicy::PgctlOffset(),kH15dAppliedPgctl);
                        if(result.pgctlApplied==kH15dAppliedPgctl &&
                           result.cgctlApplied==kH15dAppliedCgctl &&
                           RtlCompareMemory(expected,applied.Snapshot().config,
                                            kPciConfigSnapshotBytes)==kPciConfigSnapshotBytes)
                            result.flags|=H15dAppliedReadbackExact;
                    } else if(NT_SUCCESS(result.transactionStatus)) {
                        result.transactionStatus=appliedStatus;
                    }
                } else if(NT_SUCCESS(result.transactionStatus)) {
                    result.transactionStatus=STATUS_DELETE_PENDING;
                }
            } else result.transactionStatus=apply;

            if(policy.Restore()) result.flags|=H15dRestoreSucceeded;

            if(gate->Allowed() && !gate->Removed()) {
                PciConfigAttestation after;
                const auto finalStatus=after.Capture(device);
                if(NT_SUCCESS(finalStatus) && after.Valid()) {
                    result.pgctlRestored=after.Snapshot().pgctl;
                    result.cgctlRestored=after.Snapshot().cgctl;
                    if(result.pgctlRestored==kH15dExpectedPgctl &&
                       result.cgctlRestored==kH15dExpectedCgctl)
                        result.flags|=H15dFinalBaselineExact;
                    if(RtlCompareMemory(
                           before.Snapshot().config,after.Snapshot().config,
                           kPciConfigSnapshotBytes)==kPciConfigSnapshotBytes)
                        result.flags|=H15dFullConfigRestoredExact;
                } else if(NT_SUCCESS(result.transactionStatus)) {
                    result.transactionStatus=finalStatus;
                }
            } else if(NT_SUCCESS(result.transactionStatus)) {
                result.transactionStatus=STATUS_DELETE_PENDING;
            }
        } else result.transactionStatus=STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if((result.flags&kH15dRequiredSuccessFlags)==kH15dRequiredSuccessFlags)
        result.transactionStatus=STATUS_SUCCESS;
    else if(NT_SUCCESS(result.transactionStatus))
        result.transactionStatus=STATUS_DEVICE_CONFIGURATION_ERROR;

    *out=result;
    WdfRequestCompleteWithInformation(request,STATUS_SUCCESS,sizeof(result));
}
