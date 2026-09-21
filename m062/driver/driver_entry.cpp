// SPDX-License-Identifier: MIT
#include "driver_entry.h"
#include "device_owner.h"

using phaser360::windows::DeviceOwner;
using phaser360::windows::PnpResources;

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject,PUNICODE_STRING registryPath) {
    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config,Phaser360EvtDeviceAdd);

    // No global hardware state, no firmware I/O, no device access.
    return WdfDriverCreate(
        driverObject,registryPath,WDF_NO_OBJECT_ATTRIBUTES,&config,WDF_NO_HANDLE);
}

NTSTATUS Phaser360EvtDeviceAdd(
    WDFDRIVER driver,PWDFDEVICE_INIT deviceInit) {
    UNREFERENCED_PARAMETER(driver);
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !deviceInit)
        return STATUS_INVALID_DEVICE_STATE;

    WDF_OBJECT_ATTRIBUTES attributes;
    auto status=PnpResources::Configure(deviceInit,&attributes);
    if(!NT_SUCCESS(status)) return status;

    WDFDEVICE device=nullptr;
    status=WdfDeviceCreate(&deviceInit,&attributes,&device);
    if(!NT_SUCCESS(status)) return status;

    // Child WDF objects (wait lock/work item/DPC/interrupt shell) are created
    // only after the WDFDEVICE exists, as required by KMDF DeviceAdd ordering.
    return DeviceOwner::CreateInDeviceContext(device);
}
