// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include "../driver/hardware_access_gate.h"
#include "../driver/pci_config_attestation.h"

namespace phaser360 { namespace windows {

inline constexpr DEVICE_TYPE kH15kDeviceType=0x833du;
inline constexpr ULONG kH15kIoctlFunction=0x91du;
inline constexpr ULONG IOCTL_PHASER360_H15K_TRANSACTION=
    CTL_CODE(kH15kDeviceType,kH15kIoctlFunction,METHOD_BUFFERED,
             FILE_READ_ACCESS|FILE_WRITE_ACCESS);
static_assert(IOCTL_PHASER360_H15K_TRANSACTION==0x833de474u,"H15K IOCTL ABI");

inline constexpr ULONG kH15kHdaBytes=0x4000u;
inline constexpr ULONG kH15kDspBytes=0x100000u;
inline constexpr ULONG kH15kExpectedPgctl=0x00000010u;
inline constexpr ULONG kH15kExpectedCgctl=0x807b0dffu;

struct H15kObservation {
    USHORT hdaGcap; UCHAR hdaVmin; UCHAR hdaVmaj; ULONG hdaGctl;
    UCHAR hdaCorbctl; UCHAR hdaRirbctl; UCHAR totalStreams; UCHAR reserved0;
    ULONG streamRunMask; ULONG hdaIntelEm2; ULONG dspAdspcs; ULONG dspAdspic;
    ULONG dspAdspis; ULONG dspHipci; ULONG dspHipcie; ULONG dspHipcctl; ULONG dspRomStatus;
};
static_assert(sizeof(H15kObservation)==48u,"H15K observation ABI");

enum H15kFlags : ULONG {
    H15kAttestationValid=1u<<0,
    H15kExpectedBaselineMatch=1u<<1,
    H15kBeforeCaptured=1u<<2,
    H15kHdaTransportIdle=1u<<3,
    H15kCstallSetWritten=1u<<4,
    H15kCstallSetObserved=1u<<5,
    H15kStalledCaptured=1u<<6,
    H15kCrstSetWritten=1u<<7,
    H15kCrstSetObserved=1u<<8,
    H15kResetCaptured=1u<<9,
    H15kCrstRollbackWritten=1u<<10,
    H15kCrstRollbackObserved=1u<<11,
    H15kCstallRollbackWritten=1u<<12,
    H15kCstallRollbackObserved=1u<<13,
    H15kRestoredCaptured=1u<<14,
    H15kAdspcsRestoredExact=1u<<15,
    H15kNoHdaMmioWrite=1u<<16,
    H15kNoPciWrite=1u<<17,
    H15kNoDma=1u<<18,
    H15kNoIrqOwnership=1u<<19,
    H15kNoFirmware=1u<<20,
    H15kNoDspBoot=1u<<21,
    H15kOneShot=1u<<22,
    H15kSplitMappings=1u<<23,
    H15kOnlyAdspcsWrite=1u<<24,
    H15kNoSpaWrite=1u<<25
};
inline constexpr ULONG kH15kRequiredFlags=0x03ffffffu;

struct H15kRequestV1 {
    ULONG version;
    ULONG size;
    ULONG expectedPgctl;
    ULONG expectedCgctl;
};
static_assert(sizeof(H15kRequestV1)==16u,"H15K request ABI");

struct H15kResultV1 {
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
    H15kObservation before;
    H15kObservation stalled;
    H15kObservation reset;
    H15kObservation restored;
};
static_assert(sizeof(H15kResultV1)==256u,"H15K result ABI");

struct H15kDeviceContext {
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
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(H15kDeviceContext,H15kGetContext)

EVT_WDF_DRIVER_DEVICE_ADD H15kEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE H15kEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE H15kEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY H15kEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT H15kEvtD0Exit;
EVT_WDF_DEVICE_SURPRISE_REMOVAL H15kEvtSurpriseRemoval;
EVT_WDF_OBJECT_CONTEXT_CLEANUP H15kEvtCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL H15kEvtIoDeviceControl;

extern const GUID kH15kInterfaceGuid;

} }
