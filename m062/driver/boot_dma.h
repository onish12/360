// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>

namespace phaser360 { namespace windows {

struct BootDmaView {
    ULONGLONG bdlLogical;
    ULONG payloadBytes;
    USHORT lastValidIndex;
};

// PrepareHardware/ReleaseHardware lifetime DMA owner.
//
// The WDF DMA enabler and both common buffers are created while KMDF is in
// EvtDevicePrepareHardware and remain valid until EvtDeviceReleaseHardware.
// D0 sessions only stage bytes into these already-created buffers, publish the
// preallocated BDL, and retire that publication after verified stream detach.
class BootDma final {
public:
    BootDma() noexcept = default;
    BootDma(const BootDma&) = delete;
    BootDma& operator=(const BootDma&) = delete;

    _IRQL_requires_(PASSIVE_LEVEL)
    NTSTATUS PrepareHardware(WDFDEVICE device,SIZE_T payloadCapacity) noexcept;

    _IRQL_requires_(PASSIVE_LEVEL)
    NTSTATUS Stage(const UCHAR* approvedPayload,SIZE_T bytes) noexcept;

    // Call before the first hardware address publication.
    _IRQL_requires_(PASSIVE_LEVEL)
    NTSTATUS Publish(BootDmaView* view) noexcept;

    // If published, verifier must prove stream stopped AND addresses detached.
    // Resources remain allocated for a later D0 in the same prepared lifetime.
    _IRQL_requires_(PASSIVE_LEVEL)
    NTSTATUS ReleaseSession(bool (*verifyQuiesced)(void*),void* context) noexcept;

    // Normal PnP release only. Refuses to free a staged/published session.
    _IRQL_requires_(PASSIVE_LEVEL)
    NTSTATUS ReleaseHardware() noexcept;

    // Terminal surprise-removal only. Performs no hardware access and does not
    // free DMA memory. WDF retains the device-parented objects for framework
    // teardown; local handles are deliberately discarded.
    _IRQL_requires_(PASSIVE_LEVEL)
    bool AbandonForRemoval() noexcept;

    bool HardwarePrepared() const noexcept {
        return !abandoned_ && enabler_ && payload_ && bdl_ &&
            payloadVirtual_ && bdlVirtual_ && payloadCapacity_!=0;
    }
    bool SessionActive() const noexcept { return staged_ || published_; }

private:
    WDFDMAENABLER enabler_=nullptr;
    WDFCOMMONBUFFER payload_=nullptr;
    WDFCOMMONBUFFER bdl_=nullptr;
    UCHAR* payloadVirtual_=nullptr;
    UCHAR* bdlVirtual_=nullptr;
    ULONGLONG payloadLogical_=0;
    ULONGLONG bdlLogical_=0;
    SIZE_T payloadCapacity_=0;
    BootDmaView view_={};
    bool staged_=false;
    bool published_=false;
    bool abandoned_=false;

    void DeleteHardware() noexcept;
    void ResetSession() noexcept;
};

} }
