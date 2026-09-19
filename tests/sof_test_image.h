// SPDX-License-Identifier: MIT
// Synthetic test image, never signed or approved for hardware.
#pragma once
#include <algorithm>
#include <cstring>
#include <vector>
namespace sof_test_image {
constexpr size_t ext = 24;
constexpr size_t cpd = ext;
constexpr size_t css = cpd + 88;
constexpr size_t meta = cpd + 0x3c0;
constexpr size_t adsp = cpd + 0x2000;
constexpr size_t module = adsp + 52;
constexpr size_t text_segment = module + 80;

void U32(std::vector<uint8_t>& b, size_t p, uint32_t n) {
    for (size_t i = 0; i < 4; ++i) b.at(p + i) = uint8_t(n >> (i * 8));
}
void Text(std::vector<uint8_t>& b, size_t p, const char* s) {
    std::copy(s, s + std::strlen(s), b.begin() + static_cast<std::ptrdiff_t>(p));
}
void Checksum(std::vector<uint8_t>& b) {
    b[cpd + 11] = 0;
    uint32_t sum = 0;
    for (size_t i = 0; i < 88; ++i) sum += b[cpd + i];
    b[cpd + 11] = uint8_t(0u - sum);
}

std::vector<uint8_t> Fixture() {
    // Synthetic, NOT signed or loadable. Mirrors the pinned legacy layout.
    std::vector<uint8_t> b(ext + 0xa000, 0);
    Text(b, 0, "XMan"); U32(b, 4, ext); U32(b, 8, 16); U32(b, 12, 0x01000000);
    U32(b, 16, 0x1234); U32(b, 20, 8); // Unknown but bounded XMan TLV.
    Text(b, cpd, "$CPD"); U32(b, cpd + 4, 3);
    b[cpd + 8] = 1; b[cpd + 9] = 1; b[cpd + 10] = 16; Text(b, cpd + 12, "ADSP");
    Text(b, cpd + 16, "ADSP.man"); U32(b, cpd + 28, 88); U32(b, cpd + 32, 644);
    Text(b, cpd + 40, "cavs0015.met"); U32(b, cpd + 52, 0x3c0); U32(b, cpd + 56, 96);
    Text(b, cpd + 64, "cavs0015"); U32(b, cpd + 76, 0x440); U32(b, cpd + 80, 0x9bc0);
    Checksum(b);
    U32(b, css, 4); U32(b, css + 4, 161); U32(b, css + 8, 0x10000);
    U32(b, css + 16, 0x8086); U32(b, css + 24, 161); Text(b, css + 28, "$MN2");
    U32(b, css + 120, 64); U32(b, css + 124, 1);
    U32(b, meta, 17); U32(b, meta + 4, 96); U32(b, meta + 72, 0x2000);
    Text(b, adsp, "$AM1"); U32(b, adsp + 4, 52); Text(b, adsp + 8, "ADSPFW");
    U32(b, adsp + 16, 10); U32(b, adsp + 28, 0x00090001);
    U32(b, adsp + 32, 0x00010000); U32(b, adsp + 36, 1); U32(b, adsp + 48, 0x2000);
    Text(b, module, "$AME"); Text(b, module + 4, "BASEFW");
    U32(b, text_segment, 0x1001f); U32(b, text_segment + 4, 0xb000a000);
    U32(b, text_segment + 8, 0x8000);
    U32(b, text_segment + 12, 0x1012f); U32(b, text_segment + 16, 0xb000b000);
    U32(b, text_segment + 20, 0x9000);
    // BSS may be larger than the file, because it has no file bytes.
    U32(b, text_segment + 24, 0x3b0202); U32(b, text_segment + 28, 0xbe044000);
    return b;
}

}
