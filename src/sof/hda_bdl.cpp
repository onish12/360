// SPDX-License-Identifier: MIT
#include "hda_bdl.h"
namespace phaser360 { namespace sof {
namespace {
void Put32(uint8_t* p, uint32_t v) noexcept {
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<uint8_t>(v >> (i * 8));
}
}
bool BuildBootBdl(uint64_t logical, size_t bytes, bool address64,
                  uint8_t* output, size_t capacity, uint16_t* entries) noexcept {
    if (!entries) return false;
    *entries = 0;
    if (!output || capacity < kBdlBytes || bytes == 0 || bytes > kMaxDmaBytes ||
        (logical & (kDmaPageBytes - 1)) != 0) return false;
    const uint64_t lastDelta = static_cast<uint64_t>(bytes - 1);
    if (logical > UINT64_MAX - lastDelta) return false;
    if (!address64 && logical + lastDelta > UINT32_MAX) return false;
    const size_t count = (bytes + kDmaPageBytes - 1) / kDmaPageBytes;
    for (size_t i = 0; i < kBdlBytes; ++i) output[i] = 0;
    size_t remaining = bytes;
    for (size_t i = 0; i < count; ++i) {
        const uint64_t address = logical + i * kDmaPageBytes;
        const size_t length = remaining < kDmaPageBytes ? remaining : kDmaPageBytes;
        Put32(output + i * 16, static_cast<uint32_t>(address));
        Put32(output + i * 16 + 4, static_cast<uint32_t>(address >> 32));
        Put32(output + i * 16 + 8, static_cast<uint32_t>(length));
        remaining -= length;
    }
    *entries = static_cast<uint16_t>(count);
    return true;
}
} }
