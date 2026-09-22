// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include "../driver/hardware_access_gate.h"
#include "../driver/pci_config_attestation.h"
#include "../driver/pci_config_boot_policy.h"

namespace phaser360 { namespace windows {

inline constexpr DEVICE_TYPE kH15dLiveDeviceType=0x8338u;
inline constexpr ULONG kH15dLiveIoctlFunction=0x916u;
inline constexpr ULONG IOCTL_PHASER360_H15D_LIVE_TRANSACTION=
    CTL_CODE(kH15dLiveDeviceType,kH15dLiveIoctlFunction,METHOD_BUFFERED,
             FILE_READ_ACCESS|FILE_WRITE_ACCESS);
static_assert(IOCTL_PHASER360_H15D_LIVE_TRANSACTION==0x8338e458u,
              "H15D vendor IOCTL ABI");

inline constexpr ULONG kH15dExpectedPgctl=0x00000010u;
inline constexpr ULONG kH15dExpectedCgctl=0x807b0dffu;
inline constexpr ULONG kH15dAppliedPgctl=0x00000014u;
inline constexpr ULONG kH15dAppliedCgctl=0x807b0dfdu;

enum H15dLiveFlags : ULONG {
    H15dAttestationValid=1u<<0,
    H15dExpectedBaselineMatch=1u<<1,
    H15dApplySucceeded=1u<<2,
    H15dAppliedReadbackExact=1u<<3,
    H15dRestoreSucceeded=1u<<4,
    H15dFinalBaselineExact=1u<<5,
    H15dNoMmio=1u<<6,
    H15dNoDma=1u<<7,
    H15dNoDspBoot=1u<<8,
    H15dOneShot=1u<<9,
    H15dFullConfigRestoredExact=1u<<10
};
inline constexpr ULONG kH15dRequiredSuccessFlags=0x7ffu;

struct H15dLiveRequestV1 {
    ULONG version=1u;
    ULONG size=16u;
    ULONG expectedPgctl=kH15dExpectedPgctl;
    ULONG expectedCgctl=kH15dExpectedCgctl;
};
static_assert(sizeof(H15dLiveRequestV1)==16u,"H15D request ABI");

struct H15dLiveResultV1 {
    ULONG version=1u;
    ULONG size=52u;
    LONG transactionStatus=STATUS_DEVICE_NOT_READY;
    ULONG flags=H15dNoMmio|H15dNoDma|H15dNoDspBoot|H15dOneShot;
    ULONG generation=0;
    USHORT vendorId=0;
    USHORT deviceId=0;
    UCHAR headerType=0;
    UCHAR firstCapability=0;
    UCHAR capabilityCount=0;
    UCHAR reserved=0;
    ULONG pgctlBefore=0;
    ULONG cgctlBefore=0;
    ULONG pgctlApplied=0;
    ULONG cgctlApplied=0;
    ULONG pgctlRestored=0;
    ULONG cgctlRestored=0;
};
static_assert(sizeof(H15dLiveResultV1)==52u,"H15D result ABI");

struct H15dLiveDeviceContext {
    alignas(HardwareAccessGate) UCHAR gateStorage[sizeof(HardwareAccessGate)]={};
    BOOLEAN gateConstructed=FALSE;
    volatile LONG ready=0;
    volatile LONG d0=0;
    volatile LONG consumed=0;
    volatile LONG generation=0;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(H15dLiveDeviceContext,H15dLiveGetContext)

EVT_WDF_DRIVER_DEVICE_ADD H15dLiveEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE H15dLiveEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE H15dLiveEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY H15dLiveEvtDeviceD0Entry;
EVT_WDF_DEVICE_D0_EXIT H15dLiveEvtDeviceD0Exit;
EVT_WDF_DEVICE_SURPRISE_REMOVAL H15dLiveEvtSurpriseRemoval;
EVT_WDF_OBJECT_CONTEXT_CLEANUP H15dLiveEvtContextCleanup;
EVT_WDF_DEVICE_FILE_CREATE H15dLiveEvtDeviceFileCreate;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL H15dLiveEvtIoDeviceControl;

extern const GUID kH15dLiveInterfaceGuid;

} }
