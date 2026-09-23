// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include "../driver/hardware_access_gate.h"
#include "../driver/pci_config_attestation.h"

namespace phaser360 { namespace windows {

inline constexpr DEVICE_TYPE kH15oDeviceType=0x8342u;
inline constexpr ULONG kH15oIoctlFunction=0x922u;
inline constexpr ULONG IOCTL_PHASER360_H15O_TRANSACTION=
    CTL_CODE(kH15oDeviceType,kH15oIoctlFunction,METHOD_BUFFERED,
             FILE_READ_ACCESS|FILE_WRITE_ACCESS);
static_assert(IOCTL_PHASER360_H15O_TRANSACTION==0x8342e488u,"H15O IOCTL ABI");

inline constexpr ULONG kH15oHdaBytes=0x4000u;
inline constexpr ULONG kH15oDspBytes=0x100000u;
inline constexpr ULONG kH15oExpectedPgctl=0x00000010u;
inline constexpr ULONG kH15oExpectedCgctl=0x807b0dffu;
inline constexpr ULONG kH15oAppliedPgctl=0x00000014u;
inline constexpr ULONG kH15oAppliedCgctl=0x807b0dfdu;
inline constexpr ULONG kH15oExpectedEm2=0x04007000u;
inline constexpr ULONG kH15oAppliedEm2=0x04005000u;

struct H15oObservation {
    USHORT hdaGcap; UCHAR hdaVmin; UCHAR hdaVmaj; ULONG hdaGctl;
    UCHAR hdaCorbctl; UCHAR hdaRirbctl; UCHAR totalStreams; UCHAR reserved0;
    ULONG streamRunMask; ULONG hdaIntelEm2; ULONG dspAdspcs; ULONG dspAdspic;
    ULONG dspAdspis; ULONG dspHipci; ULONG dspHipcie; ULONG dspHipcctl; ULONG dspRomStatus;
};
static_assert(sizeof(H15oObservation)==48u,"H15O observation ABI");

enum H15oFlags : ULONG {
    H15oAttestationValid=1u<<0,
    H15oExpectedBaselineMatch=1u<<1,
    H15oBeforeCaptured=1u<<2,
    H15oHdaTransportIdle=1u<<3,
    H15oCrstSetWritten=1u<<4,
    H15oCrstReadyObserved=1u<<5,
    H15oReadyCaptured=1u<<6,
    H15oPpExactDiscovered=1u<<7,
    H15oPpctlZeroBaseline=1u<<8,
    H15oGprocenSetWritten=1u<<9,
    H15oGprocenSetObserved=1u<<10,
    H15oGprocenCaptured=1u<<11,
    H15oCgClearWritten=1u<<12,
    H15oCgExact=1u<<13,
    H15oCgCaptured=1u<<14,
    H15oEm2ClearWritten=1u<<15,
    H15oEm2Exact=1u<<16,
    H15oEm2Captured=1u<<17,
    H15oPgSetWritten=1u<<18,
    H15oPgExact=1u<<19,
    H15oPgCaptured=1u<<20,
    H15oCpa0ObservedAny=1u<<21,
    H15oCgRestoreExact=1u<<22,
    H15oEm2RestoreExact=1u<<23,
    H15oPgRestoreExact=1u<<24,
    H15oFullPciRestoreExact=1u<<25,
    H15oGprocenClearExact=1u<<26,
    H15oCrstClearExact=1u<<27,
    H15oFinalCaptured=1u<<28,
    H15oCoreRegsRestored=1u<<29,
    H15oNoDspMmioWrite=1u<<30,
    H15oNoDmaIrqFirmwarePlayback=1u<<31
};
inline constexpr ULONG kH15oRequiredFlags=0xffdfffffu;

struct H15oRequestV1 { ULONG version; ULONG size; ULONG expectedPgctl; ULONG expectedCgctl; };
static_assert(sizeof(H15oRequestV1)==16u,"H15O request ABI");

struct H15oResultV1 {
    ULONG version; ULONG size; LONG transactionStatus; ULONG flags; ULONG generation;
    USHORT vendorId; USHORT deviceId; UCHAR headerType; UCHAR firstCapability;
    UCHAR capabilityCount; UCHAR reserved0; ULONG pgctl; ULONG cgctl;
    ULONGLONG hdaPhysical; ULONGLONG dspPhysical; ULONG hdaLength; ULONG dspLength;
    H15oObservation before;
    H15oObservation ready;
    H15oObservation gprocenOnly;
    H15oObservation cgOff;
    H15oObservation em2Off;
    H15oObservation pgOn;
    H15oObservation finalState;
    ULONG llch; ULONG ppOffset; ULONG ppHeader;
    ULONG ppctlBefore; ULONG ppstsBefore; ULONG ppctlEnabled; ULONG ppstsEnabled;
    ULONG ppctlRestored; ULONG ppstsRestored;
    ULONG pgAfterCg; ULONG cgAfterCg;
    ULONG pgAfterPg; ULONG cgAfterPg;
    ULONG pgRestored; ULONG cgRestored;
    ULONG cpaStageMask;
};
static_assert(sizeof(H15oResultV1)==464u,"H15O result ABI");

struct H15oDeviceContext {
    alignas(HardwareAccessGate) UCHAR gateStorage[sizeof(HardwareAccessGate)];
    BOOLEAN gateConstructed; UCHAR reserved1[7];
    UCHAR* hda; UCHAR* dsp; ULONG hdaLength; ULONG dspLength;
    ULONGLONG hdaPhysical; ULONGLONG dspPhysical;
    volatile LONG ready; volatile LONG d0; volatile LONG consumed; volatile LONG generation;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(H15oDeviceContext,H15oGetContext)

EVT_WDF_DRIVER_DEVICE_ADD H15oEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE H15oEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE H15oEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY H15oEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT H15oEvtD0Exit;
EVT_WDF_DEVICE_SURPRISE_REMOVAL H15oEvtSurpriseRemoval;
EVT_WDF_OBJECT_CONTEXT_CLEANUP H15oEvtCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL H15oEvtIoDeviceControl;
extern const GUID kH15oInterfaceGuid;

} }
