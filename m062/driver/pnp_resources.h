// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include "hardware_access_gate.h"

namespace phaser360 { namespace windows {

enum class PnpInterruptKind : UCHAR {
    LineBased=0,
    MessageSignaled
};

struct PnpInterruptResource {
    PCM_PARTIAL_RESOURCE_DESCRIPTOR raw=nullptr;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR translated=nullptr;
    PnpInterruptKind kind=PnpInterruptKind::LineBased;
    UCHAR rawShareDisposition=0;
    UCHAR translatedShareDisposition=0;
    USHORT rawFlags=0;
    USHORT translatedFlags=0;
    USHORT messageCount=0; // raw MSI/MSI-X descriptor only
    ULONG rawLevel=0;      // line-based only
    ULONG rawVector=0;
    ULONG_PTR rawAffinity=0;
    ULONG translatedLevel=0;
    ULONG translatedVector=0;
    ULONG_PTR translatedAffinity=0;
};

enum class PnpPowerPhase : UCHAR {
    NoResources=0,
    Prepared,
    D0Entered,
    Operational,
    PreInterruptsDisabled
};

struct PnpResourceView {
    static constexpr ULONG kMaxInterrupts=8;
    UCHAR* hda=nullptr;
    ULONG hdaLength=0;
    UCHAR* dsp=nullptr;
    ULONG dspLength=0;
    PnpInterruptResource interrupts[kMaxInterrupts]={};
    ULONG interruptCount=0;
};

// Resource-lifetime PnP adapter. Still not a boot/audio driver.
//
// PrepareHardware validates raw/translated pairing, maps the two measured GLK
// memory resources, records (but does not select) assigned interrupt descriptor
// pairs, and opens the shared HardwareAccessGate only after full preparation.
// ReleaseHardware closes the gate before normal unmapping; a terminal Removed
// gate is already closed to consumers. This class has no DMA/boot/IRQ consumer.
class PnpResources final {
public:
    explicit PnpResources(HardwareAccessGate& gate) noexcept : gate_(&gate) {}
    PnpResources()=delete;
    PnpResources(const PnpResources&)=delete;
    PnpResources& operator=(const PnpResources&)=delete;

    // Before WdfDeviceCreate. Resets attributes and reserves primary context.
    static NTSTATUS Configure(PWDFDEVICE_INIT,WDF_OBJECT_ATTRIBUTES*) noexcept;
    // After WdfDeviceCreate using those attributes, before DeviceAdd returns.
    NTSTATUS Attach(WDFDEVICE) noexcept;

    bool Prepared() const noexcept {
        return gate_ && gate_->Allowed() && hda_!=nullptr && dsp_!=nullptr;
    }
    PnpPowerPhase PowerPhase() const noexcept { return phase_; }
    // Serialized PASSIVE caller only. Copied descriptor pointers are borrowed
    // from the PrepareHardware lists and expire at ReleaseHardware. Consumers
    // must independently honor the same HardwareAccessGate on every access.
    bool CopyPreparedView(PnpResourceView*) const noexcept;

private:
    WDFDEVICE device_=nullptr;
    HardwareAccessGate* gate_;
    UCHAR* hda_=nullptr;
    UCHAR* dsp_=nullptr;
    PnpResourceView view_={};
    PnpPowerPhase phase_=PnpPowerPhase::NoResources;

    NTSTATUS Prepare(WDFCMRESLIST raw,WDFCMRESLIST translated) noexcept;
    NTSTATUS Release() noexcept;
    static NTSTATUS PrepareHardware(WDFDEVICE,WDFCMRESLIST,WDFCMRESLIST);
    static NTSTATUS ReleaseHardware(WDFDEVICE,WDFCMRESLIST);
    static void SurpriseRemoval(WDFDEVICE);
    static NTSTATUS D0Entry(WDFDEVICE,WDF_POWER_DEVICE_STATE);
    static NTSTATUS D0EntryPostInterruptsEnabled(WDFDEVICE,WDF_POWER_DEVICE_STATE);
    static NTSTATUS D0ExitPreInterruptsDisabled(WDFDEVICE,WDF_POWER_DEVICE_STATE);
    static NTSTATUS D0Exit(WDFDEVICE,WDF_POWER_DEVICE_STATE);
};

} }
