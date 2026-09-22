// SPDX-License-Identifier: MIT
#include "h15d_live_filter.h"
#include <wdmguid.h>

using namespace phaser360::windows;

const GUID phaser360::windows::kH15dLiveInterfaceGuid={
    0x8c1b3150,0x6d0c,0x4c88,{0x9d,0x36,0x15,0xd0,0x00,0x31,0x98,0x01}
};

namespace {
bool ReadPair(WDFDEVICE device,ULONG* pgctl,ULONG* cgctl) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device || !pgctl || !cgctl) return false;
    BUS_INTERFACE_STANDARD bus{};
    const auto status=WdfFdoQueryForInterface(
        device,&GUID_BUS_INTERFACE_STANDARD,reinterpret_cast<PINTERFACE>(&bus),
        static_cast<USHORT>(sizeof(bus)),1,nullptr);
    if(!NT_SUCCESS(status)) return false;
    bool ok=bus.GetBusData && bus.InterfaceDereference;
    if(ok) {
        *pgctl=0; *cgctl=0;
        ok=bus.GetBusData(bus.Context,PCI_WHICHSPACE_CONFIG,pgctl,0x44,sizeof(*pgctl))==sizeof(*pgctl) &&
           bus.GetBusData(bus.Context,PCI_WHICHSPACE_CONFIG,cgctl,0x48,sizeof(*cgctl))==sizeof(*cgctl);
    }
    if(bus.InterfaceDereference) bus.InterfaceDereference(bus.Context);
    return ok;
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
    WdfDeviceInitSetPnpPowerEventCallbacks(deviceInit,&pnp);

    WDF_FILEOBJECT_CONFIG fileConfig;
    WDF_FILEOBJECT_CONFIG_INIT(&fileConfig,H15dLiveEvtDeviceFileCreate,
                               WDF_NO_EVENT_CALLBACK,WDF_NO_EVENT_CALLBACK);
    WdfDeviceInitSetFileObjectConfig(deviceInit,&fileConfig,WDF_NO_OBJECT_ATTRIBUTES);

    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT_CONTEXT_TYPE(&attributes,H15dLiveDeviceContext);
    WDFDEVICE device=nullptr;
    auto status=WdfDeviceCreate(&deviceInit,&attributes,&device);
    if(!NT_SUCCESS(status)) return status;

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

NTSTATUS phaser360::windows::H15dLiveEvtPrepareHardware(
    WDFDEVICE device,WDFCMRESLIST raw,WDFCMRESLIST translated) {
    UNREFERENCED_PARAMETER(raw);
    UNREFERENCED_PARAMETER(translated);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device) return STATUS_INVALID_DEVICE_STATE;
    auto* context=H15dLiveGetContext(device);
    if(!context) return STATUS_INVALID_DEVICE_STATE;
    InterlockedExchange(&context->ready,1);
    InterlockedIncrement(&context->generation);
    return STATUS_SUCCESS;
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
    if(!context || InterlockedCompareExchange(&context->ready,0,0)==0) {
        WdfRequestComplete(request,STATUS_DEVICE_NOT_READY);
        return;
    }
    if(InterlockedCompareExchange(&context->consumed,1,0)!=0) {
        WdfRequestComplete(request,STATUS_INVALID_DEVICE_STATE);
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
            HardwareAccessGate gate;
            PciConfigBootPolicy policy;
            if(gate.OpenForPrepare()) {
                const auto apply=policy.Apply(device,pci,gate);
                if(NT_SUCCESS(apply)) {
                    result.flags|=H15dApplySucceeded;
                    if(ReadPair(device,&result.pgctlApplied,&result.cgctlApplied) &&
                       result.pgctlApplied==kH15dAppliedPgctl &&
                       result.cgctlApplied==kH15dAppliedCgctl)
                        result.flags|=H15dAppliedReadbackExact;
                } else result.transactionStatus=apply;

                if(policy.Restore()) result.flags|=H15dRestoreSucceeded;
                (void)gate.CloseForRelease();

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
                } else if(NT_SUCCESS(result.transactionStatus)) result.transactionStatus=finalStatus;
            } else result.transactionStatus=STATUS_INVALID_DEVICE_STATE;
        } else result.transactionStatus=STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    if((result.flags&kH15dRequiredSuccessFlags)==kH15dRequiredSuccessFlags)
        result.transactionStatus=STATUS_SUCCESS;
    else if(NT_SUCCESS(result.transactionStatus))
        result.transactionStatus=STATUS_DEVICE_CONFIGURATION_ERROR;

    *out=result;
    WdfRequestCompleteWithInformation(request,STATUS_SUCCESS,sizeof(result));
}
