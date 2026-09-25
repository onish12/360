// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>

namespace phaser360 { namespace windows {

inline constexpr DEVICE_TYPE kH15orDeviceType=0x8343u;
inline constexpr ULONG kH15orIoctlFunction=0x923u;
inline constexpr ULONG IOCTL_PHASER360_H15OR_SNAPSHOT=
    CTL_CODE(kH15orDeviceType,kH15orIoctlFunction,METHOD_BUFFERED,
             FILE_READ_ACCESS|FILE_WRITE_ACCESS);
static_assert(IOCTL_PHASER360_H15OR_SNAPSHOT==0x8343e48cu,"H15OR IOCTL ABI");

struct H15orRequestV1 {
    ULONG version;
    ULONG size;
    ULONG busNumber;
    ULONG slotNumber;
};
static_assert(sizeof(H15orRequestV1)==16u,"H15OR request ABI");

struct H15orObservation {
    USHORT hdaGcap;
    UCHAR hdaVmin;
    UCHAR hdaVmaj;
    ULONG hdaGctl;
    UCHAR hdaCorbctl;
    UCHAR hdaRirbctl;
    UCHAR totalStreams;
    UCHAR reserved0;
    ULONG streamRunMask;
    ULONG hdaIntelEm2;
    ULONG hdaPpctl;
    ULONG hdaPpsts;
    ULONG dspAdspcs;
    ULONG dspAdspic;
    ULONG dspAdspis;
    ULONG dspHipci;
    ULONG dspHipcie;
    ULONG dspHipcctl;
    ULONG dspRomStatus;
};
static_assert(sizeof(H15orObservation)==56u,"H15OR observation ABI");

enum H15orFlags : ULONG {
    H15orPciRead=1u<<0,
    H15orPciIdentityExact=1u<<1,
    H15orMmioMapped=1u<<2,
    H15orObservationCaptured=1u<<3,
    H15orPciPolicyBaseline=1u<<4,
    H15orHdaWritableBaseline=1u<<5,
    H15orDspStableReadable=1u<<6,
    H15orPpctlZero=1u<<7,
    H15orHdaTransportIdle=1u<<8,
    H15orExactSafeForHandoff=1u<<9
};

struct H15orResultV1 {
    ULONG version;
    ULONG size;
    LONG status;
    ULONG flags;
    ULONG busNumber;
    ULONG slotNumber;
    ULONG pciBytesRead;
    USHORT vendorId;
    USHORT deviceId;
    ULONG pgctl;
    ULONG cgctl;
    ULONGLONG hdaPhysical;
    ULONGLONG dspPhysical;
    ULONG hdaLength;
    ULONG dspLength;
    H15orObservation obs;
};
static_assert(sizeof(H15orResultV1)==120u,"H15OR result ABI");

} }
