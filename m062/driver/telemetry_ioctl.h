// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include "telemetry.h"

namespace phaser360 { namespace windows {

// {6C50AFA1-EC12-4B89-A150-3600615B0011}
extern const GUID kTelemetryInterfaceGuid;

// FILE_DEVICE_UNKNOWN, function 0x800, METHOD_BUFFERED, FILE_READ_ACCESS.
inline constexpr ULONG IOCTL_PHASER360_QUERY_STATUS=0x00226000u;

NTSTATUS CreateTelemetryEndpoint(WDFDEVICE) noexcept;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL Phaser360EvtTelemetryIoctl;

} }
