// SPDX-License-Identifier: MIT
#include "boot_dma.h"
#include "../../src/sof/hda_bdl.h"

namespace phaser360 { namespace windows {
void BootDma::FreeUnpublished() noexcept {
    NT_ASSERT(!published_);
    if (bdl_) { WdfObjectDelete(bdl_); bdl_ = nullptr; }
    if (payload_) { WdfObjectDelete(payload_); payload_ = nullptr; }
    if (enabler_) { WdfObjectDelete(enabler_); enabler_ = nullptr; }
    view_ = {};
}
NTSTATUS BootDma::Prepare(WDFDEVICE device, const UCHAR* approvedPayload,
                          SIZE_T bytes) noexcept {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if (enabler_ || payload_ || bdl_ || published_) return STATUS_INVALID_DEVICE_STATE;
    if (!device || !approvedPayload || bytes == 0 || bytes > sof::kMaxDmaBytes)
        return STATUS_INVALID_PARAMETER;
    // Deliberately use 32-bit DMA: no assumption about live GCAP or IOMMU.
    WDF_DMA_ENABLER_CONFIG dma;
    WDF_DMA_ENABLER_CONFIG_INIT(&dma, WdfDmaProfileScatterGather, sof::kMaxDmaBytes);
    NTSTATUS status = WdfDmaEnablerCreate(device, &dma, WDF_NO_OBJECT_ATTRIBUTES, &enabler_);
    if (!NT_SUCCESS(status)) return status;
    WDF_COMMON_BUFFER_CONFIG buffer;
    WDF_COMMON_BUFFER_CONFIG_INIT(&buffer, 4095);
    status = WdfCommonBufferCreateWithConfig(enabler_, bytes, &buffer,
                                            WDF_NO_OBJECT_ATTRIBUTES, &payload_);
    if (!NT_SUCCESS(status)) { FreeUnpublished(); return status; }
    status = WdfCommonBufferCreateWithConfig(enabler_, sof::kBdlBytes, &buffer,
                                            WDF_NO_OBJECT_ATTRIBUTES, &bdl_);
    if (!NT_SUCCESS(status)) { FreeUnpublished(); return status; }
    const auto payloadLogical = WdfCommonBufferGetAlignedLogicalAddress(payload_);
    const auto bdlLogical = WdfCommonBufferGetAlignedLogicalAddress(bdl_);
    auto* payloadVirtual = WdfCommonBufferGetAlignedVirtualAddress(payload_);
    auto* bdlVirtual = static_cast<UCHAR*>(WdfCommonBufferGetAlignedVirtualAddress(bdl_));
    const auto bdlAddress = static_cast<ULONGLONG>(bdlLogical.QuadPart);
    USHORT count = 0;
    if (!payloadVirtual || !bdlVirtual || (bdlAddress & 4095) != 0 ||
        bdlAddress > MAXULONG - (sof::kBdlBytes - 1) ||
        !sof::BuildBootBdl(static_cast<ULONGLONG>(payloadLogical.QuadPart), bytes,
                           false, bdlVirtual, sof::kBdlBytes, &count)) {
        FreeUnpublished(); return STATUS_DEVICE_CONFIGURATION_ERROR;
    }
    RtlCopyMemory(payloadVirtual, approvedPayload, bytes);
    view_.bdlLogical = bdlAddress;
    view_.payloadBytes = static_cast<ULONG>(bytes);
    view_.lastValidIndex = static_cast<USHORT>(count - 1);
    return STATUS_SUCCESS;
}
NTSTATUS BootDma::Publish(BootDmaView* view) noexcept {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if (!view) return STATUS_INVALID_PARAMETER;
    *view = {};
    if (!enabler_ || !payload_ || !bdl_ || published_) return STATUS_INVALID_DEVICE_STATE;
    KeMemoryBarrier();
    published_ = true; // Protect even a partially successful register setup.
    *view = view_;
    return STATUS_SUCCESS;
}
NTSTATUS BootDma::Release(bool (*verifyQuiesced)(void*), void* context) noexcept {
    if (KeGetCurrentIrql() != PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if (published_) {
        if (!verifyQuiesced || !verifyQuiesced(context)) return STATUS_DEVICE_BUSY;
        KeMemoryBarrier();
        published_ = false;
    }
    FreeUnpublished();
    return STATUS_SUCCESS;
}
} }
