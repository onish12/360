// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>
#include "hardware_access_gate.h"

namespace phaser360 { namespace windows {

struct PnpInterruptResource {
    PCM_PARTIAL_RESOURCE_DESCRIPTOR raw=nullptr;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR translated=nullptr;
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

    NTSTATUS Prepare(WDFCMRESLIST raw,WDFCMRESLIST translated) noexcept;
    NTSTATUS Release() noexcept;
    static NTSTATUS PrepareHardware(WDFDEVICE,WDFCMRESLIST,WDFCMRESLIST);
    static NTSTATUS ReleaseHardware(WDFDEVICE,WDFCMRESLIST);
    static void SurpriseRemoval(WDFDEVICE);
};

} }
