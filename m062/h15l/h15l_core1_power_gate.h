// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include "../driver/hardware_access_gate.h"
#include "../driver/pci_config_attestation.h"

namespace phaser360 { namespace windows {

inline constexpr DEVICE_TYPE kH15lDeviceType=0x833fu;
inline constexpr ULONG kH15lIoctlFunction=0x91fu;
inline constexpr ULONG IOCTL_PHASER360_H15L_TRANSACTION=
    CTL_CODE(kH15lDeviceType,kH15lIoctlFunction,METHOD_BUFFERED,FILE_READ_ACCESS|FILE_WRITE_ACCESS);
static_assert(IOCTL_PHASER360_H15L_TRANSACTION==0x833fe47cu,"H15L IOCTL ABI");

inline constexpr ULONG kH15lHdaBytes=0x4000u;
inline constexpr ULONG kH15lDspBytes=0x100000u;
inline constexpr ULONG kH15lExpectedPgctl=0x00000010u;
inline constexpr ULONG kH15lExpectedCgctl=0x807b0dffu;

struct H15lObservation {
    USHORT hdaGcap; UCHAR hdaVmin; UCHAR hdaVmaj; ULONG hdaGctl;
    UCHAR hdaCorbctl; UCHAR hdaRirbctl; UCHAR totalStreams; UCHAR reserved0;
    ULONG streamRunMask; ULONG hdaIntelEm2; ULONG dspAdspcs; ULONG dspAdspic;
    ULONG dspAdspis; ULONG dspHipci; ULONG dspHipcie; ULONG dspHipcctl; ULONG dspRomStatus;
};
static_assert(sizeof(H15lObservation)==48u,"H15L observation ABI");

enum H15lFlags : ULONG {
    H15lAttestationValid=1u<<0,
    H15lExpectedBaselineMatch=1u<<1,
    H15lBeforeCaptured=1u<<2,
    H15lHdaTransportIdle=1u<<3,
    H15lCstall1Written=1u<<4,
    H15lCstall1Observed=1u<<5,
    H15lCrst1Written=1u<<6,
    H15lCrst1Observed=1u<<7,
    H15lResetCaptured=1u<<8,
    H15lSpa1SetWritten=1u<<9,
    H15lCpa1SetObserved=1u<<10,
    H15lPoweredCaptured=1u<<11,
    H15lSpa1ClearWritten=1u<<12,
    H15lCpa1ClearObserved=1u<<13,
    H15lDepoweredCaptured=1u<<14,
    H15lCrst1RollbackWritten=1u<<15,
    H15lCrst1RollbackObserved=1u<<16,
    H15lCstall1RollbackWritten=1u<<17,
    H15lCstall1RollbackObserved=1u<<18,
    H15lRestoredCaptured=1u<<19,
    H15lAdspcsRestoredExact=1u<<20,
    H15lNoHdaMmioWrite=1u<<21,
    H15lNoPciWrite=1u<<22,
    H15lNoDma=1u<<23,
    H15lNoIrqOwnership=1u<<24,
    H15lNoFirmware=1u<<25,
    H15lNoDspBoot=1u<<26,
    H15lOneShot=1u<<27,
    H15lSplitMappings=1u<<28,
    H15lOnlyAdspcsWrite=1u<<29,
    H15lNoCpaWrite=1u<<30,
    H15lCore0Untouched=1u<<31
};
inline constexpr ULONG kH15lRequiredFlags=0xffffffffu;

struct H15lRequestV1 { ULONG version; ULONG size; ULONG expectedPgctl; ULONG expectedCgctl; };
static_assert(sizeof(H15lRequestV1)==16u,"H15L request ABI");

struct H15lResultV1 {
    ULONG version; ULONG size; LONG transactionStatus; ULONG flags; ULONG generation;
    USHORT vendorId; USHORT deviceId; UCHAR headerType; UCHAR firstCapability;
    UCHAR capabilityCount; UCHAR reserved0; ULONG pgctl; ULONG cgctl;
    ULONGLONG hdaPhysical; ULONGLONG dspPhysical; ULONG hdaLength; ULONG dspLength;
    H15lObservation before;
    H15lObservation reset;
    H15lObservation powered;
    H15lObservation depowered;
    H15lObservation restored;
};
static_assert(sizeof(H15lResultV1)==304u,"H15L result ABI");

struct H15lDeviceContext {
    alignas(HardwareAccessGate) UCHAR gateStorage[sizeof(HardwareAccessGate)];
    BOOLEAN gateConstructed; UCHAR reserved1[7];
    UCHAR* hda; UCHAR* dsp; ULONG hdaLength; ULONG dspLength;
    ULONGLONG hdaPhysical; ULONGLONG dspPhysical;
    volatile LONG ready; volatile LONG d0; volatile LONG consumed; volatile LONG generation;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(H15lDeviceContext,H15lGetContext)

EVT_WDF_DRIVER_DEVICE_ADD H15lEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE H15lEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE H15lEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY H15lEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT H15lEvtD0Exit;
EVT_WDF_DEVICE_SURPRISE_REMOVAL H15lEvtSurpriseRemoval;
EVT_WDF_OBJECT_CONTEXT_CLEANUP H15lEvtCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL H15lEvtIoDeviceControl;

extern const GUID kH15lInterfaceGuid;

} }
