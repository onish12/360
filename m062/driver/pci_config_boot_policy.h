// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include "hardware_access_gate.h"
#include "pci_config_attestation.h"

namespace phaser360 { namespace windows {

// H15D owns only the APL/GLK DSP power/clock-gating bits identified by SOF and gated by H15C.
// It never writes TCSEL[1:0], LSRMD, MISCBDCGE, the PCI common header,
// capability space, or any unrelated bit. Every write is read-modify-write,
// read back, and restored on normal/failed shutdown.
class PciConfigBootPolicy final {
public:
    PciConfigBootPolicy() noexcept = default;
    PciConfigBootPolicy(const PciConfigBootPolicy&)=delete;
    PciConfigBootPolicy& operator=(const PciConfigBootPolicy&)=delete;

    NTSTATUS Apply(WDFDEVICE,const PciConfigSnapshot&,HardwareAccessGate&) noexcept;
    bool Restore() noexcept;

    bool Applied() const noexcept { return applied_; }
    bool Dirty() const noexcept { return pgChanged_ || cgChanged_; }
    ULONG OriginalPgctl() const noexcept { return originalPgctl_; }
    ULONG OriginalCgctl() const noexcept { return originalCgctl_; }

    static constexpr ULONG PgctlOffset() noexcept { return kPgctlOffset; }
    static constexpr ULONG CgctlOffset() noexcept { return kCgctlOffset; }
    static constexpr ULONG PgctlOwnedMask() noexcept { return kPgctlAdspPgd; }
    static constexpr ULONG CgctlOwnedMask() noexcept { return kCgctlAdspDcge; }

private:
    static constexpr ULONG kPgctlOffset=0x44u;
    static constexpr ULONG kCgctlOffset=0x48u;
    static constexpr ULONG kPgctlAdspPgd=1u<<2;
    static constexpr ULONG kCgctlAdspDcge=1u<<1;

    WDFDEVICE device_=nullptr;
    HardwareAccessGate* gate_=nullptr;
    ULONG originalPgctl_=0;
    ULONG originalCgctl_=0;
    bool attempted_=false;
    bool applied_=false;
    bool pgChanged_=false;
    bool cgChanged_=false;

    static bool ReadDword(BUS_INTERFACE_STANDARD&,ULONG,ULONG*) noexcept;
    static bool WriteDword(BUS_INTERFACE_STANDARD&,ULONG,ULONG) noexcept;
    bool RestoreWithBus(BUS_INTERFACE_STANDARD&) noexcept;
};

} }
