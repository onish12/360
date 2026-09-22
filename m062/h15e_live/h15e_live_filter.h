// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include "../driver/pci_config_attestation.h"

namespace phaser360 { namespace windows {

inline constexpr DEVICE_TYPE kH15eLiveDeviceType=0x8339u;
inline constexpr ULONG kH15eLiveIoctlFunction=0x917u;
inline constexpr ULONG IOCTL_PHASER360_H15E_LIVE_SNAPSHOT=
    CTL_CODE(kH15eLiveDeviceType,kH15eLiveIoctlFunction,METHOD_BUFFERED,FILE_READ_ACCESS);
static_assert(IOCTL_PHASER360_H15E_LIVE_SNAPSHOT==0x8339645cu,"H15E vendor IOCTL ABI");

inline constexpr ULONG kH15eHdaBytes=0x4000u;
inline constexpr ULONG kH15eDspBytes=0x100000u;
inline constexpr ULONG kH15eHdaGcap=0x0000u;
inline constexpr ULONG kH15eHdaVmin=0x0002u;
inline constexpr ULONG kH15eHdaVmaj=0x0003u;
inline constexpr ULONG kH15eHdaGctl=0x0008u;
inline constexpr ULONG kH15eIntelEm2=0x1030u;
inline constexpr ULONG kH15eDspAdspcs=0x0004u;
inline constexpr ULONG kH15eDspAdspis=0x000cu;
inline constexpr ULONG kH15eDspHipci=0x0048u;
inline constexpr ULONG kH15eDspHipcie=0x004cu;
inline constexpr ULONG kH15eDspRomStatus=0x80000u;

enum H15eLiveFlags : ULONG {
    H15eAttestationValid=1u<<0,
    H15eResourcesExact=1u<<1,
    H15eHdaMapped=1u<<2,
    H15eDspMapped=1u<<3,
    H15eHdaRegistersValid=1u<<4,
    H15eDspRegistersValid=1u<<5,
    H15eMappingsReleased=1u<<6,
    H15eNoMmioWrite=1u<<7,
    H15eNoPciWrite=1u<<8,
    H15eNoDma=1u<<9,
    H15eNoDspBoot=1u<<10
};
inline constexpr ULONG kH15eRequiredSuccessFlags=0x7ffu;

struct H15eLiveSnapshotV1 {
    ULONG version=1u;
    ULONG size=104u;
    LONG captureStatus=STATUS_DEVICE_NOT_READY;
    ULONG flags=H15eNoMmioWrite|H15eNoPciWrite|H15eNoDma|H15eNoDspBoot;
    ULONG generation=0;
    USHORT vendorId=0;
    USHORT deviceId=0;
    UCHAR headerType=0;
    UCHAR firstCapability=0;
    UCHAR capabilityCount=0;
    UCHAR reserved8=0;
    ULONG pgctl=0;
    ULONG cgctl=0;
    ULONG reserved0=0;
    ULONGLONG hdaPhysical=0;
    ULONGLONG dspPhysical=0;
    ULONG hdaLength=0;
    ULONG dspLength=0;
    USHORT hdaGcap=0;
    UCHAR hdaVmin=0;
    UCHAR hdaVmaj=0;
    ULONG hdaGctl=0;
    ULONG hdaIntelEm2=0;
    ULONG dspAdspcs=0;
    ULONG dspAdspis=0;
    ULONG dspHipci=0;
    ULONG dspHipcie=0;
    ULONG dspRomStatus=0;
    ULONG reserved1=0;
    ULONG reserved2=0;
};
static_assert(sizeof(H15eLiveSnapshotV1)==104u,"H15E snapshot ABI");

struct H15eLiveDeviceContext {
    H15eLiveSnapshotV1 snapshot{};
    WDFSPINLOCK snapshotLock=nullptr;
    volatile LONG ready=0;
    volatile LONG generation=0;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(H15eLiveDeviceContext,H15eLiveGetContext)

EVT_WDF_DRIVER_DEVICE_ADD H15eLiveEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE H15eLiveEvtPrepareHardware;
EVT_WDF_DEVICE_FILE_CREATE H15eLiveEvtDeviceFileCreate;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL H15eLiveEvtIoDeviceControl;

extern const GUID kH15eLiveInterfaceGuid;

} }
