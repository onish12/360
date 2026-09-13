// SPDX-License-Identifier: MIT
// Copyright (c) 2026 PHASER360 Open Audio contributors
#ifndef PHASER360_SOF_FIRMWARE_IMAGE_H
#define PHASER360_SOF_FIRMWARE_IMAGE_H

#include <stddef.h>
#include <stdint.h>

namespace phaser360 { namespace sof {

constexpr size_t kMaxImageBytes = 16u * 1024u * 1024u;
constexpr uint32_t kMaxModules = 128;

enum class ParseStatus {
    Ok, InvalidArgument, TooLarge, Truncated, UnsupportedFormat,
    ExtendedHeader, ExtendedElement, CseHeader, CseChecksum,
    CseEntry, CssHeader, CssExtension, Metadata, AdspHeader,
    ModuleTable, ModuleEntry, ModuleConfig, Segment
};

struct FirmwareImage {
    uint32_t payload_offset;
    uint32_t payload_bytes;
    uint32_t adsp_header_offset;
    uint32_t module_table_offset;
    uint32_t module_count;
    uint32_t file_backed_segments;
    uint32_t extended_element_count;
    uint32_t preload_pages;
    uint16_t major;
    uint16_t minor;
    uint16_t hotfix;
    uint16_t build;
};

// Pure bounded byte parser for the legacy APL/GLK v1.8 container profile.
// Ok means structural checks passed, NOT signature/platform/boot validation.
// No allocation, I/O, hardware callbacks, reinterpret-casts or exceptions.
// The input is never changed. On failure *output is zero, not partial data.
// Caller owns both buffers; output must not overlap the input buffer.
ParseStatus ParseFirmwareImage(const uint8_t* bytes, size_t length,
                               FirmwareImage* output) noexcept;
const char* ParseStatusName(ParseStatus status) noexcept;

}} // namespace phaser360::sof
#endif
