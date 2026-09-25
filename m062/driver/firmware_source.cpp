// SPDX-License-Identifier: MIT
#include "firmware_source.h"

namespace phaser360 { namespace windows {

NTSTATUS StagePinnedFirmwareFromSource(
    PinnedFirmware& owner,WDFDEVICE device,FirmwareSourceProvider provider) noexcept {
    if(KeGetCurrentIrql()!=PASSIVE_LEVEL || !device || !provider || owner.Loaded())
        return STATUS_INVALID_DEVICE_STATE;

    FirmwareSourceView view{};
    if(!provider(&view)) return STATUS_INVALID_DEVICE_STATE;

    // Exact extent is part of the reviewed identity. PinnedFirmware::Load makes
    // a private nonpaged copy and performs the SHA-256 pin over that owned copy.
    if(!view.data || view.bytes!=kPinnedImageBytes)
        return STATUS_INVALID_IMAGE_HASH;

    const auto status=owner.Load(device,view.data,view.bytes);

    // No source pointer survives this synchronous handoff.
    view=FirmwareSourceView{};
    return status;
}

} }
