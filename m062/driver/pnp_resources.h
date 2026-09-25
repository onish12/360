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

// Short-lived software handoff from the prepared PnP owner to the dormant
// device-lifetime interrupt shell. Descriptor pointers remain borrowed from
// PrepareHardware and must never be retained past ReleaseHardware.
struct PnpDormantInterruptBinding {
    HardwareAccessGate* gate=nullptr;
    UCHAR* dsp=nullptr;
    ULONG dspLength=0;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR raw=nullptr;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR translated=nullptr;
    PnpInterruptKind kind=PnpInterruptKind::LineBased;
    USHORT messageCount=0;
};

// Optional composition seam used by the non-installable H4 owner. PnP remains
// the sole owner of mappings; hooks receive copied resource metadata only.
struct PnpLifecycleOps {
    void* context=nullptr;
    NTSTATUS(*prepared)(void*,const PnpResourceView&,const PnpDormantInterruptBinding&) noexcept=nullptr;
    NTSTATUS(*d0Entry)(void*,WDFDEVICE,const PnpResourceView&) noexcept=nullptr;
    NTSTATUS(*postInterruptsEnabled)(void*) noexcept=nullptr;
    NTSTATUS(*preInterruptsDisabled)(void*) noexcept=nullptr;
    NTSTATUS(*d0Exit)(void*) noexcept=nullptr;
    NTSTATUS(*release)(void*) noexcept=nullptr;
    void(*surpriseRemoval)(void*) noexcept=nullptr;
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
    // Before PrepareHardware only. The function table is copied; its context
    // must outlive this PnP owner.
    bool InstallLifecycle(const PnpLifecycleOps&) noexcept;

    bool Prepared() const noexcept {
        return gate_ && gate_->Allowed() && hda_!=nullptr && dsp_!=nullptr;
    }
    PnpPowerPhase PowerPhase() const noexcept { return phase_; }
    // Serialized PASSIVE caller only. Copied descriptor pointers are borrowed
    // from the PrepareHardware lists and expire at ReleaseHardware. Consumers
    // must independently honor the same HardwareAccessGate on every access.
    bool CopyPreparedView(PnpResourceView*) const noexcept;
    // M0.6.15G: copy the only interrupt pair that is admissible for this
    // DEV_3198 hardware contract. Does not create/enable a WDF interrupt.
    // LINE is accepted. MESSAGE is accepted only for exactly one raw message.
    bool CopySingleInterruptForCreate(PnpInterruptResource*) const noexcept;
    // M0.6.15H2: export one short-lived, software-only binding contract for the
    // dormant WDF interrupt shell. Does not call WdfInterruptCreate or MMIO.
    bool CopyDormantInterruptBinding(PnpDormantInterruptBinding*) const noexcept;

private:
    WDFDEVICE device_=nullptr;
    HardwareAccessGate* gate_;
    UCHAR* hda_=nullptr;
    UCHAR* dsp_=nullptr;
    PnpResourceView view_={};
    PnpLifecycleOps lifecycle_={};
    bool lifecyclePrepared_=false;
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
