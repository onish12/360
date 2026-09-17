// SPDX-License-Identifier: MIT
#pragma once
#include <stddef.h>
#include <stdint.h>

namespace phaser360 { namespace sof {
constexpr size_t kBdlBytes = 4096;
constexpr size_t kDmaPageBytes = 4096;
constexpr size_t kMaxDmaBytes = 256 * kDmaPageBytes;
// Policy: page-aligned common buffer; <=256 entries, no entry crosses 4 KiB.
// Writes little-endian bytes, no IOC (boot completion is not a period interrupt).
// On failure, output bytes are untouched and entries is zero.
// output and entries must not overlap; caller supplies a valid output extent.
bool BuildBootBdl(uint64_t logical, size_t bytes, bool address64,
                  uint8_t* output, size_t capacity, uint16_t* entries) noexcept;
} }
