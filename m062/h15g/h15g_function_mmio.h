// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>

namespace phaser360 { namespace windows {

inline constexpr DEVICE_TYPE kH15gDeviceType=0x833bu;
inline constexpr ULONG kH15gIoctlFunction=0x919u;
inline constexpr ULONG IOCTL_PHASER360_H15G_SNAPSHOT=
    CTL_CODE(kH15gDeviceType,kH15gIoctlFunction,METHOD_BUFFERED,FILE_READ_ACCESS);
static_assert(IOCTL_PHASER360_H15G_SNAPSHOT==0x833b6464u,"H15G IOCTL ABI");

inline constexpr ULONG kH15gHdaBytes=0x4000u;
inline constexpr ULONG kH15gDspBytes=0x100000u;

inline constexpr ULONG kH15gHdaGcap=0x0000u;
inline constexpr ULONG kH15gHdaVmin=0x0002u;
inline constexpr ULONG kH15gHdaVmaj=0x0003u;
inline constexpr ULONG kH15gHdaGctl=0x0008u;
inline constexpr ULONG kH15gIntelEm2=0x1030u;

inline constexpr ULONG kH15gDspAdspcs=0x0004u;
inline constexpr ULONG kH15gDspAdspis=0x000cu;
inline constexpr ULONG kH15gDspHipci=0x0048u;
inline constexpr ULONG kH15gDspHipcie=0x004cu;
inline constexpr ULONG kH15gDspRomStatus=0x80000u;

enum H15gFlags : ULONG {
    H15gDeviceAdded=1u<<0,
    H15gPrepared=1u<<1,
    H15gResourcesExact=1u<<2,
    H15gMappingsLive=1u<<3,
    H15gD0Entered=1u<<4,
    H15gSnapshotValid=1u<<5,
    H15gNoMmioWrite=1u<<6,
    H15gNoPciWrite=1u<<7,
    H15gNoDma=1u<<8,
    H15gNoIrqOwnership=1u<<9,
    H15gNoFirmware=1u<<10,
    H15gNoPlayback=1u<<11,
    H15gMappingsReadOnly=1u<<12
};
inline constexpr ULONG kH15gRequiredLiveFlags=0x1fffu;

struct H15gSnapshotV1 {
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
    ULONGLONG hdaPhysical;
    ULONGLONG dspPhysical;
    ULONG hdaLength;
    ULONG dspLength;
    USHORT hdaGcap;
    UCHAR hdaVmin;
    UCHAR hdaVmaj;
    ULONG hdaGctl;
    ULONG hdaIntelEm2;
    ULONG dspAdspcs;
    ULONG dspAdspis;
    ULONG dspHipci;
    ULONG dspHipcie;
    ULONG dspRomStatus;
    ULONG reserved;
};
static_assert(sizeof(H15gSnapshotV1)==120u,"H15G snapshot ABI");

struct H15gDeviceContext {
    H15gSnapshotV1 snapshot;
    WDFSPINLOCK lock;
    UCHAR* hda;
    UCHAR* dsp;
    ULONG hdaLength;
    ULONG dspLength;
    volatile LONG removed;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(H15gDeviceContext,H15gGetContext)

EVT_WDF_DRIVER_DEVICE_ADD H15gEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE H15gEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE H15gEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY H15gEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT H15gEvtD0Exit;
EVT_WDF_DEVICE_SURPRISE_REMOVAL H15gEvtSurpriseRemoval;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL H15gEvtIoDeviceControl;

extern const GUID kH15gInterfaceGuid;

} }
