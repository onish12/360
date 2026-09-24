// SPDX-License-Identifier: MIT
#include "driver_entry.h"
#include "device_owner.h"
#include "stage_trace.h"

using phaser360::windows::DeviceOwner;
using phaser360::windows::PnpResources;
using phaser360::windows::StageTrace;
using phaser360::windows::StageTraceRegister;
using phaser360::windows::StageTraceStatus;
using phaser360::windows::StageTraceUnregister;

extern "C"
NTSTATUS DriverEntry(PDRIVER_OBJECT driverObject,PUNICODE_STRING registryPath) {
    const auto traceStatus=StageTraceRegister();
    if(!NT_SUCCESS(traceStatus)) return traceStatus;
    StageTrace(L"A00_DRIVER_ENTRY");

    WDF_DRIVER_CONFIG config;
    WDF_DRIVER_CONFIG_INIT(&config,Phaser360EvtDeviceAdd);
    config.EvtDriverUnload=Phaser360EvtDriverUnload;

    // No global hardware state, no firmware I/O, no device access.
    const auto status=WdfDriverCreate(
        driverObject,registryPath,WDF_NO_OBJECT_ATTRIBUTES,&config,WDF_NO_HANDLE);
    if(!NT_SUCCESS(status)) {
        StageTraceStatus(L"A01_WDF_DRIVER_CREATE_FAIL",status);
        StageTraceUnregister();
    }
    return status;
}

void Phaser360EvtDriverUnload(WDFDRIVER driver) {
    UNREFERENCED_PARAMETER(driver);
    StageTrace(L"A99_DRIVER_UNLOAD");
    StageTraceUnregister();
}

NTSTATUS Phaser360EvtDeviceAdd(
    WDFDRIVER driver,PWDFDEVICE_INIT deviceInit) {
    UNREFERENCED_PARAMETER(driver);
    StageTrace(L"A10_DEVICE_ADD_ENTER");
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !deviceInit)
        return STATUS_INVALID_DEVICE_STATE;

    WDF_OBJECT_ATTRIBUTES attributes;
    auto status=PnpResources::Configure(deviceInit,&attributes);
    if(!NT_SUCCESS(status)) {
        StageTraceStatus(L"A11_PNP_CONFIGURE_FAIL",status);
        return status;
    }

    WDFDEVICE device=nullptr;
    status=WdfDeviceCreate(&deviceInit,&attributes,&device);
    if(!NT_SUCCESS(status)) {
        StageTraceStatus(L"A12_WDF_DEVICE_CREATE_FAIL",status);
        return status;
    }
    StageTrace(L"A13_WDF_DEVICE_CREATE_OK");

    // Child WDF objects (wait lock/work item/DPC/interrupt shell) are created
    // only after the WDFDEVICE exists, as required by KMDF DeviceAdd ordering.
    status=DeviceOwner::CreateInDeviceContext(device);
    StageTraceStatus(NT_SUCCESS(status)
        ? L"A20_DEVICE_OWNER_OK" : L"A20_DEVICE_OWNER_FAIL",status);
    return status;
}
