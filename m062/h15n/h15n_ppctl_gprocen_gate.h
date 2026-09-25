// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include "../driver/hardware_access_gate.h"
#include "../driver/pci_config_attestation.h"

namespace phaser360 { namespace windows {

inline constexpr DEVICE_TYPE kH15nDeviceType=0x8341u;
inline constexpr ULONG kH15nIoctlFunction=0x921u;
inline constexpr ULONG IOCTL_PHASER360_H15N_TRANSACTION=
    CTL_CODE(kH15nDeviceType,kH15nIoctlFunction,METHOD_BUFFERED,
             FILE_READ_ACCESS|FILE_WRITE_ACCESS);
static_assert(IOCTL_PHASER360_H15N_TRANSACTION==0x8341e484u,"H15N IOCTL ABI");

inline constexpr ULONG kH15nHdaBytes=0x4000u;
inline constexpr ULONG kH15nDspBytes=0x100000u;
inline constexpr ULONG kH15nExpectedPgctl=0x00000010u;
inline constexpr ULONG kH15nExpectedCgctl=0x807b0dffu;

struct H15nObservation {
    USHORT hdaGcap; UCHAR hdaVmin; UCHAR hdaVmaj; ULONG hdaGctl;
    UCHAR hdaCorbctl; UCHAR hdaRirbctl; UCHAR totalStreams; UCHAR reserved0;
    ULONG streamRunMask; ULONG hdaIntelEm2; ULONG dspAdspcs; ULONG dspAdspic;
    ULONG dspAdspis; ULONG dspHipci; ULONG dspHipcie; ULONG dspHipcctl; ULONG dspRomStatus;
};
static_assert(sizeof(H15nObservation)==48u,"H15N observation ABI");

enum H15nFlags : ULONG {
    H15nAttestationValid=1u<<0,
    H15nExpectedBaselineMatch=1u<<1,
    H15nBeforeCaptured=1u<<2,
    H15nHdaTransportIdle=1u<<3,
    H15nCrstSetWritten=1u<<4,
    H15nCrstReadyObserved=1u<<5,
    H15nReadyCaptured=1u<<6,
    H15nPpExactDiscovered=1u<<7,
    H15nPpctlZeroBaseline=1u<<8,
    H15nGprocenSetWritten=1u<<9,
    H15nGprocenSetObserved=1u<<10,
    H15nEnabledCaptured=1u<<11,
    H15nCpa0Observed=1u<<12,
    H15nGprocenClearWritten=1u<<13,
    H15nGprocenClearObserved=1u<<14,
    H15nPpctlRestoredExact=1u<<15,
    H15nDisabledCaptured=1u<<16,
    H15nCrstClearWritten=1u<<17,
    H15nCrstRestoredObserved=1u<<18,
    H15nFinalCaptured=1u<<19,
    H15nGctlRestoredExact=1u<<20,
    H15nNoPciWrite=1u<<21,
    H15nNoDspMmioWrite=1u<<22,
    H15nNoEm2Write=1u<<23,
    H15nNoDma=1u<<24,
    H15nNoIrqOwnership=1u<<25,
    H15nNoFirmware=1u<<26,
    H15nNoDspBoot=1u<<27,
    H15nNoPlayback=1u<<28,
    H15nOneShot=1u<<29,
    H15nOnlyGprocenPpctlWrite=1u<<30,
    H15nCore0StateReadOnly=1u<<31
};
inline constexpr ULONG kH15nRequiredFlags=0xffffefffu;

struct H15nRequestV1 { ULONG version; ULONG size; ULONG expectedPgctl; ULONG expectedCgctl; };
static_assert(sizeof(H15nRequestV1)==16u,"H15N request ABI");

struct H15nResultV1 {
    ULONG version; ULONG size; LONG transactionStatus; ULONG flags; ULONG generation;
    USHORT vendorId; USHORT deviceId; UCHAR headerType; UCHAR firstCapability;
    UCHAR capabilityCount; UCHAR reserved0; ULONG pgctl; ULONG cgctl;
    ULONGLONG hdaPhysical; ULONGLONG dspPhysical; ULONG hdaLength; ULONG dspLength;
    H15nObservation before;
    H15nObservation ready;
    H15nObservation enabled;
    H15nObservation disabled;
    H15nObservation finalState;
    ULONG llch; ULONG ppOffset; ULONG ppHeader;
    ULONG ppctlBefore; ULONG ppstsBefore;
    ULONG ppctlEnabled; ULONG ppstsEnabled;
    ULONG ppctlDisabled; ULONG ppstsDisabled;
};
static_assert(sizeof(H15nResultV1)==344u,"H15N result ABI");

struct H15nDeviceContext {
    alignas(HardwareAccessGate) UCHAR gateStorage[sizeof(HardwareAccessGate)];
    BOOLEAN gateConstructed; UCHAR reserved1[7];
    UCHAR* hda; UCHAR* dsp; ULONG hdaLength; ULONG dspLength;
    ULONGLONG hdaPhysical; ULONGLONG dspPhysical;
    volatile LONG ready; volatile LONG d0; volatile LONG consumed; volatile LONG generation;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(H15nDeviceContext,H15nGetContext)

EVT_WDF_DRIVER_DEVICE_ADD H15nEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE H15nEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE H15nEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY H15nEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT H15nEvtD0Exit;
EVT_WDF_DEVICE_SURPRISE_REMOVAL H15nEvtSurpriseRemoval;
EVT_WDF_OBJECT_CONTEXT_CLEANUP H15nEvtCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL H15nEvtIoDeviceControl;
extern const GUID kH15nInterfaceGuid;

} }
