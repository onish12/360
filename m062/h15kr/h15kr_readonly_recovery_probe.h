// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>

namespace phaser360 { namespace windows {

inline constexpr DEVICE_TYPE kH15krDeviceType=0x833eu;
inline constexpr ULONG kH15krIoctlFunction=0x91eu;
inline constexpr ULONG IOCTL_PHASER360_H15KR_SNAPSHOT=
    CTL_CODE(kH15krDeviceType,kH15krIoctlFunction,METHOD_BUFFERED,FILE_READ_ACCESS);
static_assert(IOCTL_PHASER360_H15KR_SNAPSHOT==0x833e6478u,"H15KR IOCTL ABI");

inline constexpr ULONG kH15krHdaBytes=0x4000u;
inline constexpr ULONG kH15krDspBytes=0x100000u;

enum H15krFlags : ULONG {
    H15krDeviceAdded=1u<<0,
    H15krPrepared=1u<<1,
    H15krResourcesExact=1u<<2,
    H15krMappingsLive=1u<<3,
    H15krD0Entered=1u<<4,
    H15krSnapshotValid=1u<<5,
    H15krNoMmioWrite=1u<<6,
    H15krNoPciWrite=1u<<7,
    H15krNoDma=1u<<8,
    H15krNoIrqOwnership=1u<<9,
    H15krNoFirmware=1u<<10,
    H15krNoPlayback=1u<<11,
    H15krMappingsReadOnly=1u<<12
};
inline constexpr ULONG kH15krRequiredLiveFlags=0x1fffu;

struct H15krSnapshotV1 {
    ULONG version;
    ULONG size;
    ULONG flags;
    LONG lastStatus;
    ULONG generation;
    ULONG prepareCount;
    ULONG d0EntryCount;
    ULONG d0ExitCount;
    ULONG releaseCount;
    ULONG rawResourceCount;
    ULONG translatedResourceCount;
    ULONG memoryCount;
    ULONG interruptCount;
    ULONG reservedAlign;
    ULONGLONG hdaPhysical;
    ULONGLONG dspPhysical;
    ULONG hdaLength;
    ULONG dspLength;
    USHORT hdaGcap;
    UCHAR hdaVmin;
    UCHAR hdaVmaj;
    ULONG hdaGctl;
    UCHAR hdaCorbctl;
    UCHAR hdaRirbctl;
    UCHAR totalStreams;
    UCHAR reserved0;
    ULONG streamRunMask;
    ULONG hdaIntelEm2;
    ULONG dspAdspcs;
    ULONG dspAdspic;
    ULONG dspAdspis;
    ULONG dspHipci;
    ULONG dspHipcie;
    ULONG dspHipcctl;
    ULONG dspRomStatus;
    ULONG reserved1;
};
static_assert(sizeof(H15krSnapshotV1)==136u,"H15KR snapshot ABI");

struct H15krDeviceContext {
    H15krSnapshotV1 snapshot;
    WDFSPINLOCK lock;
    UCHAR* hda;
    UCHAR* dsp;
    ULONG hdaLength;
    ULONG dspLength;
    volatile LONG removed;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(H15krDeviceContext,H15krGetContext)

EVT_WDF_DRIVER_DEVICE_ADD H15krEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE H15krEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE H15krEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY H15krEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT H15krEvtD0Exit;
EVT_WDF_DEVICE_SURPRISE_REMOVAL H15krEvtSurpriseRemoval;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL H15krEvtIoDeviceControl;

extern const GUID kH15krInterfaceGuid;

} }
