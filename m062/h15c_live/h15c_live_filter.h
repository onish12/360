// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include "../driver/pci_config_attestation.h"

namespace phaser360 { namespace windows {

inline constexpr ULONG IOCTL_PHASER360_H15C_LIVE_SNAPSHOT=0x00226004u;

enum H15cLiveFlags : ULONG {
    H15cLiveCaptureAttempted=1u<<0,
    H15cLiveCaptureValid=1u<<1,
    H15cLiveGetBusDataOnly=1u<<2,
    H15cLiveNoPciWrite=1u<<3
};

struct H15cLiveSnapshotV1 {
    ULONG version=1;
    ULONG size=292u;
    LONG captureStatus=STATUS_DEVICE_NOT_READY;
    ULONG flags=0;
    ULONG generation=0;
    USHORT vendorId=0;
    USHORT deviceId=0;
    UCHAR headerType=0;
    UCHAR firstCapability=0;
    UCHAR capabilityCount=0;
    UCHAR reserved=0;
    ULONG pgctl=0;
    ULONG cgctl=0;
    UCHAR config[kPciConfigSnapshotBytes]={};
};
static_assert(sizeof(H15cLiveSnapshotV1)==292u,"H15C live snapshot ABI");

struct H15cLiveDeviceContext {
    H15cLiveSnapshotV1 snapshot{};
    volatile LONG ready=0;
    volatile LONG generation=0;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(H15cLiveDeviceContext,H15cLiveGetContext)

EVT_WDF_DRIVER_DEVICE_ADD H15cLiveEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE H15cLiveEvtPrepareHardware;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL H15cLiveEvtIoDeviceControl;

extern const GUID kH15cLiveInterfaceGuid;

} }
