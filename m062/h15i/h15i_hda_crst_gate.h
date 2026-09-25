// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include "../driver/hardware_access_gate.h"
#include "../driver/pci_config_attestation.h"

namespace phaser360 { namespace windows {

inline constexpr DEVICE_TYPE kH15iDeviceType=0x833du;
inline constexpr ULONG kH15iIoctlFunction=0x91bu;
inline constexpr ULONG IOCTL_PHASER360_H15I_TRANSACTION=
    CTL_CODE(kH15iDeviceType,kH15iIoctlFunction,METHOD_BUFFERED,
             FILE_READ_ACCESS|FILE_WRITE_ACCESS);
static_assert(IOCTL_PHASER360_H15I_TRANSACTION==0x833de46cu,"H15I IOCTL ABI");

inline constexpr ULONG kH15iHdaBytes=0x4000u;
inline constexpr ULONG kH15iDspBytes=0x100000u;
inline constexpr ULONG kH15iExpectedPgctl=0x00000010u;
inline constexpr ULONG kH15iExpectedCgctl=0x807b0dffu;

struct H15iRegisterSet {
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
};
static_assert(sizeof(H15iRegisterSet)==32u,"H15I register ABI");

enum H15iFlags : ULONG {
    H15iAttestationValid=1u<<0,
    H15iExpectedBaselineMatch=1u<<1,
    H15iBeforeCaptured=1u<<2,
    H15iCrstSetWritten=1u<<3,
    H15iCrstReadyObserved=1u<<4,
    H15iReadyCaptured=1u<<5,
    H15iCrstClearWritten=1u<<6,
    H15iCrstRestoredObserved=1u<<7,
    H15iRestoredCaptured=1u<<8,
    H15iGctlRestoredExact=1u<<9,
    H15iNoPciWrite=1u<<10,
    H15iNoDspMmioWrite=1u<<11,
    H15iNoDma=1u<<12,
    H15iNoIrqOwnership=1u<<13,
    H15iNoFirmware=1u<<14,
    H15iNoDspBoot=1u<<15,
    H15iOneShot=1u<<16,
    H15iSplitMappings=1u<<17
};
inline constexpr ULONG kH15iRequiredFlags=0x3ffffu;

struct H15iRequestV1 {
    ULONG version;
    ULONG size;
    ULONG expectedPgctl;
    ULONG expectedCgctl;
};
static_assert(sizeof(H15iRequestV1)==16u,"H15I request ABI");

struct H15iResultV1 {
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
    H15iRegisterSet before;
    H15iRegisterSet ready;
    H15iRegisterSet restored;
};
static_assert(sizeof(H15iResultV1)==160u,"H15I result ABI");

struct H15iDeviceContext {
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
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(H15iDeviceContext,H15iGetContext)

EVT_WDF_DRIVER_DEVICE_ADD H15iEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE H15iEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE H15iEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY H15iEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT H15iEvtD0Exit;
EVT_WDF_DEVICE_SURPRISE_REMOVAL H15iEvtSurpriseRemoval;
EVT_WDF_OBJECT_CONTEXT_CLEANUP H15iEvtCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL H15iEvtIoDeviceControl;

extern const GUID kH15iInterfaceGuid;

} }
