// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include "../driver/hardware_access_gate.h"
#include "../driver/pci_config_attestation.h"

namespace phaser360 { namespace windows {

inline constexpr DEVICE_TYPE kH15jDeviceType=0x833du;
inline constexpr ULONG kH15jIoctlFunction=0x91cu;
inline constexpr ULONG IOCTL_PHASER360_H15J_TRANSACTION=
    CTL_CODE(kH15jDeviceType,kH15jIoctlFunction,METHOD_BUFFERED,
             FILE_READ_ACCESS|FILE_WRITE_ACCESS);
static_assert(IOCTL_PHASER360_H15J_TRANSACTION==0x833de470u,"H15J IOCTL ABI");

inline constexpr ULONG kH15jHdaBytes=0x4000u;
inline constexpr ULONG kH15jDspBytes=0x100000u;
inline constexpr ULONG kH15jExpectedPgctl=0x00000010u;
inline constexpr ULONG kH15jExpectedCgctl=0x807b0dffu;

struct H15jRegisterSet {
    USHORT hdaGcap;
    UCHAR hdaVmin;
    UCHAR hdaVmaj;
    ULONG hdaGctl;
    ULONG hdaIntelEm2;
    UCHAR hdaCorbctl;
    UCHAR hdaRirbctl;
    USHORT hdaStreamCount;
    ULONG hdaStreamRunMask;
    ULONG dspAdspcs;
    ULONG dspAdspic;
    ULONG dspAdspis;
    ULONG dspHipci;
    ULONG dspHipcie;
    ULONG dspHipcctl;
    ULONG dspRomStatus;
};
static_assert(sizeof(H15jRegisterSet)==48u,"H15J register ABI");

enum H15jFlags : ULONG {
    H15jAttestationValid=1u<<0,
    H15jExpectedBaselineMatch=1u<<1,
    H15jBeforeCaptured=1u<<2,
    H15jBeforeQuiescent=1u<<3,
    H15jCrstSetWritten=1u<<4,
    H15jCrstReadyObserved=1u<<5,
    H15jReadyCaptured=1u<<6,
    H15jReadyQuiescent=1u<<7,
    H15jCrstClearWritten=1u<<8,
    H15jCrstRestoredObserved=1u<<9,
    H15jRestoredCaptured=1u<<10,
    H15jGctlRestoredExact=1u<<11,
    H15jNoPciWrite=1u<<12,
    H15jNoDspMmioWrite=1u<<13,
    H15jNoDma=1u<<14,
    H15jNoIrqOwnership=1u<<15,
    H15jNoFirmware=1u<<16,
    H15jNoDspBoot=1u<<17,
    H15jOneShot=1u<<18,
    H15jSplitMappings=1u<<19
};
inline constexpr ULONG kH15jRequiredFlags=0xfffffu;

struct H15jRequestV1 {
    ULONG version;
    ULONG size;
    ULONG expectedPgctl;
    ULONG expectedCgctl;
};
static_assert(sizeof(H15jRequestV1)==16u,"H15J request ABI");

struct H15jResultV1 {
    ULONG version;
    ULONG size;
    LONG transactionStatus;
    ULONG flags;
    ULONG generation;
    USHORT vendorId;
    USHORT deviceId;
    UCHAR headerType;
    UCHAR firstCapability;
    UCHAR capabilityCount;
    UCHAR reserved0;
    ULONG pgctl;
    ULONG cgctl;
    ULONGLONG hdaPhysical;
    ULONGLONG dspPhysical;
    ULONG hdaLength;
    ULONG dspLength;
    H15jRegisterSet before;
    H15jRegisterSet ready;
    H15jRegisterSet restored;
};
static_assert(sizeof(H15jResultV1)==208u,"H15J result ABI");

struct H15jDeviceContext {
    alignas(HardwareAccessGate) UCHAR gateStorage[sizeof(HardwareAccessGate)];
    BOOLEAN gateConstructed;
    UCHAR reserved1[7];
    UCHAR* hda;
    UCHAR* dsp;
    ULONG hdaLength;
    ULONG dspLength;
    ULONGLONG hdaPhysical;
    ULONGLONG dspPhysical;
    volatile LONG ready;
    volatile LONG d0;
    volatile LONG consumed;
    volatile LONG generation;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(H15jDeviceContext,H15jGetContext)

EVT_WDF_DRIVER_DEVICE_ADD H15jEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE H15jEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE H15jEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY H15jEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT H15jEvtD0Exit;
EVT_WDF_DEVICE_SURPRISE_REMOVAL H15jEvtSurpriseRemoval;
EVT_WDF_OBJECT_CONTEXT_CLEANUP H15jEvtCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL H15jEvtIoDeviceControl;

extern const GUID kH15jInterfaceGuid;

} }
