// SPDX-License-Identifier: MIT
#include "firmware_image.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

using namespace phaser360::sof;
namespace {
size_t checks = 0;
constexpr size_t ext = 24;
constexpr size_t cpd = ext;
constexpr size_t css = cpd + 88;
constexpr size_t meta = cpd + 0x3c0;
constexpr size_t adsp = cpd + 0x2000;
constexpr size_t module = adsp + 52;
constexpr size_t text_segment = module + 80;

void Check(bool ok, const char* name) {
    ++checks;
    if (!ok) { std::cerr << "FAIL: " << name << '\n'; std::exit(1); }
}
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

ParseStatus Parse(const std::vector<uint8_t>& b, FirmwareImage* result) {
    return ParseFirmwareImage(b.data(), b.size(), result);
}
void Reject(const std::vector<uint8_t>& b, ParseStatus expected, const char* name) {
    FirmwareImage image{};
    image.payload_offset = 99; image.module_count = 99;
    const ParseStatus status = Parse(b, &image);
    if (status != expected)
        std::cerr << name << ": got " << ParseStatusName(status)
                  << ", expected " << ParseStatusName(expected) << '\n';
    Check(status == expected, name);
    Check(image.payload_offset == 0 && image.module_count == 0 && image.payload_bytes == 0,
          "failure clears output");
}
void Mutate32(size_t pos, uint32_t value, ParseStatus expected, const char* name,
              bool checksum = false) {
    auto b = Fixture(); U32(b, pos, value); if (checksum) Checksum(b);
    Reject(b, expected, name);
}
}

int main() {
    const auto valid = Fixture();
    FirmwareImage image{};
    Check(Parse(valid, &image) == ParseStatus::Ok, "synthetic profile accepted");
    Check(image.payload_offset == ext && image.payload_bytes == 0xa000 &&
          image.module_count == 1 && image.file_backed_segments == 2 &&
          image.extended_element_count == 1 && image.adsp_header_offset == adsp &&
          image.major == 1 && image.minor == 9 && image.hotfix == 0 && image.build == 1,
          "metadata exact");
    Check(ParseFirmwareImage(nullptr, 1, &image) == ParseStatus::InvalidArgument,
          "null source rejected");
    Check(ParseFirmwareImage(valid.data(), valid.size(), nullptr) == ParseStatus::InvalidArgument,
          "null output rejected");
    Check(ParseFirmwareImage(valid.data(), kMaxImageBytes + 1, &image) == ParseStatus::TooLarge,
          "size cap checked before reading");
    Check(ParseFirmwareImage(valid.data(), 0, &image) == ParseStatus::Truncated, "empty rejected");

    // Every prefix must be rejected; no out-of-bounds reads under sanitizers.
    for (size_t n = 0; n < valid.size(); ++n)
        Check(ParseFirmwareImage(valid.data(), n, &image) != ParseStatus::Ok, "truncated prefix");
    auto raw = std::vector<uint8_t>(valid.begin() + ext, valid.end());
    Check(Parse(raw, &image) == ParseStatus::Ok && image.payload_offset == 0,
          "raw CPD accepted without unsigned XMan");
    // Misalignment must not rely on native packed-structure dereferencing.
    auto unaligned = valid; unaligned.insert(unaligned.begin(), 0);
    Check(ParseFirmwareImage(unaligned.data() + 1, valid.size(), &image) == ParseStatus::Ok,
          "unaligned bytes supported");
    Mutate32(0, 0x31454124, ParseStatus::UnsupportedFormat, "AE1 not silently treated as XMan");
    Mutate32(4, 0xffffffff, ParseStatus::ExtendedHeader, "oversized extended length");
    Mutate32(8, 8, ParseStatus::ExtendedHeader, "undersized extended header");
    Mutate32(8, 20, ParseStatus::ExtendedElement, "truncated extended element");
    Mutate32(12, 0x02000000, ParseStatus::ExtendedHeader, "unknown extended major");
    Mutate32(20, 0, ParseStatus::ExtendedElement, "zero size TLV cannot loop");
    Mutate32(20, 12, ParseStatus::ExtendedElement, "TLV crosses extended boundary");
    Mutate32(20, 9, ParseStatus::ExtendedElement, "unaligned TLV");
    Mutate32(cpd + 4, 0xffffffff, ParseStatus::CseHeader, "entry count bounded");
    Mutate32(cpd + 8, 0x00100102, ParseStatus::CseHeader, "CSE v2 unsupported");
    Mutate32(cpd + 12, 0, ParseStatus::CseHeader, "partition name required");
    auto checksum_bad = valid; checksum_bad[cpd + 11] ^= 1;
    Reject(checksum_bad, ParseStatus::CseChecksum, "CSE checksum corruption");
    Mutate32(cpd + 28, 0xfffffff0, ParseStatus::CseEntry, "entry offset overflow", true);
    Mutate32(cpd + 32, 0xffffffff, ParseStatus::CseEntry, "entry size overflow", true);
    Mutate32(cpd + 32, 0, ParseStatus::CseEntry, "empty entry", true);
    Mutate32(cpd + 28, 16, ParseStatus::CseEntry, "entry overlaps directory", true);
    Mutate32(cpd + 52, 88, ParseStatus::CseEntry, "entries overlap", true);
    Mutate32(cpd + 36, 1, ParseStatus::CseEntry, "entry reserved field", true);
    auto renamed = valid; renamed[cpd + 40] = 'x'; Checksum(renamed);
    Reject(renamed, ParseStatus::CseEntry, "wrong component name");
    Mutate32(css, 5, ParseStatus::CssHeader, "wrong CSS type");
    Mutate32(css + 4, 0xffffffff, ParseStatus::CssHeader, "CSS header length overflow");
    Mutate32(css + 24, 0xffffffff, ParseStatus::CssHeader, "CSS total length overflow");
    Mutate32(css + 120, 96, ParseStatus::CssHeader, "unsupported RSA layout");
    auto css_ext = valid; U32(css_ext, cpd + 32, 652); U32(css_ext, css + 24, 163);
    U32(css_ext, css + 644, 15); U32(css_ext, css + 648, 0); Checksum(css_ext);
    Reject(css_ext, ParseStatus::CssExtension, "CSS zero-length extension");
    U32(css_ext, css + 648, 8);
    Check(Parse(css_ext, &image) == ParseStatus::Ok, "bounded CSS extension");
    Mutate32(meta, 18, ParseStatus::Metadata, "wrong metadata extension");
    Mutate32(meta + 72, 0xfffff000, ParseStatus::Metadata, "wrong metadata base");
    Mutate32(adsp, 0, ParseStatus::AdspHeader, "missing AM1");
    Mutate32(adsp + 4, 0xffffffff, ParseStatus::AdspHeader, "wrong ADSP header length");
    Mutate32(adsp + 16, 0xffffffff, ParseStatus::AdspHeader, "preload multiplication overflow");
    Mutate32(adsp + 48, 0x4000, ParseStatus::AdspHeader, "unexpected load offset");
    Mutate32(adsp + 36, 0, ParseStatus::ModuleTable, "no modules");
    Mutate32(adsp + 36, 0xffffffff, ParseStatus::ModuleTable, "module count overflow");
    Mutate32(module, 0, ParseStatus::ModuleEntry, "missing AME");
    Mutate32(module + 68, 0xffff0000, ParseStatus::ModuleConfig, "config count cap");
    Mutate32(module + 68, 1, ParseStatus::ModuleConfig, "config index outside table");
    auto config = valid; U32(config, module + 68, 0x00010000);
    Check(Parse(config, &image) == ParseStatus::Ok, "one bounded config");
    Mutate32(text_segment + 8, 0xfffff000, ParseStatus::Segment, "segment outside file");
    Mutate32(text_segment + 8, 0x2000, ParseStatus::Segment, "segment overlaps manifest");
    Mutate32(text_segment + 8, 0x8001, ParseStatus::Segment, "segment misaligned");
    Mutate32(text_segment, 0xffff001f, ParseStatus::Segment, "segment pages too large");
    Mutate32(text_segment, 0x1001e, ParseStatus::Segment, "text lacks file contents");
    Mutate32(text_segment, 0x1031f, ParseStatus::Segment, "unknown segment type");
    Mutate32(text_segment + 4, 0xfffff001, ParseStatus::Segment, "virtual address overflow");
    Mutate32(text_segment + 24, 0x3b0207, ParseStatus::Segment, "BSS cannot contain file bytes");
    Mutate32(text_segment + 32, 0x1000, ParseStatus::Segment, "BSS cannot have file offset");
    auto empty = valid; U32(empty, text_segment + 24, 0xf00); U32(empty, text_segment + 28, 0);
    Check(Parse(empty, &image) == ParseStatus::Ok, "empty segment supported");
    auto extended = valid; extended.push_back(0);
    Reject(extended, ParseStatus::AdspHeader, "unexplained trailing byte");

    // Reproducible mutation smoke test; valid content mutations may still pass
    // because this is not a signature verifier. Always preserve output bounds.
    uint32_t rng = 0x36005;
    for (size_t i = 0; i < 20000; ++i) {
        auto b = valid;
        rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
        const size_t pos = rng % b.size();
        b[pos] ^= uint8_t((rng >> 16) | 1);
        const auto before = b;
        const ParseStatus status = Parse(b, &image);
        Check(b == before, "input immutable");
        if (status == ParseStatus::Ok) {
            Check(size_t(image.payload_offset) + image.payload_bytes == b.size() &&
                  image.module_count > 0 && image.module_count <= kMaxModules,
                  "success metadata bounded");
        } else {
            Check(image.payload_offset == 0 && image.module_count == 0, "no partial result");
        }
    }
    Check(std::strcmp(ParseStatusName(static_cast<ParseStatus>(999)), "Unknown") == 0,
          "unknown status safe");
    std::cout << "SOF_PARSER_CHECKS=" << checks << " PASS\n"
              << "HARDWARE_ACCESS=NONE\nSIGNATURE_VERIFICATION=NOT_PERFORMED\n";
}
