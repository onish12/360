// SPDX-License-Identifier: MIT
#pragma once
#include "hda_bdl.h"
namespace phaser360 { namespace sof {
// Serialized, nonthrowing, bounded I/O callbacks. Width is in bytes.
struct RegisterIo {
    void* context;
    bool (*read)(void*, uint32_t, unsigned, uint32_t*);
    bool (*write)(void*, uint32_t, unsigned, uint32_t);
    void (*delay_us)(void*, unsigned);
    uint32_t length;
};
enum class StreamState { Empty, Selected, Prepared, Running, Fault, Detached };
class BootStream final {
public:
    BootStream() noexcept = default;
    BootStream(const BootStream&) = delete;
    BootStream& operator=(const BootStream&) = delete;
    // Exclusive cold controller, D0, GCTL/GPROCEN initialized, DMI L1 off.
    // Discovers first output stream and capabilities; no writes.
    bool Select(const RegisterIo& io) noexcept;
    // Owner MUST publish/pin DMA lifetime before calling, even on failure.
    bool Configure(uint64_t bdl, uint32_t bytes, uint16_t lvi) noexcept;
    // Caller must have initialized ROM and established platform power policy.
    bool Start() noexcept;
    bool StopDetach() noexcept;
    bool IsDetached() noexcept; // fresh readback
    uint8_t Tag() const noexcept { return 1; }
    StreamState State() const noexcept { return state_; }
private:
    RegisterIo io_ = {};
    StreamState state_ = StreamState::Empty;
    uint32_t sd_=0, pp_=0, spib_=0, index_=0;
    bool Read(uint32_t, unsigned, uint32_t&) noexcept;
    bool Write(uint32_t, unsigned, uint32_t) noexcept;
    bool Update(uint32_t, unsigned, uint32_t, uint32_t) noexcept;
    bool Match(uint32_t, unsigned, uint32_t, uint32_t) noexcept;
    bool Poll(uint32_t, uint32_t) noexcept;
};
} }
