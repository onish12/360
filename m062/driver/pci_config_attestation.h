// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>

namespace phaser360 { namespace windows {

inline constexpr ULONG kPciConfigSnapshotBytes=256u;

struct PciConfigSnapshot {
    USHORT vendorId=0;
    USHORT deviceId=0;
    UCHAR headerType=0;
    UCHAR firstCapability=0;
    UCHAR capabilityCount=0;
    UCHAR reserved=0;
    ULONG pgctl=0;
    ULONG cgctl=0;
    UCHAR config[kPciConfigSnapshotBytes]={};
};

class PciConfigAttestation final {
public:
    PciConfigAttestation() noexcept = default;
    PciConfigAttestation(const PciConfigAttestation&)=delete;
    PciConfigAttestation& operator=(const PciConfigAttestation&)=delete;

    // Read-only PCI config-space attestation. No SetBusData call is permitted.
    // The accepted target is Intel 8086:3198, type-0 header, with every
    // conventional capability at >= 0x50. This proves 0x44/0x48 are outside
    // the OS-owned header/capability list on this device before later stages
    // consider any vendor-defined write.
    NTSTATUS Capture(WDFDEVICE device) noexcept;
    bool Valid() const noexcept { return valid_; }
    const PciConfigSnapshot& Snapshot() const noexcept { return snapshot_; }

private:
    PciConfigSnapshot snapshot_{};
    bool attempted_=false;
    bool valid_=false;

    static USHORT Load16(const UCHAR*) noexcept;
    static ULONG Load32(const UCHAR*) noexcept;
    static bool ValidateCapabilityChain(
        const UCHAR* config,SIZE_T bytes,UCHAR first,UCHAR* count) noexcept;
};

} }
