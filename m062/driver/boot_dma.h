// SPDX-License-Identifier: MIT
#pragma once
#include <ntddk.h>
#include <wdf.h>

namespace phaser360 { namespace windows {
// This component does not authenticate firmware, access MMIO or start DMA.
// All calls: serialized by the owning driver, PASSIVE_LEVEL, device alive.
// Must be constructed (or placement-constructed); never memcpy this owner.
struct BootDmaView {
    ULONGLONG bdlLogical;
    ULONG payloadBytes;
    USHORT lastValidIndex;
};
class BootDma final {
public:
    BootDma() noexcept = default;
    BootDma(const BootDma&) = delete;
    BootDma& operator=(const BootDma&) = delete;
    // No automatic WDF deletion: the driver must Release before destruction.
    // Device/enabler parent destruction ALSO requires prior DMA quiescence.
    _IRQL_requires_(PASSIVE_LEVEL)
    NTSTATUS Prepare(WDFDEVICE device, const UCHAR* approvedPayload, SIZE_T bytes) noexcept;
    // Call BEFORE the first hardware address publication, even if start fails.
    _IRQL_requires_(PASSIVE_LEVEL)
    NTSTATUS Publish(BootDmaView* view) noexcept;
    // If published, verifier must prove stream stopped AND addresses detached.
    // False/missing verifier retains ownership. No fallback free or retry.
    _IRQL_requires_(PASSIVE_LEVEL)
    NTSTATUS Release(bool (*verifyQuiesced)(void*), void* context) noexcept;
private:
    WDFDMAENABLER enabler_ = nullptr;
    WDFCOMMONBUFFER payload_ = nullptr;
    WDFCOMMONBUFFER bdl_ = nullptr;
    BootDmaView view_ = {};
    bool published_ = false;
    void FreeUnpublished() noexcept;
};
} }
