// SPDX-License-Identifier: MIT
#include "ipc3_ready.h"
namespace phaser360 { namespace sof {
namespace {
uint32_t U32(const uint8_t* p) noexcept {
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 |
           uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}
uint16_t U16(const uint8_t* p) noexcept {
    return static_cast<uint16_t>(uint16_t(p[0]) | uint16_t(p[1]) << 8);
}
}
ReadyStatus ParseIpc3Ready(const uint8_t* b, size_t size, uint16_t max_minor,
                          ReadyInfo* out) noexcept {
    if (!out) return ReadyStatus::InvalidArgument;
    *out = {};
    if (!b || max_minor > 0xfffu) return ReadyStatus::InvalidArgument;
    if (size != kIpc3ReadyBytes) return ReadyStatus::Size;
    if (U32(b) != kIpc3ReadyBytes) return ReadyStatus::Size;
    if (U32(b + 4) != kIpc3ReadyCommand) return ReadyStatus::Command;
    if (U32(b + 24) != 60) return ReadyStatus::VersionSize;
    const uint32_t abi = U32(b + 64);
    if ((abi >> 24) != 3 || ((abi >> 12) & 0xfffu) > max_minor)
        return ReadyStatus::Abi;
    for (size_t i = 72; i < 84; i += 4)
        if (U32(b + i)) return ReadyStatus::Reserved;
    for (size_t i = 92; i < 108; i += 4)
        if (U32(b + i)) return ReadyStatus::Reserved;
    *out = {U16(b + 28), U16(b + 30), U16(b + 32), U16(b + 34),
            abi, U32(b + 68), U32(b + 8), U32(b + 12), U32(b + 16), U32(b + 20),
            uint64_t(U32(b + 84)) | uint64_t(U32(b + 88)) << 32};
    return ReadyStatus::Ok;
}
}}
