// SPDX-License-Identifier: MIT
#pragma once
#include "pinned_firmware.h"

namespace phaser360 { namespace windows {

// Kernel-owned immutable source view. Providers must not allocate, perform file
// I/O or return user-mode memory. The consumer copies synchronously and does not
// retain this pointer.
struct FirmwareSourceView {
    const UCHAR* data=nullptr;
    SIZE_T bytes=0;
};

using FirmwareSourceProvider=bool(*)(FirmwareSourceView*) noexcept;

// Source-independent staging contract. PinnedFirmware still hashes its owned
// copy, so a provider cannot authorize bytes merely by claiming identity.
NTSTATUS StagePinnedFirmwareFromSource(
    PinnedFirmware&,WDFDEVICE,FirmwareSourceProvider) noexcept;

// Implemented only by a generated translation unit produced from the exact
// build-time-pinned SOF fixture. It is intentionally absent from the repository.
bool GetEmbeddedFirmwareSource(FirmwareSourceView*) noexcept;

} }
