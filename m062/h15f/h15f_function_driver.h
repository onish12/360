// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>

namespace phaser360 { namespace windows {

inline constexpr DEVICE_TYPE kH15fDeviceType=0x833au;
inline constexpr ULONG kH15fIoctlFunction=0x918u;
inline constexpr ULONG IOCTL_PHASER360_H15F_SNAPSHOT=
    CTL_CODE(kH15fDeviceType,kH15fIoctlFunction,METHOD_BUFFERED,FILE_READ_ACCESS);
static_assert(IOCTL_PHASER360_H15F_SNAPSHOT==0x833a6460u,"H15F IOCTL ABI");

inline constexpr ULONG kH15fHdaBytes=0x4000u;
inline constexpr ULONG kH15fDspBytes=0x100000u;

enum H15fFlags : ULONG {
    H15fDeviceAdded=1u<<0,
    H15fPrepared=1u<<1,
    H15fResourcesExact=1u<<2,
    H15fD0Entered=1u<<3,
    H15fNoMmio=1u<<4,
    H15fNoPciWrite=1u<<5,
    H15fNoDma=1u<<6,
    H15fNoIrqOwnership=1u<<7,
    H15fNoFirmware=1u<<8,
    H15fNoPlayback=1u<<9
};
inline constexpr ULONG kH15fRequiredLiveFlags=0x3ffu;

struct H15fSnapshotV1 {
    ULONG version=1u;
    ULONG size=96u;
    ULONG flags=H15fNoMmio|H15fNoPciWrite|H15fNoDma|
                H15fNoIrqOwnership|H15fNoFirmware|H15fNoPlayback;
    LONG lastStatus=STATUS_DEVICE_NOT_READY;
    ULONG generation=0;
    ULONG prepareCount=0;
    ULONG d0EntryCount=0;
    ULONG d0ExitCount=0;
    ULONG releaseCount=0;
    ULONG rawResourceCount=0;
    ULONG translatedResourceCount=0;
    ULONG memoryCount=0;
    ULONG interruptCount=0;
    ULONG reserved0=0;
    ULONGLONG hdaPhysical=0;
    ULONGLONG dspPhysical=0;
    ULONG hdaLength=0;
    ULONG dspLength=0;
    ULONG interruptFlags=0;
    ULONG reserved1=0;
    ULONG reserved2=0;
    ULONG reserved3=0;
};
static_assert(sizeof(H15fSnapshotV1)==96u,"H15F snapshot ABI");

struct H15fDeviceContext {
    H15fSnapshotV1 snapshot{};
    WDFSPINLOCK lock=nullptr;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(H15fDeviceContext,H15fGetContext)

EVT_WDF_DRIVER_DEVICE_ADD H15fEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE H15fEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE H15fEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY H15fEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT H15fEvtD0Exit;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL H15fEvtIoDeviceControl;

extern const GUID kH15fInterfaceGuid;

} }
