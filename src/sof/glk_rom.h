// SPDX-License-Identifier: MIT
#pragma once
#include "hda_bdl.h"
namespace phaser360 { namespace sof {
struct RomIo {
    void* context;
    bool (*read)(void*,uint32_t,uint32_t*);
    bool (*write)(void*,uint32_t,uint32_t);
    bool (*delay_us)(void*,unsigned);
    uint64_t (*now_us)(void*);
    uint32_t length;
};
enum class RomState { Unbound, Bound, Cold, Initializing, DownloadReady, Entered, Fault };
enum class RomError { None, Argument, State, Io, Timeout, Clock, Halted, Precondition };
class GlkRom final {
public:
    GlkRom() noexcept = default;
    GlkRom(const GlkRom&) = delete;
    GlkRom& operator=(const GlkRom&) = delete;
    bool Bind(const RomIo&) noexcept; // no writes; caller owns exact GLK device
    bool PowerDown() noexcept; // caller MUST stop HDA DMA first
    bool Initialize(uint8_t streamTag) noexcept; // Cold only, no automatic retries
    bool WaitEntered() noexcept; // caller starts prepared HDA stream first
    RomState State() const noexcept { return state_; }
    RomError Error() const noexcept { return error_; }
    uint32_t LastValue() const noexcept { return last_; }
private:
    RomIo io_={};
    RomState state_=RomState::Unbound;
    RomError error_=RomError::None;
    uint32_t last_=0;
    bool Fail(RomError) noexcept;
    bool Read(uint32_t,uint32_t&) noexcept;
    bool Write(uint32_t,uint32_t) noexcept;
    bool Update(uint32_t,uint32_t,uint32_t) noexcept;
    bool Poll(uint32_t,uint32_t,uint32_t,uint32_t,bool=false) noexcept;
    bool DownCores(uint32_t) noexcept;
};
} }
