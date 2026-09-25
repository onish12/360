// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include "../driver/hardware_access_gate.h"
#include "../driver/pci_config_attestation.h"
#include "../driver/pci_config_boot_policy.h"

namespace phaser360 { namespace windows {

inline constexpr DEVICE_TYPE kH15hDeviceType=0x833cu;
inline constexpr ULONG kH15hIoctlFunction=0x91au;
inline constexpr ULONG IOCTL_PHASER360_H15H_TRANSACTION=
    CTL_CODE(kH15hDeviceType,kH15hIoctlFunction,METHOD_BUFFERED,
             FILE_READ_ACCESS|FILE_WRITE_ACCESS);
static_assert(IOCTL_PHASER360_H15H_TRANSACTION==0x833ce468u,"H15H IOCTL ABI");

inline constexpr ULONG kH15hHdaBytes=0x4000u;
inline constexpr ULONG kH15hDspBytes=0x100000u;
inline constexpr ULONG kH15hExpectedPgctl=0x00000010u;
inline constexpr ULONG kH15hExpectedCgctl=0x807b0dffu;
inline constexpr ULONG kH15hAppliedPgctl=0x00000014u;
inline constexpr ULONG kH15hAppliedCgctl=0x807b0dfdu;

struct H15hRegisterSet {
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
static_assert(sizeof(H15hRegisterSet)==32u,"H15H register ABI");

enum H15hFlags : ULONG {
    H15hAttestationValid=1u<<0,
    H15hExpectedBaselineMatch=1u<<1,
    H15hApplySucceeded=1u<<2,
    H15hAppliedPciExact=1u<<3,
    H15hAppliedMmioCaptured=1u<<4,
    H15hRestoreSucceeded=1u<<5,
    H15hFinalPciExact=1u<<6,
    H15hFullConfigRestoredExact=1u<<7,
    H15hNoMmioWrite=1u<<8,
    H15hNoDma=1u<<9,
    H15hNoIrqOwnership=1u<<10,
    H15hNoFirmware=1u<<11,
    H15hNoDspBoot=1u<<12,
    H15hNoPlayback=1u<<13,
    H15hOneShot=1u<<14,
    H15hMappingsReadOnly=1u<<15,
    H15hRestoredMmioCaptured=1u<<16
};
inline constexpr ULONG kH15hRequiredFlags=0x1ffffu;

struct H15hRequestV1 {
    ULONG version;
    ULONG size;
    ULONG expectedPgctl;
    ULONG expectedCgctl;
};
static_assert(sizeof(H15hRequestV1)==16u,"H15H request ABI");

struct H15hResultV1 {
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
    ULONG pgctlBefore;
    ULONG cgctlBefore;
    ULONG pgctlApplied;
    ULONG cgctlApplied;
    ULONG pgctlRestored;
    ULONG cgctlRestored;
    ULONGLONG hdaPhysical;
    ULONGLONG dspPhysical;
    ULONG hdaLength;
    ULONG dspLength;
    H15hRegisterSet before;
    H15hRegisterSet applied;
    H15hRegisterSet restored;
};
static_assert(sizeof(H15hResultV1)==176u,"H15H result ABI");

struct H15hDeviceContext {
    alignas(HardwareAccessGate) UCHAR gateStorage[sizeof(HardwareAccessGate)];
    BOOLEAN gateConstructed;
    UCHAR reserved1[7];
    WDFSPINLOCK lock;
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
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(H15hDeviceContext,H15hGetContext)

EVT_WDF_DRIVER_DEVICE_ADD H15hEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE H15hEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE H15hEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY H15hEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT H15hEvtD0Exit;
EVT_WDF_DEVICE_SURPRISE_REMOVAL H15hEvtSurpriseRemoval;
EVT_WDF_OBJECT_CONTEXT_CLEANUP H15hEvtCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL H15hEvtIoDeviceControl;

extern const GUID kH15hInterfaceGuid;

} }
