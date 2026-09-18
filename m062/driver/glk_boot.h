// SPDX-License-Identifier: MIT
#pragma once
#include "hda_transport.h"
#include "../../src/sof/glk_rom.h"
namespace phaser360 { namespace windows {
struct TransferResult {
    bool started=false, firmwareEntered=false, dmaReleased=false;
    sof::RomError romError=sof::RomError::None;
};
// Exact GLK device ownership and both translated BAR mappings supplied by the
// future PnP/power driver. Caller authenticates payload before any operation.
// Serialized PASSIVE_LEVEL calls, single attempt, explicit Shutdown mandatory.
class GlkBoot final {
public:
    NTSTATUS Prepare(WDFDEVICE,UCHAR* hda,ULONG hdaLength,UCHAR* dsp,ULONG dspLength,
                     const UCHAR* approvedPayload,SIZE_T bytes) noexcept;
    TransferResult Transfer() noexcept;
    bool Shutdown() noexcept; // preserves DMA if stop fails; no DSP writes then
    sof::RomError RomError() const noexcept { return primaryError_; }
private:
    HdaTransport hda_;
    sof::GlkRom rom_;
    UCHAR* dsp_=nullptr;
    ULONG length_=0;
    bool attempted_=false, dspTouched_=false, prepared_=false;
    sof::RomError primaryError_=sof::RomError::None;
    static bool Read(void*,ULONG,ULONG*) noexcept;
    static bool Write(void*,ULONG,ULONG) noexcept;
    static bool Delay(void*,unsigned) noexcept;
    static ULONGLONG Now(void*) noexcept;
    bool Valid(ULONG) const noexcept;
};
} }
