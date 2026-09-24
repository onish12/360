// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>

extern "C" DRIVER_INITIALIZE DriverEntry;
EVT_WDF_DRIVER_DEVICE_ADD Phaser360EvtDeviceAdd;
EVT_WDF_DRIVER_UNLOAD Phaser360EvtDriverUnload;
