// SPDX-License-Identifier: MIT
#include "boot_dma.h"
#include "../../src/sof/hda_bdl.h"

namespace phaser360 { namespace windows {

void BootDma::ResetSession() noexcept {
    if(payloadVirtual_ && payloadCapacity_)
        RtlZeroMemory(payloadVirtual_,payloadCapacity_);
    if(bdlVirtual_)
        RtlZeroMemory(bdlVirtual_,sof::kBdlBytes);
    view_={};
    staged_=false;
    published_=false;
}

void BootDma::DeleteHardware() noexcept {
    NT_ASSERT(!staged_ && !published_);
    if(bdl_) { WdfObjectDelete(bdl_); bdl_=nullptr; }
    if(payload_) { WdfObjectDelete(payload_); payload_=nullptr; }
    if(enabler_) { WdfObjectDelete(enabler_); enabler_=nullptr; }
    payloadVirtual_=nullptr;
    bdlVirtual_=nullptr;
    payloadLogical_=0;
    bdlLogical_=0;
    payloadCapacity_=0;
    view_={};
}

NTSTATUS BootDma::PrepareHardware(WDFDEVICE device,SIZE_T payloadCapacity) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if(abandoned_ || enabler_ || payload_ || bdl_ || staged_ || published_)
        return STATUS_INVALID_DEVICE_STATE;
    if(!device || payloadCapacity==0 || payloadCapacity>sof::kMaxDmaBytes)
        return STATUS_INVALID_PARAMETER;

    // KMDF requires the device alignment contract before WdfDmaEnablerCreate.
    // 4096-byte alignment is also the reviewed HDA BDL/common-buffer contract.
    WdfDeviceSetAlignmentRequirement(device,FILE_4096_BYTE_ALIGNMENT);

    WDF_DMA_ENABLER_CONFIG dma;
    WDF_DMA_ENABLER_CONFIG_INIT(
        &dma,WdfDmaProfileScatterGather,sof::kMaxDmaBytes);
    NTSTATUS status=WdfDmaEnablerCreate(
        device,&dma,WDF_NO_OBJECT_ATTRIBUTES,&enabler_);
    if(!NT_SUCCESS(status)) return status;

    WDF_COMMON_BUFFER_CONFIG buffer;
    WDF_COMMON_BUFFER_CONFIG_INIT(&buffer,FILE_4096_BYTE_ALIGNMENT);

    status=WdfCommonBufferCreateWithConfig(
        enabler_,payloadCapacity,&buffer,WDF_NO_OBJECT_ATTRIBUTES,&payload_);
    if(!NT_SUCCESS(status)) {
        DeleteHardware();
        return status;
    }

    status=WdfCommonBufferCreateWithConfig(
        enabler_,sof::kBdlBytes,&buffer,WDF_NO_OBJECT_ATTRIBUTES,&bdl_);
    if(!NT_SUCCESS(status)) {
        DeleteHardware();
        return status;
    }

    const auto payloadPa=WdfCommonBufferGetAlignedLogicalAddress(payload_);
    const auto bdlPa=WdfCommonBufferGetAlignedLogicalAddress(bdl_);
    payloadVirtual_=static_cast<UCHAR*>(
        WdfCommonBufferGetAlignedVirtualAddress(payload_));
    bdlVirtual_=static_cast<UCHAR*>(
        WdfCommonBufferGetAlignedVirtualAddress(bdl_));

    if(payloadPa.QuadPart<0 || bdlPa.QuadPart<0 ||
       !payloadVirtual_ || !bdlVirtual_) {
        DeleteHardware();
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    payloadLogical_=static_cast<ULONGLONG>(payloadPa.QuadPart);
    bdlLogical_=static_cast<ULONGLONG>(bdlPa.QuadPart);
    payloadCapacity_=payloadCapacity;

    const ULONGLONG max32=static_cast<ULONGLONG>(MAXULONG);
    if((payloadLogical_&FILE_4096_BYTE_ALIGNMENT)!=0 ||
       (bdlLogical_&FILE_4096_BYTE_ALIGNMENT)!=0 ||
       payloadLogical_>max32-static_cast<ULONGLONG>(payloadCapacity_-1) ||
       bdlLogical_>max32-static_cast<ULONGLONG>(sof::kBdlBytes-1)) {
        DeleteHardware();
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    RtlZeroMemory(payloadVirtual_,payloadCapacity_);
    RtlZeroMemory(bdlVirtual_,sof::kBdlBytes);
    return STATUS_SUCCESS;
}

NTSTATUS BootDma::Stage(const UCHAR* approvedPayload,SIZE_T bytes) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if(!HardwarePrepared() || staged_ || published_)
        return STATUS_INVALID_DEVICE_STATE;
    if(!approvedPayload || bytes==0 || bytes>payloadCapacity_)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(payloadVirtual_,payloadCapacity_);
    RtlZeroMemory(bdlVirtual_,sof::kBdlBytes);

    USHORT count=0;
    if(!sof::BuildBootBdl(
            payloadLogical_,bytes,false,
            bdlVirtual_,sof::kBdlBytes,&count) ||
       count==0) {
        ResetSession();
        return STATUS_DEVICE_CONFIGURATION_ERROR;
    }

    RtlCopyMemory(payloadVirtual_,approvedPayload,bytes);
    view_.bdlLogical=bdlLogical_;
    view_.payloadBytes=static_cast<ULONG>(bytes);
    view_.lastValidIndex=static_cast<USHORT>(count-1);
    staged_=true;
    return STATUS_SUCCESS;
}

NTSTATUS BootDma::Publish(BootDmaView* view) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if(!view) return STATUS_INVALID_PARAMETER;
    *view={};
    if(!HardwarePrepared() || !staged_ || published_)
        return STATUS_INVALID_DEVICE_STATE;

    KeMemoryBarrier();
    published_=true;
    *view=view_;
    return STATUS_SUCCESS;
}

NTSTATUS BootDma::ReleaseSession(
    bool (*verifyQuiesced)(void*),void* context) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if(abandoned_) return STATUS_SUCCESS;
    if(!HardwarePrepared()) {
        return (!staged_ && !published_)
            ? STATUS_SUCCESS : STATUS_INVALID_DEVICE_STATE;
    }

    if(published_) {
        if(!verifyQuiesced || !verifyQuiesced(context))
            return STATUS_DEVICE_BUSY;
        KeMemoryBarrier();
        published_=false;
    }

    ResetSession();
    return STATUS_SUCCESS;
}

NTSTATUS BootDma::ReleaseHardware() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return STATUS_INVALID_DEVICE_STATE;
    if(abandoned_) return STATUS_SUCCESS;
    if(staged_ || published_) return STATUS_DEVICE_BUSY;
    if(!enabler_ && !payload_ && !bdl_) return STATUS_SUCCESS;
    if(!HardwarePrepared()) return STATUS_INVALID_DEVICE_STATE;

    DeleteHardware();
    return STATUS_SUCCESS;
}

bool BootDma::AbandonForRemoval() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL) return false;

    // Do not call WdfObjectDelete and do not touch common-buffer memory here.
    // The device is terminally removed; framework parent teardown owns these
    // objects. Clearing local references prevents any later accidental use.
    enabler_=nullptr;
    payload_=nullptr;
    bdl_=nullptr;
    payloadVirtual_=nullptr;
    bdlVirtual_=nullptr;
    payloadLogical_=0;
    bdlLogical_=0;
    payloadCapacity_=0;
    view_={};
    staged_=false;
    published_=false;
    abandoned_=true;
    return true;
}

} }
