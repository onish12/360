// SPDX-License-Identifier: MIT
#pragma once
#include "cold_power.h"
#include "firmware_pin.h"
namespace phaser360 { namespace windows {
// Serialized PASSIVE calls. No user pointers. Release before device destruction.
// Owns an immutable snapshot exposed only through the synchronous Enter path.
class PinnedFirmware final {
public:
    PinnedFirmware() noexcept = default;
    PinnedFirmware(const PinnedFirmware&)=delete;
    PinnedFirmware& operator=(const PinnedFirmware&)=delete;
    NTSTATUS Load(WDFDEVICE,const UCHAR* kernelBytes,SIZE_T bytes) noexcept;
    NTSTATUS Enter(ColdPower&,WDFDEVICE,UCHAR* hda,ULONG hdaLength,UCHAR* dsp,ULONG dspLength) noexcept;
    bool Release() noexcept;
private:
    WDFMEMORY memory_=nullptr;
    WDFDEVICE device_=nullptr;
    const UCHAR* image_=nullptr;
    bool entering_=false;
};
}}
