// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include "../driver/hardware_access_gate.h"
#include "../driver/pci_config_attestation.h"

namespace phaser360 { namespace windows {

inline constexpr DEVICE_TYPE kH15mDeviceType=0x8340u;
inline constexpr ULONG kH15mIoctlFunction=0x920u;
inline constexpr ULONG IOCTL_PHASER360_H15M_TRANSACTION=
    CTL_CODE(kH15mDeviceType,kH15mIoctlFunction,METHOD_BUFFERED,
             FILE_READ_ACCESS|FILE_WRITE_ACCESS);
static_assert(IOCTL_PHASER360_H15M_TRANSACTION==0x8340e480u,"H15M IOCTL ABI");

inline constexpr ULONG kH15mHdaBytes=0x4000u;
inline constexpr ULONG kH15mDspBytes=0x100000u;
inline constexpr ULONG kH15mExpectedPgctl=0x00000010u;
inline constexpr ULONG kH15mExpectedCgctl=0x807b0dffu;
inline constexpr ULONG kH15mMaxCaps=12u;

struct H15mObservation {
    USHORT hdaGcap; UCHAR hdaVmin; UCHAR hdaVmaj; ULONG hdaGctl;
    UCHAR hdaCorbctl; UCHAR hdaRirbctl; UCHAR totalStreams; UCHAR reserved0;
    ULONG streamRunMask; ULONG hdaIntelEm2; ULONG dspAdspcs; ULONG dspAdspic;
    ULONG dspAdspis; ULONG dspHipci; ULONG dspHipcie; ULONG dspHipcctl; ULONG dspRomStatus;
};
static_assert(sizeof(H15mObservation)==48u,"H15M observation ABI");

struct H15mCapEntry { ULONG offset; ULONG header; };
static_assert(sizeof(H15mCapEntry)==8u,"H15M capability entry ABI");

enum H15mFlags : ULONG {
    H15mAttestationValid=1u<<0,
    H15mExpectedBaselineMatch=1u<<1,
    H15mBeforeCaptured=1u<<2,
    H15mHdaTransportIdle=1u<<3,
    H15mCrstSetWritten=1u<<4,
    H15mCrstReadyObserved=1u<<5,
    H15mReadyCaptured=1u<<6,
    H15mCapabilityChainCaptured=1u<<7,
    H15mPpCapabilityFound=1u<<8,
    H15mPpRegistersCaptured=1u<<9,
    H15mCrstClearWritten=1u<<10,
    H15mCrstRestoredObserved=1u<<11,
    H15mRestoredCaptured=1u<<12,
    H15mGctlRestoredExact=1u<<13,
    H15mNoPciWrite=1u<<14,
    H15mNoDspMmioWrite=1u<<15,
    H15mNoPpctlWrite=1u<<16,
    H15mNoEm2Write=1u<<17,
    H15mNoDma=1u<<18,
    H15mNoIrqOwnership=1u<<19,
    H15mNoFirmware=1u<<20,
    H15mNoDspBoot=1u<<21,
    H15mNoPlayback=1u<<22,
    H15mOneShot=1u<<23,
    H15mSplitMappings=1u<<24,
    H15mCapabilityWalkBounded=1u<<25,
    H15mCore0StateReadOnly=1u<<26
};
inline constexpr ULONG kH15mRequiredFlags=0x07fffcffu;

struct H15mRequestV1 { ULONG version; ULONG size; ULONG expectedPgctl; ULONG expectedCgctl; };
static_assert(sizeof(H15mRequestV1)==16u,"H15M request ABI");

struct H15mResultV1 {
    ULONG version; ULONG size; LONG transactionStatus; ULONG flags; ULONG generation;
    USHORT vendorId; USHORT deviceId; UCHAR headerType; UCHAR firstCapability;
    UCHAR capabilityCount; UCHAR reserved0; ULONG pgctl; ULONG cgctl;
    ULONGLONG hdaPhysical; ULONGLONG dspPhysical; ULONG hdaLength; ULONG dspLength;
    H15mObservation before; H15mObservation ready; H15mObservation restored;
    ULONG llch; ULONG ppOffset; ULONG ppHeader; ULONG ppctl; ULONG ppsts; ULONG hdaCapCount;
    H15mCapEntry caps[kH15mMaxCaps];
};
static_assert(sizeof(H15mResultV1)==328u,"H15M result ABI");

struct H15mDeviceContext {
    alignas(HardwareAccessGate) UCHAR gateStorage[sizeof(HardwareAccessGate)];
    BOOLEAN gateConstructed; UCHAR reserved1[7];
    UCHAR* hda; UCHAR* dsp; ULONG hdaLength; ULONG dspLength;
    ULONGLONG hdaPhysical; ULONGLONG dspPhysical;
    volatile LONG ready; volatile LONG d0; volatile LONG consumed; volatile LONG generation;
};
WDF_DECLARE_CONTEXT_TYPE_WITH_NAME(H15mDeviceContext,H15mGetContext)

EVT_WDF_DRIVER_DEVICE_ADD H15mEvtDeviceAdd;
EVT_WDF_DEVICE_PREPARE_HARDWARE H15mEvtPrepareHardware;
EVT_WDF_DEVICE_RELEASE_HARDWARE H15mEvtReleaseHardware;
EVT_WDF_DEVICE_D0_ENTRY H15mEvtD0Entry;
EVT_WDF_DEVICE_D0_EXIT H15mEvtD0Exit;
EVT_WDF_DEVICE_SURPRISE_REMOVAL H15mEvtSurpriseRemoval;
EVT_WDF_OBJECT_CONTEXT_CLEANUP H15mEvtCleanup;
EVT_WDF_IO_QUEUE_IO_DEVICE_CONTROL H15mEvtIoDeviceControl;
extern const GUID kH15mInterfaceGuid;

} }
