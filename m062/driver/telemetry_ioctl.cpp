// SPDX-License-Identifier: MIT
#include "telemetry_ioctl.h"
#include "device_owner.h"

namespace phaser360 { namespace windows {

const GUID kTelemetryInterfaceGuid={
    0x6c50afa1,0xec12,0x4b89,{0xa1,0x50,0x36,0x00,0x61,0x5b,0x00,0x11}
};

NTSTATUS CreateTelemetryEndpoint(WDFDEVICE device) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device)
        return STATUS_INVALID_DEVICE_STATE;

    WDF_IO_QUEUE_CONFIG config;
    WDF_IO_QUEUE_CONFIG_INIT_DEFAULT_QUEUE(&config,WdfIoQueueDispatchSequential);
    // Querying software telemetry must never power the hardware into D0.
    config.PowerManaged=WdfFalse;
    config.EvtIoDeviceControl=Phaser360EvtTelemetryIoctl;

    WDFQUEUE queue=nullptr;
    auto status=WdfIoQueueCreate(
        device,&config,WDF_NO_OBJECT_ATTRIBUTES,&queue);
    if(!NT_SUCCESS(status)) return status;

    return WdfDeviceCreateDeviceInterface(
        device,&kTelemetryInterfaceGuid,nullptr);
}

void Phaser360EvtTelemetryIoctl(
    WDFQUEUE queue,WDFREQUEST request,SIZE_T outputBufferLength,
    SIZE_T inputBufferLength,ULONG ioControlCode) {
    UNREFERENCED_PARAMETER(outputBufferLength);

    if(!queue || !request) return;

    if(ioControlCode!=IOCTL_PHASER360_QUERY_STATUS) {
        WdfRequestComplete(request,STATUS_INVALID_DEVICE_REQUEST);
        return;
    }
    if(inputBufferLength!=0) {
        WdfRequestComplete(request,STATUS_INVALID_PARAMETER);
        return;
    }

    auto* owner=DeviceOwner::FromDevice(WdfIoQueueGetDevice(queue));
    if(!owner) {
        WdfRequestComplete(request,STATUS_INVALID_DEVICE_STATE);
        return;
    }

    TelemetrySnapshotV1* out=nullptr;
    SIZE_T available=0;
    const auto status=WdfRequestRetrieveOutputBuffer(
        request,sizeof(TelemetrySnapshotV1),
        reinterpret_cast<void**>(&out),&available);
    if(!NT_SUCCESS(status)) {
        WdfRequestComplete(request,status);
        return;
    }

    owner->QueryTelemetry(out);
    WdfRequestCompleteWithInformation(
        request,STATUS_SUCCESS,sizeof(TelemetrySnapshotV1));
}

} }
