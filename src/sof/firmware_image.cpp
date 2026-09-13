// SPDX-License-Identifier: MIT
// Copyright (c) 2026 PHASER360 Open Audio contributors
// Layout references and deliberately unsupported cases: docs/M06_FIRMWARE.md.
#include "firmware_image.h"

namespace phaser360 { namespace sof {
namespace {
constexpr uint32_t kXMan = 0x6e614d58;
constexpr uint32_t kCpd = 0x44504324;
constexpr uint32_t kAm1 = 0x314d4124;
constexpr uint32_t kAme = 0x454d4124;
constexpr size_t kCseTableBytes = 16 + 3 * 24;
constexpr size_t kAdspOffset = 0x2000;
constexpr size_t kAdspHeaderBytes = 52;
constexpr size_t kModuleBytes = 116;
constexpr size_t kConfigBytes = 44;
constexpr size_t kPageBytes = 4096;

bool InRange(size_t length, size_t offset, size_t count) noexcept {
    return offset <= length && count <= length - offset;
}

// All callers establish a containing range before reading these fields.
uint32_t U32(const uint8_t* p) noexcept {
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) |
           (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}
uint16_t U16(const uint8_t* p) noexcept {
    return uint16_t(uint16_t(p[0]) | (uint16_t(p[1]) << 8));
}

bool NameEquals(const uint8_t* p, size_t width, const char* name) noexcept {
    size_t i = 0;
    while (i < width && name[i] != '\0') {
        if (p[i] != static_cast<uint8_t>(name[i])) return false;
        ++i;
    }
    if (i == width) return name[i] == '\0';
    for (; i < width; ++i) if (p[i] != 0) return false;
    return true;
}

struct Entry { size_t offset; size_t length; };
bool Contains(const Entry& entry, size_t offset, size_t length) noexcept {
    return offset >= entry.offset &&
           InRange(entry.length, offset - entry.offset, length);
}

ParseStatus ParseCss(const uint8_t* p, size_t length) noexcept {
    // Type 4, 2048-bit RSA layout. Structural checks do not authenticate RSA.
    constexpr size_t kHeaderBytes = 128 + 256 + 4 + 256;
    if (length < kHeaderBytes) return ParseStatus::CssHeader;
    if (U32(p) != 4 || U32(p + 4) != kHeaderBytes / 4 ||
        U32(p + 8) != 0x10000 || U32(p + 16) != 0x8086 ||
        uint64_t(U32(p + 24)) * 4 != length ||
        !NameEquals(p + 28, 4, "$MN2") ||
        U32(p + 120) != 64 || U32(p + 124) != 1)
        return ParseStatus::CssHeader;

    size_t pos = kHeaderBytes;
    while (pos < length) {
        if (!InRange(length, pos, 4)) return ParseStatus::CssExtension;
        if (U32(p + pos) == 0xffffffffu) { pos += 4; continue; }
        if (!InRange(length, pos, 8)) return ParseStatus::CssExtension;
        const size_t size = U32(p + pos + 4);
        if (size < 8 || size % 4 != 0 || !InRange(length, pos, size))
            return ParseStatus::CssExtension;
        pos += size;
    }
    return ParseStatus::Ok;
}
} // namespace

ParseStatus ParseFirmwareImage(const uint8_t* bytes, size_t length,
                               FirmwareImage* output) noexcept {
    if (!output) return ParseStatus::InvalidArgument;
    *output = FirmwareImage{};
    if (!bytes) return ParseStatus::InvalidArgument;
    if (length > kMaxImageBytes) return ParseStatus::TooLarge;
    if (length < 4) return ParseStatus::Truncated;

    FirmwareImage image{};
    size_t cse = 0;
    if (U32(bytes) == kXMan) {
        if (length < 16) return ParseStatus::Truncated;
        cse = U32(bytes + 4);
        size_t pos = U32(bytes + 8);
        if (cse < 16 || cse > 65536 || cse % 4 != 0 ||
            pos < 16 || pos > cse || pos % 4 != 0 ||
            (U32(bytes + 12) >> 24) != 1)
            return ParseStatus::ExtendedHeader;
        if (cse > length) return ParseStatus::Truncated;
        while (pos < cse) {
            if (!InRange(cse, pos, 8)) return ParseStatus::ExtendedElement;
            const size_t size = U32(bytes + pos + 4);
            if (size < 8 || size % 4 != 0 || !InRange(cse, pos, size))
                return ParseStatus::ExtendedElement;
            pos += size; // Unknown element types are bounded, not interpreted.
            ++image.extended_element_count;
        }
    } else if (U32(bytes) != kCpd) {
        return ParseStatus::UnsupportedFormat; // Including $AE1 and IPC4 layouts.
    }

    if (!InRange(length, cse, kCseTableBytes)) return ParseStatus::Truncated;
    const uint8_t* payload = bytes + cse;
    const size_t payload_bytes = length - cse;
    if (U32(payload) != kCpd || U32(payload + 4) != 3 ||
        payload[8] != 1 || payload[9] != 1 || payload[10] != 16 ||
        !NameEquals(payload + 12, 4, "ADSP")) return ParseStatus::CseHeader;
    uint32_t checksum = 0;
    for (size_t i = 0; i < kCseTableBytes; ++i) checksum += payload[i];
    if ((checksum & 0xffu) != 0) return ParseStatus::CseChecksum;

    const char* const expected_names[3] = {"ADSP.man", "cavs0015.met", "cavs0015"};
    Entry entries[3]{};
    for (size_t i = 0; i < 3; ++i) {
        const uint8_t* p = payload + 16 + i * 24;
        entries[i] = {U32(p + 12), U32(p + 16)};
        if (!NameEquals(p, 12, expected_names[i]) || U32(p + 20) != 0 ||
            entries[i].offset < kCseTableBytes || entries[i].length == 0 ||
            !InRange(payload_bytes, entries[i].offset, entries[i].length))
            return ParseStatus::CseEntry;
        for (size_t j = 0; j < i; ++j) {
            if (entries[i].offset < entries[j].offset + entries[j].length &&
                entries[j].offset < entries[i].offset + entries[i].length)
                return ParseStatus::CseEntry;
        }
    }
    const ParseStatus css = ParseCss(payload + entries[0].offset, entries[0].length);
    if (css != ParseStatus::Ok) return css;
    const uint8_t* metadata = payload + entries[1].offset;
    if (entries[1].length != 96 || U32(metadata) != 17 ||
        U32(metadata + 4) != 96 || U32(metadata + 72) != kAdspOffset)
        return ParseStatus::Metadata;

    if (!Contains(entries[2], kAdspOffset, kAdspHeaderBytes))
        return ParseStatus::AdspHeader;
    const uint8_t* adsp = payload + kAdspOffset;
    if (U32(adsp) != kAm1 || U32(adsp + 4) != kAdspHeaderBytes ||
        !NameEquals(adsp + 8, 8, "ADSPFW") || U32(adsp + 48) != kAdspOffset ||
        uint64_t(U32(adsp + 16)) * kPageBytes != payload_bytes)
        return ParseStatus::AdspHeader;

    const uint32_t modules = U32(adsp + 36);
    const size_t table = kAdspOffset + kAdspHeaderBytes;
    if (modules == 0 || modules > kMaxModules ||
        !Contains(entries[2], table, size_t(modules) * kModuleBytes))
        return ParseStatus::ModuleTable;
    uint32_t configs = 0;
    for (uint32_t i = 0; i < modules; ++i) {
        const uint8_t* module = payload + table + size_t(i) * kModuleBytes;
        if (U32(module) != kAme) return ParseStatus::ModuleEntry;
        configs += U16(module + 70);
    }
    const size_t config_start = table + size_t(modules) * kModuleBytes;
    if (configs > 1024 ||
        !Contains(entries[2], config_start, size_t(configs) * kConfigBytes))
        return ParseStatus::ModuleConfig;
    const size_t manifest_end = config_start + size_t(configs) * kConfigBytes;
    for (uint32_t i = 0; i < modules; ++i) {
        const uint8_t* module = payload + table + size_t(i) * kModuleBytes;
        if (uint32_t(U16(module + 68)) + U16(module + 70) > configs)
            return ParseStatus::ModuleConfig;
        for (size_t j = 0; j < 3; ++j) {
            const uint8_t* segment = module + 80 + j * 12;
            const uint32_t flags = U32(segment);
            const uint32_t type = (flags >> 8) & 0xfu;
            const size_t segment_bytes = size_t(flags >> 16) * kPageBytes;
            const size_t offset = U32(segment + 8);
            if (uint64_t(U32(segment + 4)) + segment_bytes > 0x100000000ull)
                return ParseStatus::Segment;
            if (type == 0 || type == 1) {
                if ((flags & 5u) != 5 || segment_bytes == 0 ||
                    offset < manifest_end || offset % kPageBytes != 0 ||
                    !Contains(entries[2], offset, segment_bytes))
                    return ParseStatus::Segment;
                ++image.file_backed_segments;
            } else if (type == 2) {
                // BSS occupies DSP memory, not bytes in the firmware file.
                if ((flags & 5u) != 0 || offset != 0)
                    return ParseStatus::Segment;
            } else if (type == 15) {
                if ((flags & 5u) != 0 || offset != 0 || segment_bytes != 0)
                    return ParseStatus::Segment;
            } else {
                return ParseStatus::Segment;
            }
        }
    }
    if (image.file_backed_segments == 0) return ParseStatus::Segment;
    image.payload_offset = static_cast<uint32_t>(cse);
    image.payload_bytes = static_cast<uint32_t>(payload_bytes);
    image.adsp_header_offset = static_cast<uint32_t>(cse + kAdspOffset);
    image.module_table_offset = static_cast<uint32_t>(cse + table);
    image.module_count = modules;
    image.preload_pages = U32(adsp + 16);
    image.major = U16(adsp + 28);
    image.minor = U16(adsp + 30);
    image.hotfix = U16(adsp + 32);
    image.build = U16(adsp + 34);
    *output = image;
    return ParseStatus::Ok;
}

const char* ParseStatusName(ParseStatus status) noexcept {
    switch (status) {
#define PHASER_SOF_STATUS(x) case ParseStatus::x: return #x
        PHASER_SOF_STATUS(Ok);
        PHASER_SOF_STATUS(InvalidArgument);
        PHASER_SOF_STATUS(TooLarge);
        PHASER_SOF_STATUS(Truncated);
        PHASER_SOF_STATUS(UnsupportedFormat);
        PHASER_SOF_STATUS(ExtendedHeader);
        PHASER_SOF_STATUS(ExtendedElement);
        PHASER_SOF_STATUS(CseHeader);
        PHASER_SOF_STATUS(CseChecksum);
        PHASER_SOF_STATUS(CseEntry);
        PHASER_SOF_STATUS(CssHeader);
        PHASER_SOF_STATUS(CssExtension);
        PHASER_SOF_STATUS(Metadata);
        PHASER_SOF_STATUS(AdspHeader);
        PHASER_SOF_STATUS(ModuleTable);
        PHASER_SOF_STATUS(ModuleEntry);
        PHASER_SOF_STATUS(ModuleConfig);
        PHASER_SOF_STATUS(Segment);
#undef PHASER_SOF_STATUS
    }
    return "Unknown";
}
}} // namespace phaser360::sof
