// SPDX-License-Identifier: MIT
#include "pinned_firmware.h"
namespace phaser360 { namespace windows {
NTSTATUS PinnedFirmware::Load(WDFDEVICE device,const UCHAR* input,SIZE_T bytes) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || memory_ || entering_) return STATUS_INVALID_DEVICE_STATE;
    if(!device || !input || bytes!=kPinnedImageBytes) return STATUS_INVALID_PARAMETER;
    WDF_OBJECT_ATTRIBUTES attributes;
    WDF_OBJECT_ATTRIBUTES_INIT(&attributes); attributes.ParentObject=device;
    void* snapshot=nullptr;
    const auto status=WdfMemoryCreate(&attributes,NonPagedPoolNx,0x46534850u,bytes,&memory_,&snapshot);
    if(!NT_SUCCESS(status)) return status;
    RtlCopyMemory(snapshot,input,bytes);
    // Hash the owned copy, never authorize a different mutable caller buffer.
    if(!MatchesFirmwarePin(static_cast<const UCHAR*>(snapshot),kPinnedImageBytes)) {
        WdfObjectDelete(memory_); memory_=nullptr; return STATUS_INVALID_IMAGE_HASH;
    }
    image_=static_cast<const UCHAR*>(snapshot); device_=device; return STATUS_SUCCESS;
}
NTSTATUS PinnedFirmware::Enter(ColdPower& power,WDFDEVICE device,UCHAR* hda,ULONG hdaLength,
                               UCHAR* dsp,ULONG dspLength) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !image_ || entering_ || device!=device_)
        return STATUS_INVALID_DEVICE_STATE;
    entering_=true;
    // Fixed offsets are bound by the whole-image pin. No caller-selected split/ABI.
    const auto status=power.Enter(device,hda,hdaLength,dsp,dspLength,
        image_+kPinnedXmanBytes,kPinnedPayloadBytes,image_,kPinnedXmanBytes,20);
    entering_=false; return status;
}
bool PinnedFirmware::Release() noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || entering_) return false;
    image_=nullptr; device_=nullptr;
    if(memory_) { WdfObjectDelete(memory_); memory_=nullptr; }
    return true;
}
}}
